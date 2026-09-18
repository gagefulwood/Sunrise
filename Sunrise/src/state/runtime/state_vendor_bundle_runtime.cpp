#include "state_vendor_bundle_runtime.h"

#include <algorithm>

#include "../build_data/runtime.h"
#include "../build_data/vendors/vendor_catalog.h"
#include "../investment/store_internal.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"

namespace sunrise::state {
namespace {

/** Solstice upgrade sacks contain one piece for each of the five armour slots. */
constexpr std::size_t kArmourPieceCount = 5;
/** Build-86657 vendor hash for Banshee-44. */
constexpr std::uint32_t kBansheeHash = 672118013U;
/** Build-86657 Solstice upgrade sales belong to Banshee's Upgrade category. */
constexpr std::int32_t kUpgradeCategory = 20;

/** Build-matched sack contents and account-bank rows; indices are not wire flag slots. */
struct BundleRule {
    std::uint32_t sourceHash;
    std::uint16_t saleIndex;
    CharacterClass characterClass;
    std::uint16_t claimRow;
    std::array<std::uint16_t, kArmourPieceCount> requiredRows;
    std::array<std::uint32_t, kArmourPieceCount> items;
};

/** Build-86657 class bindings name content and saved flag locations, not player-specific values. */
constexpr std::array<BundleRule, kCharacterCapacity> kBundles{{
    // Hunter Solstice upgrade wrapper and its Banshee sale row.
    {.sourceHash = 1493877378U,
     .saleIndex = 165,
     .characterClass = CharacterClass::hunter,
     // FLAG[10454] maps to this account-bank claim row.
     .claimRow = 6398,
     // POOL[844] reads FLAG[8451..8455] from these account-bank rows.
     .requiredRows = {5261, 5262, 5263, 5264, 5265},
     // Native sack reward list 786, in its authored order.
     .items = {1775707016U,   // Arms.
               2805101184U,   // Chest.
               2156817213U,   // Class item.
               3159052337U,   // Head.
               2877046370U}}, // Legs.
    // Titan Solstice upgrade wrapper and its Banshee sale row.
    {.sourceHash = 4036562374U,
     .saleIndex = 166,
     .characterClass = CharacterClass::titan,
     // FLAG[10455] maps to this account-bank claim row.
     .claimRow = 6399,
     // POOL[845] reads FLAG[8529..8533] from these account-bank rows.
     .requiredRows = {5317, 5318, 5319, 5320, 5321},
     // Native sack reward list 787, in its authored order.
     .items = {2291082292U,   // Arms.
               1288683596U,   // Chest.
               3987442049U,   // Class item.
               1510405477U,   // Head.
               2578820926U}}, // Legs.
    // Warlock Solstice upgrade wrapper and its Banshee sale row.
    {.sourceHash = 2370303981U,
     .saleIndex = 167,
     .characterClass = CharacterClass::warlock,
     // FLAG[10456] maps to this account-bank claim row.
     .claimRow = 6400,
     // POOL[846] reads FLAG[8607..8611] from these account-bank rows.
     .requiredRows = {5373, 5374, 5375, 5376, 5377},
     // Native sack reward list 788, in its authored order.
     .items = {2127474099U,   // Arms.
               450844637U,    // Chest.
               2337290000U,   // Class item.
               2546370410U,   // Head.
               1862324869U}}, // Legs.
}};

/**
 * Recognizes a supported sale before checking whether its installed contents still match.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Requested sale selector.
 * @return Binding for a supported sale, or null for an unrelated request.
 */
const BundleRule* find_rule(std::uint16_t vendorIndex, std::uint16_t saleIndex) noexcept {
    build_data::vendors::IndexEntry vendor{};
    if (!build_data::vendors::find_index(vendorIndex, vendor)
        || vendor.definitionHash != kBansheeHash) {
        return nullptr;
    }
    const auto found = std::find_if(kBundles.begin(), kBundles.end(), [&](const auto& rule) {
        return rule.saleIndex == saleIndex;
    });
    return found == kBundles.end() ? nullptr : &*found;
}

/**
 * Only the matching free wrapper and its saved purchase gates admit an upgrade.
 * @param rule Checked build binding.
 * @param characterClass Selected character class.
 * @param banks Current saved unlocks.
 * @return True when the wrapper, class and every required account flag match.
 */
bool eligible(const BundleRule& rule,
              CharacterClass characterClass,
              const unlocks::Table& banks) noexcept {
    build_data::vendors::Definition vendor{};
    build_data::vendors::SaleRow sale{};
    build_data::items::Definition item{};
    return characterClass == rule.characterClass
           && banks.accountFlags[rule.claimRow] != unlocks::kFlagSet
           && std::all_of(rule.requiredRows.begin(),
                          rule.requiredRows.end(),
                          [&](auto row) { return banks.accountFlags[row] == unlocks::kFlagSet; })
           && build_data::vendors::find(kBansheeHash, vendor)
           && build_data::vendors::sale_row(vendor, rule.saleIndex, sale)
           && sale.categoryIndex == kUpgradeCategory && sale.costQuantity == 0
           && build_data::find_item_definition_index(sale.itemIndex, item)
           && item.definitionHash == rule.sourceHash;
}

} // namespace

/**
 * Prepare all five pieces without writing either inventory or the account claim.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Requested sale selector.
 * @param mutation Receives the complete grant only on success.
 * @return Recognized failures remain owned by this handler, not the ordinary item path.
 */
VendorBundleDisposition prepare_vendor_bundle(std::uint16_t vendorIndex,
                                              std::uint16_t saleIndex,
                                              PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    const auto* rule = find_rule(vendorIndex, saleIndex);
    if (rule == nullptr) {
        return VendorBundleDisposition::notApplicable;
    }
    const std::lock_guard lock(investment::store::g_mutex);
    const AccountState account = account_snapshot();
    const auto selected = runtime::detail::selected_character_index(account);
    unlocks::Table banks{};
    if (!account::valid(account) || selected >= account.characterCount
        || !investment::store::read_unlocks(banks, static_cast<int>(selected))
        || !eligible(*rule, account.characters[selected].characterClass, banks)) {
        return VendorBundleDisposition::refused;
    }
    std::array<DirectRecordReward, kArmourPieceCount> rewards{};
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        build_data::items::Definition item{};
        if (!build_data::find_item_definition_hash(rule->items[index], item)) {
            return VendorBundleDisposition::refused;
        }
        rewards[index] = {item.definitionIndex, 1};
    }
    if (!prepare_record_reward_grant(rewards, kUnclaimedRecordIndex, mutation)) {
        mutation = {};
        return VendorBundleDisposition::refused;
    }
    mutation.vendorBundle = VendorBundleClaim{
        rule->sourceHash, vendorIndex, saleIndex, banks.accountFlags[rule->claimRow]};
    std::uint16_t claimRow{};
    if (!vendor_bundle_claim_row(mutation, banks, claimRow)) {
        mutation = {};
        return VendorBundleDisposition::refused;
    }
    return VendorBundleDisposition::prepared;
}

