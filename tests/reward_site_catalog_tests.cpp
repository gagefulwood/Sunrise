#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "../Sunrise/src/state/build_data/items/item_catalog.h"
#include "../Sunrise/src/state/build_data/reward_sites/reward_site_catalog.h"

namespace test_data {

/** Build 86657 is identified by these PE timestamp and image-size fields. */
constexpr std::uint32_t kSupportedImageTimestamp = 1598231435U;
constexpr std::uint32_t kSupportedImageSize = 145091072U;
/** Configured equipment does not identify executable-owned Reward Site definitions. */
constexpr std::uint64_t kIgnoredEquipmentHash = 0;
/** Build 86657 is the only executable identity covered by the first definition set. */
constexpr sunrise::state::build_data::BuildIdentity kSupportedBuild{
    kSupportedImageTimestamp, kSupportedImageSize, kIgnoredEquipmentHash};
/** A neighboring timestamp proves definitions do not cross executable builds. */
constexpr sunrise::state::build_data::BuildIdentity kUnsupportedBuild{
    kSupportedImageTimestamp + 1, kSupportedImageSize, kIgnoredEquipmentHash};
/** Site 11481 is the first reconstructed Unlimited Power completion effect. */
constexpr std::uint16_t kSiteIndex = 11481;
/** Installed item rows 15284 and 15285 identify the supported stage replacement. */
constexpr std::uint16_t kSourceItemIndex = 15284, kSuccessorItemIndex = 15285;
/** These hashes bind both native item rows to the supported executable content. */
constexpr std::uint32_t kSourceItemHash = 3398477426U;
constexpr std::uint32_t kSuccessorItemHash = 4280995080U;
/** Character object row 526 advances the supported stage from 100 to 200. */
constexpr std::uint16_t kCharacterObjectRow = 526;
constexpr std::int32_t kExpectedValue = 100, kNextValue = 200;
/** Hash one intentionally differs from both supported installed item identities. */
constexpr std::uint32_t kMismatchedItemHash = 1U;
/** The definition set contains one row in each supported operation family. */
constexpr std::size_t kExpectedOperationCount = 1;
/** The first definition set publishes exactly one Reward Site. */
constexpr std::size_t kExpectedSiteCount = 1;
/** The adjacent site index is absent from the definition set. */
constexpr std::uint16_t kUnknownSiteIndex = kSiteIndex + 1;
/** The executable accepts its path plus one resource-directory argument. */
constexpr int kExpectedArgumentCount = 2, kResourceDirectoryArgument = 1;

bool validItemRows = true;

} // namespace test_data

namespace sunrise::state::build_data::items {

/**
 * Supplies the two installed item identities needed by this isolated catalog check.
 * @param definitionIndex Installed item-table index.
 * @param definition Receives the matching identity; cleared when absent.
 * @return True when the fixture contains the requested row.
 */
bool find_index(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    if (definitionIndex == test_data::kSourceItemIndex) {
        definition.definitionIndex = definitionIndex;
        definition.definitionHash =
            test_data::validItemRows ? test_data::kSourceItemHash : test_data::kMismatchedItemHash;
        return true;
    }
    if (definitionIndex == test_data::kSuccessorItemIndex) {
        definition.definitionIndex = definitionIndex;
        definition.definitionHash = test_data::kSuccessorItemHash;
        return true;
    }
    return false;
}

} // namespace sunrise::state::build_data::items

namespace {

using sunrise::state::build_data::reward_sites::CharacterObjectTransition;
using sunrise::state::build_data::reward_sites::Definition;
using sunrise::state::build_data::reward_sites::ItemProgression;
using sunrise::state::build_data::reward_sites::Provenance;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "reward_site_catalog: %s\n", message);
        std::abort();
    }
}

/**
 * Reads one source-controlled SQL resource without changing it.
 * @param directory Resource database directory.
 * @param name SQL resource filename.
 * @return Complete file contents.
 */
std::string read_file(const char* directory, const char* name) {
    const std::string path = std::string(directory) + "/" + name;
    std::FILE* stream = nullptr;
    check(fopen_s(&stream, path.c_str(), "rb") == 0 && stream != nullptr,
          "SQL resource could not be opened");
    check(std::fseek(stream, 0, SEEK_END) == 0, "SQL resource seek failed");
    const long length = std::ftell(stream);
    check(length >= 0 && std::fseek(stream, 0, SEEK_SET) == 0, "SQL resource length failed");
    std::string contents(static_cast<std::size_t>(length), '\0');
    check(contents.empty()
              || std::fread(contents.data(), 1, contents.size(), stream) == contents.size(),
          "SQL resource read failed");
    check(std::fclose(stream) == 0, "SQL resource close failed");
    return contents;
}

