#include "reward_site_catalog.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../unlocks/definition.h"
#include "../items/item_catalog.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::reward_sites {
namespace {

/** Version one stores site identity, item progression, and character object transitions. */
constexpr int kSchemaVersion = 1;
/** SQLite consumes the complete null-terminated query when the byte count is negative. */
constexpr int kCompleteSqlText = -1;
/** Build-filtered queries bind executable timestamp first and image size second. */
constexpr int kImageTimestampParameter = 1, kImageSizeParameter = 2;
/** Recovered rows use this constrained SQLite provenance value. */
constexpr std::string_view kRecoveredProvenance = "recovered";
/** Evidence-backed rows use this constrained SQLite provenance value. */
constexpr std::string_view kReconstructedProvenance = "reconstructed";
/** Site rows sort by wire index so binary lookup stays deterministic. */
constexpr char kSiteQuery[] = "SELECT site_index,provenance FROM reward_sites "
                              "WHERE image_timestamp=?1 AND image_size=?2 ORDER BY site_index";
/** Item progression rows sort by site and ordinal so each site owns one contiguous range. */
constexpr char kItemProgressionQuery[] =
    "SELECT site_index,ordinal,source_item_index,source_item_hash,"
    "successor_item_index,successor_item_hash FROM reward_site_item_progressions "
    "WHERE image_timestamp=?1 AND image_size=?2 ORDER BY site_index,ordinal";
/** Character object rows sort by site and ordinal so gaps are rejected while loading. */
constexpr char kCharacterObjectTransitionQuery[] =
    "SELECT site_index,ordinal,row_index,expected_value,next_value "
    "FROM reward_site_character_object_transitions "
    "WHERE image_timestamp=?1 AND image_size=?2 ORDER BY site_index,ordinal";

/** Selected columns stay aligned with kSiteQuery. */
enum SiteColumn : int {
    /** Site index is the first selected column. */
    kSiteIndexColumn,
    /** Provenance is the second selected column. */
    kSiteProvenanceColumn,
};

/** Selected columns stay aligned with kItemProgressionQuery. */
enum ItemProgressionColumn : int {
    /** Site index is the first selected column. */
    kItemSiteIndexColumn,
    /** Operation ordinal is the second selected column. */
    kItemOrdinalColumn,
    /** Source item index is the third selected column. */
    kSourceItemIndexColumn,
    /** Source item hash is the fourth selected column. */
    kSourceItemHashColumn,
    /** Successor item index is the fifth selected column. */
    kSuccessorItemIndexColumn,
    /** Successor item hash is the sixth selected column. */
    kSuccessorItemHashColumn,
};

/** Selected columns stay aligned with kCharacterObjectTransitionQuery. */
enum CharacterObjectTransitionColumn : int {
    /** Site index is the first selected column. */
    kCharacterSiteIndexColumn,
    /** Operation ordinal is the second selected column. */
    kCharacterOrdinalColumn,
    /** Character-object row is the third selected column. */
    kCharacterRowIndexColumn,
    /** Expected value is the fourth selected column. */
    kCharacterExpectedValueColumn,
    /** Replacement value is the fifth selected column. */
    kCharacterNextValueColumn,
};

/** PRAGMA user_version returns its value in the first result column. */
constexpr int kSchemaVersionColumn = 0;

core::threading::SrwLock g_lock;
std::vector<Definition> g_definitions;
std::vector<ItemProgression> g_itemProgressions;
std::vector<CharacterObjectTransition> g_characterObjectTransitions;

/** Holds an unpublished catalog until every row passes validation. */
struct StagedCatalog {
    std::vector<Definition> sites;
    std::vector<ItemProgression> itemProgressions;
    std::vector<CharacterObjectTransition> characterObjectTransitions;
};

/** Owns the startup-only SQLite connection until validation completes. */
class Database final {
public:
    Database() = default;
    ~Database() {
        if (value_ != nullptr) {
            (void)sqlite3_close_v2(value_);
        }
    }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] bool open() noexcept {
        return sqlite3_open_v2(":memory:",
                               &value_,
                               SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                               nullptr)
               == SQLITE_OK;
    }

