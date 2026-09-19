#include "reward_site_catalog.h"

#include <array>

namespace sunrise::state::build_data::reward_sites {
namespace {

namespace quest = items;

/** Installed build 86657 uses these three Reward Site rows for non-final Unlimited Power stages. */
constexpr std::uint16_t kReachPower900Site = 11481;
constexpr std::uint16_t kGearUpSite = 11484;
constexpr std::uint16_t kReachPower910Site = 11487;

/** Installed item-table rows for the four ordered Unlimited Power quest members. */
constexpr std::uint16_t kReachPower900Item = 15284;
constexpr std::uint16_t kGearUpItem = 15285;
constexpr std::uint16_t kReachPower910Item = 15286;
constexpr std::uint16_t kSpeakToCryptarchItem = 15287;

/** Definition hashes pair each build-specific item row with its authored item identity. */
constexpr std::uint32_t kReachPower900Hash = 3398477426U;
constexpr std::uint32_t kGearUpHash = 4280995080U;
constexpr std::uint32_t kReachPower910Hash = 3398477425U;
constexpr std::uint32_t kSpeakToCryptarchHash = 4197518657U;

/** The quest-set slot maps to selected-character object-value row 526 in build 86657. */
constexpr std::uint16_t kUnlimitedPowerValueRow = 526;
/** Quest-set members use authored identifiers 100, 200, 300 and 400. */
constexpr std::int32_t kReachPower900Value = 100;
constexpr std::int32_t kGearUpValue = 200;
constexpr std::int32_t kReachPower910Value = 300;
constexpr std::int32_t kSpeakToCryptarchValue = 400;

/** The first comparison uses 899 even though its localized objective says Power 900. */
constexpr std::int32_t kReachPower900Minimum = 899;
/** The third member's authored comparison requires Power 910. */
constexpr std::int32_t kReachPower910Minimum = 910;
/** Gear Up reads character-owned challenge and Prime-decryption counters. */
constexpr std::uint16_t kChallengeCounterSlot = 13080;
constexpr std::uint16_t kPrimeDecryptionCounterSlot = 13081;
/** Gear Up completes after three challenges and two Prime Engram decryptions. */
constexpr std::int32_t kChallengeMinimum = 3;
constexpr std::int32_t kPrimeDecryptionMinimum = 2;

/** Builds one row with a single selected-character Power requirement. */
[[nodiscard]] constexpr Definition power_site(std::uint16_t site,
                                              std::uint16_t sourceItem,
                                              std::uint16_t successorItem,
                                              std::uint32_t sourceHash,
                                              std::uint32_t successorHash,
                                              std::int32_t currentValue,
                                              std::int32_t nextValue,
                                              std::int32_t minimumPower) noexcept {
    Definition result{};
    result.definitionIndex = site;
    result.sourceItemHash = sourceHash;
    result.successorItemHash = successorHash;
    result.transition.sourceItemIndex = sourceItem;
    result.transition.successorItemIndex = successorItem;
    result.transition.currentValue = currentValue;
    result.transition.nextValue = nextValue;
    result.transition.valueRow = kUnlimitedPowerValueRow;
    result.transition.objectives[0] = {quest::kQuestCharacterPowerSlot,
                                       minimumPower,
                                       quest::QuestPredicate::Input::characterPower};
    result.transition.objectiveCount = 1;
    result.transition.completionEffect = site;
    return result;
}

/** Builds the Gear Up row with both character-owned counters required. */
[[nodiscard]] constexpr Definition gear_up_site() noexcept {
    Definition result{};
    result.definitionIndex = kGearUpSite;
    result.sourceItemHash = kGearUpHash;
    result.successorItemHash = kReachPower910Hash;
    result.transition.sourceItemIndex = kGearUpItem;
    result.transition.successorItemIndex = kReachPower910Item;
    result.transition.currentValue = kGearUpValue;
    result.transition.nextValue = kReachPower910Value;
    result.transition.valueRow = kUnlimitedPowerValueRow;
    result.transition.objectives[0] = {
        kChallengeCounterSlot, kChallengeMinimum, quest::QuestPredicate::Input::characterCounter};
    result.transition.objectives[1] = {kPrimeDecryptionCounterSlot,
                                       kPrimeDecryptionMinimum,
                                       quest::QuestPredicate::Input::characterCounter};
    result.transition.objectiveCount = 2;
    result.transition.completionEffect = kGearUpSite;
    return result;
}

/**
 * These rows reconstruct only effects supported by retained metadata and verified State paths.
 * The final 11490 row is deliberately absent because its challenge unlocks remain unidentified.
 */
constexpr std::array kDefinitions{
    power_site(kReachPower900Site,
               kReachPower900Item,
               kGearUpItem,
               kReachPower900Hash,
               kGearUpHash,
               kReachPower900Value,
               kGearUpValue,
               kReachPower900Minimum),
    gear_up_site(),
    power_site(kReachPower910Site,
               kReachPower910Item,
               kSpeakToCryptarchItem,
               kReachPower910Hash,
               kSpeakToCryptarchHash,
               kReachPower910Value,
               kSpeakToCryptarchValue,
               kReachPower910Minimum),
};

static_assert([] {
    for (std::size_t index = 0; index < kDefinitions.size(); ++index) {
        if (!valid(kDefinitions[index])) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (kDefinitions[prior].definitionIndex == kDefinitions[index].definitionIndex
                || kDefinitions[prior].sourceItemHash == kDefinitions[index].sourceItemHash) {
                return false;
            }
        }
    }
    return true;
}());

} // namespace

/** Finds one explicitly reconstructed native row. */
bool find(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    for (const Definition& candidate : kDefinitions) {
        if (candidate.definitionIndex == definitionIndex) {
            definition = candidate;
            return true;
        }
    }
    return false;
}

/** Finds one explicitly reconstructed source-item binding. */
bool find_source(std::uint32_t sourceItemHash, Definition& definition) noexcept {
    definition = {};
    for (const Definition& candidate : kDefinitions) {
        if (candidate.sourceItemHash == sourceItemHash) {
            definition = candidate;
            return true;
        }
    }
    return false;
}

} // namespace sunrise::state::build_data::reward_sites