/**
 * Verifies build binding, typed resolution, failed-load atomicity, and clear semantics.
 * @param schema Complete schema script.
 * @param definitions Complete definition script.
 */
void verify_catalog(std::string_view schema, std::string_view definitions) {
    namespace sites = sunrise::state::build_data::reward_sites;

    sites::clear();
    check(!sites::ready() && sites::count() == 0, "clear left a published catalog");
    check(sites::load(test_data::kSupportedBuild, schema, definitions), "supported build rejected");
    check(sites::ready() && sites::count() == test_data::kExpectedSiteCount,
          "supported catalog was not published");

    Definition site{};
    check(sites::find(test_data::kSiteIndex, site), "known site not found");
    check(site.siteIndex == test_data::kSiteIndex && site.provenance == Provenance::reconstructed,
          "known site identity differs");

    std::array<ItemProgression, test_data::kExpectedOperationCount> itemRows{};
    std::size_t itemCount{};
    check(sites::item_progressions(site, itemRows, itemCount) && itemCount == itemRows.size(),
          "item progression did not resolve");
    check(itemRows.front().sourceItemIndex == test_data::kSourceItemIndex
              && itemRows.front().sourceItemHash == test_data::kSourceItemHash
              && itemRows.front().successorItemIndex == test_data::kSuccessorItemIndex
              && itemRows.front().successorItemHash == test_data::kSuccessorItemHash,
          "item progression differs");

    std::array<CharacterObjectTransition, test_data::kExpectedOperationCount> stateRows{};
    std::size_t stateCount{};
    check(sites::character_object_transitions(site, stateRows, stateCount)
              && stateCount == stateRows.size(),
          "character object transition did not resolve");
    check(stateRows.front().rowIndex == test_data::kCharacterObjectRow
              && stateRows.front().expectedValue == test_data::kExpectedValue
              && stateRows.front().nextValue == test_data::kNextValue,
          "character object transition differs");

    test_data::validItemRows = false;
    check(!sites::item_progressions(site, itemRows, itemCount) && itemCount == 0
              && itemRows.front().sourceItemHash == 0
              && itemRows.front().sourceItemIndex == sites::kUnavailableItemIndex,
          "item identity mismatch was accepted");
    test_data::validItemRows = true;

    Definition missing = site;
    check(!sites::find(test_data::kUnknownSiteIndex, missing)
              && missing.siteIndex == sites::kUnavailableSiteIndex
              && missing.itemProgressionCount == 0 && missing.characterObjectTransitionCount == 0,
          "unknown site returned data");
    check(!sites::load(test_data::kUnsupportedBuild, schema, definitions),
          "unsupported build accepted definitions");
    check(sites::ready() && sites::count() == test_data::kExpectedSiteCount
              && sites::find(test_data::kSiteIndex, site),
          "failed load replaced the published catalog");

    std::string invalidDefinitions(definitions);
    invalidDefinitions += "\nUPDATE reward_sites SET provenance='unknown';";
    check(!sites::load(test_data::kSupportedBuild, schema, invalidDefinitions),
          "schema constraint violation was accepted");
    check(sites::ready() && sites::count() == test_data::kExpectedSiteCount
              && sites::find(test_data::kSiteIndex, site),
          "invalid definitions replaced the published catalog");

    check(sites::load(test_data::kSupportedBuild, schema, definitions)
              && sites::count() == test_data::kExpectedSiteCount,
          "repeat load changed the catalog");
    sites::clear();
    check(!sites::ready() && sites::count() == 0, "final clear left published data");
}

} // namespace

int main(int argumentCount, char** arguments) {
    check(argumentCount == test_data::kExpectedArgumentCount,
          "expected the resource database directory");
    const std::string schema =
        read_file(arguments[test_data::kResourceDirectoryArgument], "reward_site_schema.sql");
    const std::string definitions =
        read_file(arguments[test_data::kResourceDirectoryArgument], "reward_site_definitions.sql");
    verify_catalog(schema, definitions);
    std::puts("PASS: Reward Site schema, build binding, typed resolution, and atomic publication");
    return EXIT_SUCCESS;
}