    [[nodiscard]] bool execute(std::string_view sql) noexcept {
        if (value_ == nullptr || sql.empty()) {
            return false;
        }
        const std::string owned(sql);
        return sqlite3_exec(value_, owned.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    [[nodiscard]] bool close() noexcept {
        if (value_ == nullptr) {
            return true;
        }
        const int result = sqlite3_close(value_);
        if (result == SQLITE_OK) {
            value_ = nullptr;
        }
        return result == SQLITE_OK;
    }

    [[nodiscard]] sqlite3* get() const noexcept {
        return value_;
    }

private:
    sqlite3* value_{};
};

/** Finalizes one startup query before its database is closed. */
class Statement final {
public:
    Statement(sqlite3* database, const char* sql) noexcept {
        if (database != nullptr
            && sqlite3_prepare_v2(database, sql, kCompleteSqlText, &value_, nullptr) != SQLITE_OK) {
            sqlite3_finalize(value_);
            value_ = nullptr;
        }
    }
    ~Statement() {
        sqlite3_finalize(value_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool bind_build(const BuildIdentity& build) noexcept {
        return value_ != nullptr
               && sqlite3_bind_int64(value_, kImageTimestampParameter, build.imageTimestamp)
                      == SQLITE_OK
               && sqlite3_bind_int64(value_, kImageSizeParameter, build.imageSize) == SQLITE_OK;
    }

    [[nodiscard]] int step() noexcept {
        return value_ != nullptr ? sqlite3_step(value_) : SQLITE_ERROR;
    }

    /**
     * Reads one SQL integer without narrowing it into the requested type.
     * @tparam T Integral destination type.
     * @param column Zero-based result-column index.
     * @param output Receives the checked value; unchanged on failure.
     * @return False for a null statement, non-integer column, or out-of-range value.
     */
    template <typename T> [[nodiscard]] bool integer(int column, T& output) const noexcept {
        static_assert(std::is_integral_v<T>);
        if (value_ == nullptr || sqlite3_column_type(value_, column) != SQLITE_INTEGER) {
            return false;
        }
        const sqlite3_int64 raw = sqlite3_column_int64(value_, column);
        if constexpr (std::is_unsigned_v<T>) {
            if (raw < 0
                || static_cast<std::uint64_t>(raw)
                       > static_cast<std::uint64_t>((std::numeric_limits<T>::max)())) {
                return false;
            }
        } else if (raw < static_cast<sqlite3_int64>((std::numeric_limits<T>::min)())
                   || raw > static_cast<sqlite3_int64>((std::numeric_limits<T>::max)())) {
            return false;
        }
        output = static_cast<T>(raw);
        return true;
    }

    [[nodiscard]] bool text(int column, std::string_view& output) const noexcept {
        if (value_ == nullptr || sqlite3_column_type(value_, column) != SQLITE_TEXT) {
            return false;
        }
        const auto* bytes = reinterpret_cast<const char*>(sqlite3_column_text(value_, column));
        if (bytes == nullptr) {
            return false;
        }
        output = {bytes, static_cast<std::size_t>(sqlite3_column_bytes(value_, column))};
        return true;
    }

private:
    sqlite3_stmt* value_{};
};

/**
 * Maps one constrained SQLite provenance value.
 * @param value Stored provenance spelling.
 * @return The matching provenance, or none for an unknown value.
 */
[[nodiscard]] Provenance provenance(std::string_view value) noexcept {
    if (value == kRecoveredProvenance) {
        return Provenance::recovered;
    }
    if (value == kReconstructedProvenance) {
        return Provenance::reconstructed;
    }
    return Provenance::none;
}

/**
 * Decodes one site row from kSiteQuery.
 * @param query Query positioned on a row.
 * @param row Receives the typed site definition.
 * @return False when any selected field is absent or invalid.
 */
[[nodiscard]] bool read_site_row(const Statement& query, Definition& row) noexcept {
    std::string_view origin;
    return query.integer(kSiteIndexColumn, row.siteIndex)
           && query.text(kSiteProvenanceColumn, origin)
           && (row.provenance = provenance(origin)) != Provenance::none;
}

/**
 * Finds one mutable definition in an ordered staging bank.
 * @param definitions Definitions sorted by site index.
 * @param siteIndex Native Reward Site index.
 * @return The matching row, or null when absent.
 */
[[nodiscard]] Definition* find_site(std::vector<Definition>& definitions,
                                    std::uint16_t siteIndex) noexcept {
    const auto found = std::lower_bound(
        definitions.begin(), definitions.end(), siteIndex, [](const Definition& row, auto index) {
            return row.siteIndex < index;
        });
    return found != definitions.end() && found->siteIndex == siteIndex ? &*found : nullptr;
}

/**
 * Reads the active build's sites in canonical lookup order.
 * @param database Open catalog database.
 * @param build Active executable identity.
 * @param output Receives ordered definitions; unchanged rows remain on failure.
 * @return False for query failure, invalid provenance, duplicates, or an empty result.
 */
[[nodiscard]] bool read_sites(sqlite3* database,
                              const BuildIdentity& build,
                              std::vector<Definition>& output) noexcept {
    Statement query(database, kSiteQuery);
    if (!query.bind_build(build)) {
        return false;
    }
    int result = query.step();
    while (result == SQLITE_ROW) {
        Definition row{};
        if (!read_site_row(query, row)
            || (!output.empty() && output.back().siteIndex >= row.siteIndex)) {
            return false;
        }
        output.push_back(row);
        result = query.step();
    }
    return result == SQLITE_DONE && !output.empty();
}

/**
 * Decodes one item progression row from kItemProgressionQuery.
 * @param query Query positioned on a row.
 * @param siteIndex Receives the owning Reward Site index.
 * @param ordinal Receives the operation ordinal.
 * @param row Receives the typed item progression.
 * @return False when any selected field is absent or out of range.
 */
[[nodiscard]] bool read_item_progression_row(const Statement& query,
                                             std::uint16_t& siteIndex,
                                             std::uint16_t& ordinal,
                                             ItemProgression& row) noexcept {
    return query.integer(kItemSiteIndexColumn, siteIndex)
           && query.integer(kItemOrdinalColumn, ordinal)
           && query.integer(kSourceItemIndexColumn, row.sourceItemIndex)
           && query.integer(kSourceItemHashColumn, row.sourceItemHash)
           && query.integer(kSuccessorItemIndexColumn, row.successorItemIndex)
           && query.integer(kSuccessorItemHashColumn, row.successorItemHash);
}

/**
 * Checks one item progression against its owning site and installed item domain.
 * @param site Owning site, or null when the query names no site.
 * @param ordinal Operation ordinal from SQLite.
 * @param row Decoded item progression.
 * @return True when the row is the site's next valid operation.
 */
[[nodiscard]] bool valid_item_progression(const Definition* site,
                                          std::uint16_t ordinal,
                                          const ItemProgression& row) noexcept {
    return site != nullptr && ordinal == site->itemProgressionCount
           && site->itemProgressionCount != (std::numeric_limits<std::uint16_t>::max)()
           && row.sourceItemIndex < items::kDefinitionCapacity
           && row.successorItemIndex < items::kDefinitionCapacity && row.sourceItemHash != 0
           && row.successorItemHash != 0;
}

/**
 * Reads item progression rows and rejects ordinal gaps or invalid item identities.
 * @param database Open catalog database.
 * @param build Active executable identity.
 * @param sites Ordered definitions whose operation ranges are filled in place.
 * @param output Receives typed progression rows.
 * @return False for query failure, missing sites, ordinal gaps, or invalid identities.
 */
[[nodiscard]] bool read_item_progressions(sqlite3* database,
                                          const BuildIdentity& build,
                                          std::vector<Definition>& sites,
                                          std::vector<ItemProgression>& output) noexcept {
    Statement query(database, kItemProgressionQuery);
    if (!query.bind_build(build)) {
        return false;
    }
    int result = query.step();
    while (result == SQLITE_ROW) {
        std::uint16_t siteIndex{};
        std::uint16_t ordinal{};
        ItemProgression row{};
        if (!read_item_progression_row(query, siteIndex, ordinal, row)) {
            return false;
        }
        Definition* site = find_site(sites, siteIndex);
        if (!valid_item_progression(site, ordinal, row)) {
            return false;
        }
        if (site->itemProgressionCount == 0) {
            site->itemProgressionOffset = output.size();
        }
        output.push_back(row);
        ++site->itemProgressionCount;
        result = query.step();
    }
    return result == SQLITE_DONE;
}

/**
 * Decodes one character-object row from kCharacterObjectTransitionQuery.
 * @param query Query positioned on a row.
 * @param siteIndex Receives the owning Reward Site index.
 * @param ordinal Receives the operation ordinal.
 * @param row Receives the typed character-object transition.
 * @return False when any selected field is absent or out of range.
 */
[[nodiscard]] bool read_character_object_transition_row(const Statement& query,
                                                        std::uint16_t& siteIndex,
                                                        std::uint16_t& ordinal,
                                                        CharacterObjectTransition& row) noexcept {
    return query.integer(kCharacterSiteIndexColumn, siteIndex)
           && query.integer(kCharacterOrdinalColumn, ordinal)
           && query.integer(kCharacterRowIndexColumn, row.rowIndex)
           && query.integer(kCharacterExpectedValueColumn, row.expectedValue)
           && query.integer(kCharacterNextValueColumn, row.nextValue);
}

/**
 * Checks one character-object transition against its owning site and state bank.
 * @param site Owning site, or null when the query names no site.
 * @param ordinal Operation ordinal from SQLite.
 * @param row Decoded character-object transition.
 * @return True when the row is the site's next valid state operation.
 */
[[nodiscard]] bool valid_character_object_transition(
    const Definition* site, std::uint16_t ordinal, const CharacterObjectTransition& row) noexcept {
    return site != nullptr && ordinal == site->characterObjectTransitionCount
           && site->characterObjectTransitionCount != (std::numeric_limits<std::uint16_t>::max)()
           && row.rowIndex < unlocks::kCharacterObjectValueCapacity
           && row.expectedValue != row.nextValue;
}

/**
 * Reads selected-character transitions and rejects ordinal gaps or no-op writes.
 * @param database Open catalog database.
 * @param build Active executable identity.
 * @param sites Ordered definitions whose operation ranges are filled in place.
 * @param output Receives typed character-object transitions.
 * @return False for query failure, missing sites, ordinal gaps, or invalid rows.
 */
[[nodiscard]] bool
read_character_object_transitions(sqlite3* database,
                                  const BuildIdentity& build,
                                  std::vector<Definition>& sites,
                                  std::vector<CharacterObjectTransition>& output) noexcept {
    Statement query(database, kCharacterObjectTransitionQuery);
    if (!query.bind_build(build)) {
        return false;
    }
    int result = query.step();
    while (result == SQLITE_ROW) {
        std::uint16_t siteIndex{};
        std::uint16_t ordinal{};
        CharacterObjectTransition row{};
        if (!read_character_object_transition_row(query, siteIndex, ordinal, row)) {
            return false;
        }
        Definition* site = find_site(sites, siteIndex);
        if (!valid_character_object_transition(site, ordinal, row)) {
            return false;
        }
        if (site->characterObjectTransitionCount == 0) {
            site->characterObjectTransitionOffset = output.size();
        }
        output.push_back(row);
        ++site->characterObjectTransitionCount;
        result = query.step();
    }
    return result == SQLITE_DONE;
}

/**
 * Checks the schema version and all foreign-key references.
 * @param database Open catalog database.
 * @return True when the schema and definitions satisfy both checks.
 */
[[nodiscard]] bool schema_valid(sqlite3* database) noexcept {
    Statement version(database, "PRAGMA user_version");
    int schemaVersion{};
    if (version.step() != SQLITE_ROW || !version.integer(kSchemaVersionColumn, schemaVersion)
        || schemaVersion != kSchemaVersion || version.step() != SQLITE_DONE) {
        return false;
    }
    Statement foreignKeys(database, "PRAGMA foreign_key_check");
    return foreignKeys.step() == SQLITE_DONE;
}

/**
 * Rejects definitions that would execute no typed operation.
 * @param definitions Fully loaded site definitions.
 * @return True when every site owns at least one operation.
 */
[[nodiscard]] bool every_site_has_an_effect(std::span<const Definition> definitions) noexcept {
    return std::all_of(definitions.begin(), definitions.end(), [](const Definition& definition) {
        return definition.itemProgressionCount != 0
               || definition.characterObjectTransitionCount != 0;
    });
}

/**
 * Builds the in-memory database from the complete source-controlled scripts.
 * @param database Empty startup database.
 * @param schema Complete schema script.
 * @param definitions Complete definition script.
 * @return False when setup or either script fails; failed script writes are rolled back.
 */
[[nodiscard]] bool create_database(Database& database,
                                   std::string_view schema,
                                   std::string_view definitions) noexcept {
    if (!database.open() || !database.execute("PRAGMA foreign_keys=ON")
        || !database.execute("BEGIN IMMEDIATE")) {
        return false;
    }
    if (database.execute(schema) && database.execute(definitions) && database.execute("COMMIT")) {
        return true;
    }
    (void)database.execute("ROLLBACK");
    return false;
}

/**
 * Reads and validates all rows for one executable build.
 * @param database Open catalog database.
 * @param build Active executable identity.
 * @param output Receives the unpublished catalog.
 * @return False when any catalog contract fails.
 */
[[nodiscard]] bool
read_catalog(sqlite3* database, const BuildIdentity& build, StagedCatalog& output) noexcept {
    return schema_valid(database) && read_sites(database, build, output.sites)
           && read_item_progressions(database, build, output.sites, output.itemProgressions)
           && read_character_object_transitions(
               database, build, output.sites, output.characterObjectTransitions)
           && every_site_has_an_effect(output.sites);
}

/**
 * Checks one item progression against the installed item catalog.
 * @param row Authored item replacement.
 * @return True when both indices still name their authored hashes.
 */
[[nodiscard]] bool installed_items_match(const ItemProgression& row) noexcept {
    items::Definition source{};
    items::Definition successor{};
    return items::find_index(row.sourceItemIndex, source)
           && items::find_index(row.successorItemIndex, successor)
           && source.definitionHash == row.sourceItemHash
           && successor.definitionHash == row.successorItemHash;
}

/**
 * Copies one current catalog range without exposing the backing vectors.
 * @tparam Row Typed operation row.
 * @param bank Published operation bank.
 * @param offset First row owned by the definition.
 * @param rows Number of rows owned by the definition.
 * @param output Caller-owned destination.
 * @param count Receives the copied row count, or zero on failure.
 * @return False when the source range or destination capacity is invalid.
 */
template <typename Row>
[[nodiscard]] bool copy_range(std::span<const Row> bank,
                              std::size_t offset,
                              std::size_t rows,
                              std::span<Row> output,
                              std::size_t& count) noexcept {
    count = 0;
    if (offset > bank.size() || rows > bank.size() - offset || output.size() < rows) {
        return false;
    }
    std::copy_n(bank.begin() + static_cast<std::ptrdiff_t>(offset), rows, output.begin());
    count = rows;
    return true;
}

} // namespace

/**
 * Loads, validates, closes, and publishes one build's static Reward Site content.
 * @param build Active executable identity.
 * @param schema Complete source-controlled schema script.
 * @param definitions Complete source-controlled definition script.
 * @return True after the validated catalog replaces the published catalog.
 */
bool load(const BuildIdentity& build,
          std::string_view schema,
          std::string_view definitions) noexcept {
    Database database;
    if (!create_database(database, schema, definitions)) {
        return false;
    }
    StagedCatalog loaded;
    if (!read_catalog(database.get(), build, loaded) || !database.close()) {
        return false;
    }

    const std::lock_guard guard(g_lock);
    g_definitions = std::move(loaded.sites);
    g_itemProgressions = std::move(loaded.itemProgressions);
    g_characterObjectTransitions = std::move(loaded.characterObjectTransitions);
    return true;
}

/** Clears the static catalog under one exclusive hold. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    std::vector<Definition>{}.swap(g_definitions);
    std::vector<ItemProgression>{}.swap(g_itemProgressions);
    std::vector<CharacterObjectTransition>{}.swap(g_characterObjectTransitions);
}

bool ready() noexcept {
    const std::shared_lock guard(g_lock);
    return !g_definitions.empty();
}

/**
 * Finds one site by native index.
 * @param siteIndex Native Reward Site index.
 * @param definition Receives the matching site; cleared when absent.
 * @return True when the published catalog contains the site.
 */
bool find(std::uint16_t siteIndex, Definition& definition) noexcept {
    definition = {};
    const std::shared_lock guard(g_lock);
    const auto found =
        std::lower_bound(g_definitions.begin(),
                         g_definitions.end(),
                         siteIndex,
                         [](const Definition& row, auto index) { return row.siteIndex < index; });
    if (found == g_definitions.end() || found->siteIndex != siteIndex) {
        return false;
    }
    definition = *found;
    return true;
}

/**
 * Copies and validates one site's item progression range.
 * @param definition Site returned by find().
 * @param output Caller-owned operation storage.
 * @param count Receives the copied count, or zero on failure.
 * @return True when every installed item index still matches its authored hash.
 */
bool item_progressions(const Definition& definition,
                       std::span<ItemProgression> output,
                       std::size_t& count) noexcept {
    {
        const std::shared_lock guard(g_lock);
        if (!copy_range(std::span<const ItemProgression>{g_itemProgressions},
                        definition.itemProgressionOffset,
                        definition.itemProgressionCount,
                        output,
                        count)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!installed_items_match(output[index])) {
            std::fill_n(output.begin(), count, ItemProgression{});
            count = 0;
            return false;
        }
    }
    return true;
}

/**
 * Copies one site's selected-character object transition range.
 * @param definition Site returned by find().
 * @param output Caller-owned operation storage.
 * @param count Receives the copied count, or zero on failure.
 * @return True when the published catalog still contains the complete range.
 */
bool character_object_transitions(const Definition& definition,
                                  std::span<CharacterObjectTransition> output,
                                  std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return copy_range(std::span<const CharacterObjectTransition>{g_characterObjectTransitions},
                      definition.characterObjectTransitionOffset,
                      definition.characterObjectTransitionCount,
                      output,
                      count);
}

std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.size();
}

} // namespace sunrise::state::build_data::reward_sites
