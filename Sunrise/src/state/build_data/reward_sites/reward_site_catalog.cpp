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
    kSiteIndexColumn,
    kSiteProvenanceColumn,
};

/** Selected columns stay aligned with kItemProgressionQuery. */
enum ItemProgressionColumn : int {
    kItemSiteIndexColumn,
    kItemOrdinalColumn,
    kSourceItemIndexColumn,
    kSourceItemHashColumn,
    kSuccessorItemIndexColumn,
    kSuccessorItemHashColumn,
};

/** Selected columns stay aligned with kCharacterObjectTransitionQuery. */
enum CharacterObjectTransitionColumn : int {
    kCharacterSiteIndexColumn,
    kCharacterOrdinalColumn,
    kCharacterRowIndexColumn,
    kCharacterExpectedValueColumn,
    kCharacterNextValueColumn,
};

/** PRAGMA user_version returns its value in the first result column. */
constexpr int kSchemaVersionColumn = 0;

core::threading::SrwLock g_lock;
std::vector<Definition> g_definitions;
std::vector<ItemProgression> g_itemProgressions;
std::vector<CharacterObjectTransition> g_characterObjectTransitions;

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

    /** Reads one SQL integer without narrowing it into the requested type. */
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

[[nodiscard]] Provenance provenance(std::string_view value) noexcept {
    if (value == "recovered") {
        return Provenance::recovered;
    }
    if (value == "reconstructed") {
        return Provenance::reconstructed;
    }
    return Provenance::none;
}

[[nodiscard]] Definition* find_site(std::vector<Definition>& definitions,
                                    std::uint16_t siteIndex) noexcept {
    const auto found = std::lower_bound(
        definitions.begin(), definitions.end(), siteIndex, [](const Definition& row, auto index) {
            return row.siteIndex < index;
        });
    return found != definitions.end() && found->siteIndex == siteIndex ? &*found : nullptr;
}

/** Reads the active build's sites in canonical lookup order. */
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
        std::string_view origin;
        if (!query.integer(kSiteIndexColumn, row.siteIndex)
            || !query.text(kSiteProvenanceColumn, origin)
            || (row.provenance = provenance(origin)) == Provenance::none
            || (!output.empty() && output.back().siteIndex >= row.siteIndex)) {
            return false;
        }
        output.push_back(row);
        result = query.step();
    }
    return result == SQLITE_DONE && !output.empty();
}

/** Reads item progression rows and rejects ordinal gaps or invalid item identities. */
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
        if (!query.integer(kItemSiteIndexColumn, siteIndex)
            || !query.integer(kItemOrdinalColumn, ordinal)
            || !query.integer(kSourceItemIndexColumn, row.sourceItemIndex)
            || !query.integer(kSourceItemHashColumn, row.sourceItemHash)
            || !query.integer(kSuccessorItemIndexColumn, row.successorItemIndex)
            || !query.integer(kSuccessorItemHashColumn, row.successorItemHash)) {
            return false;
        }
        Definition* site = find_site(sites, siteIndex);
        if (site == nullptr || ordinal != site->itemProgressionCount
            || site->itemProgressionCount == (std::numeric_limits<std::uint16_t>::max)()
            || row.sourceItemIndex >= items::kDefinitionCapacity
            || row.successorItemIndex >= items::kDefinitionCapacity || row.sourceItemHash == 0
            || row.successorItemHash == 0) {
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

/** Reads selected-character transitions and rejects ordinal gaps or no-op writes. */
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
        if (!query.integer(kCharacterSiteIndexColumn, siteIndex)
            || !query.integer(kCharacterOrdinalColumn, ordinal)
            || !query.integer(kCharacterRowIndexColumn, row.rowIndex)
            || !query.integer(kCharacterExpectedValueColumn, row.expectedValue)
            || !query.integer(kCharacterNextValueColumn, row.nextValue)) {
            return false;
        }
        Definition* site = find_site(sites, siteIndex);
        if (site == nullptr || ordinal != site->characterObjectTransitionCount
            || site->characterObjectTransitionCount == (std::numeric_limits<std::uint16_t>::max)()
            || row.rowIndex >= unlocks::kCharacterObjectValueCapacity
            || row.expectedValue == row.nextValue) {
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

[[nodiscard]] bool every_site_has_an_effect(std::span<const Definition> definitions) noexcept {
    return std::all_of(definitions.begin(), definitions.end(), [](const Definition& definition) {
        return definition.itemProgressionCount != 0
               || definition.characterObjectTransitionCount != 0;
    });
}

/** Copies one current catalog range without exposing the backing vectors. */
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

/** Loads, validates, closes, and publishes one build's static Reward Site content. */
bool load(const BuildIdentity& build,
          std::string_view schema,
          std::string_view definitions) noexcept {
    Database database;
    if (!database.open() || !database.execute("PRAGMA foreign_keys=ON")
        || !database.execute("BEGIN IMMEDIATE")) {
        return false;
    }
    if (!database.execute(schema) || !database.execute(definitions)
        || !database.execute("COMMIT")) {
        (void)database.execute("ROLLBACK");
        return false;
    }

    std::vector<Definition> loadedSites;
    std::vector<ItemProgression> loadedItemProgressions;
    std::vector<CharacterObjectTransition> loadedCharacterObjectTransitions;
    if (!schema_valid(database.get()) || !read_sites(database.get(), build, loadedSites)
        || !read_item_progressions(database.get(), build, loadedSites, loadedItemProgressions)
        || !read_character_object_transitions(
            database.get(), build, loadedSites, loadedCharacterObjectTransitions)
        || !every_site_has_an_effect(loadedSites) || !database.close()) {
        return false;
    }

    const std::lock_guard guard(g_lock);
    g_definitions = std::move(loadedSites);
    g_itemProgressions = std::move(loadedItemProgressions);
    g_characterObjectTransitions = std::move(loadedCharacterObjectTransitions);
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

/** Finds one site by native index. */
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

/** Copies and validates one site's item progression range. */
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
        items::Definition source{};
        items::Definition successor{};
        const ItemProgression& row = output[index];
        if (!items::find_index(row.sourceItemIndex, source)
            || !items::find_index(row.successorItemIndex, successor)
            || source.definitionHash != row.sourceItemHash
            || successor.definitionHash != row.successorItemHash) {
            std::fill_n(output.begin(), count, ItemProgression{});
            count = 0;
            return false;
        }
    }
    return true;
}

/** Copies one site's selected-character object transition range. */
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