/**
 * Recheck the exact five-piece grant before preview or commit can mark its source claimed.
 * @param mutation Prepared rewards and source sale.
 * @param banks Current saved unlocks.
 * @param claimRow Receives the account claim row only on success.
 * @return False when any purchase gate or grant row no longer matches.
 */
bool vendor_bundle_claim_row(const PendingRecordRewardGrant& mutation,
                             const unlocks::Table& banks,
                             std::uint16_t& claimRow) noexcept {
    if (!mutation.prepared || !mutation.vendorBundle.has_value()
        || mutation.claimedRecordIndex != kUnclaimedRecordIndex) {
        return false;
    }
    const auto& claim = *mutation.vendorBundle;
    const auto* rule = find_rule(claim.vendorIndex, claim.saleIndex);
    if (rule == nullptr || claim.sourceHash != rule->sourceHash
        || mutation.rewardCount != rule->items.size()
        || claim.beforeClaim != banks.accountFlags[rule->claimRow]
        || !eligible(*rule, mutation.beforeCharacter.characterClass, banks)) {
        return false;
    }
    for (std::size_t index = 0; index < rule->items.size(); ++index) {
        const auto& reward = mutation.rewards[index];
        if (reward.definitionHash != rule->items[index] || reward.quantity != 1
            || reward.kind != RecordRewardKind::characterInstance) {
            return false;
        }
    }
    claimRow = rule->claimRow;
    return true;
}

} // namespace sunrise::state
