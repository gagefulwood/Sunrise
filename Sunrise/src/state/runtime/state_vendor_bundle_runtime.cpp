#include "state_vendor_bundle_runtime.h"

#include <algorithm>

#include "../build_data/runtime.h"
#include "../build_data/vendors/bundle_catalog.h"
#include "../build_data/vendors/vendor_catalog.h"
#include "../investment/store_internal.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"

namespace sunrise::state {
namespace {
namespace bundles = build_data::vendors::bundles;

/**
 * Recognize supported wrapper identities even when their extracted payout is unavailable.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Requested sale selector.
 * @param sourceHash Receives the wrapper identity.
 * @return True for a supported source; unavailable content must refuse, not fall through.
 */
bool supported_sale(std::uint16_t vendorIndex,
                    std::uint16_t saleIndex,
                    std::uint32_t& sourceHash) noexcept {
    build_data::vendors::IndexEntry vendor{};
    build_data::vendors::Definition definition{};
    build_data::vendors::SaleRow sale{};
    build_data::items::Definition item{};
    if (!build_data::vendors::find_index(vendorIndex, vendor)
        || !build_data::vendors::find(vendor.definitionHash, definition)
        || !build_data::vendors::sale_row(definition, saleIndex, sale)
        || !build_data::find_item_definition_index(sale.itemIndex, item)) {
        return false;
    }
    sourceHash = item.definitionHash;
    return std::any_of(bundles::kClaimEffects.begin(),
                       bundles::kClaimEffects.end(),
                       [=](const auto& effect) { return effect.itemHash == sourceHash; });
}

/**
 * Recheck live price and all extracted purchase gates against saved state.
 * @param rule Installed bundle definition.
 * @param characterClass Selected character class.
 * @param banks Current saved unlocks.
 * @return True only while the same free offer remains eligible.
 */
bool eligible(const bundles::Definition& rule,
              CharacterClass characterClass,
              const unlocks::Table& banks) noexcept {
    build_data::vendors::IndexEntry index{};
    build_data::vendors::Definition vendor{};
    build_data::vendors::SaleRow sale{};
    build_data::items::Definition item{};
    return (!rule.characterClass.has_value() || characterClass == *rule.characterClass)
           && banks.accountFlags[rule.claimRow] != unlocks::kFlagSet
           && std::all_of(rule.requiredRows.begin(),
                          rule.requiredRows.begin()
                              + static_cast<std::ptrdiff_t>(rule.requiredCount),
                          [&](auto row) { return banks.accountFlags[row] == unlocks::kFlagSet; })
           && build_data::vendors::find_index(rule.vendorIndex, index)
           && build_data::vendors::find(index.definitionHash, vendor)
           && build_data::vendors::sale_row(vendor, rule.saleIndex, sale) && sale.costQuantity == 0
           && build_data::find_item_definition_index(sale.itemIndex, item)
           && item.definitionHash == rule.sourceHash;
}
} // namespace

/**
 * Prepare the installed sack members without writing inventory or the account claim.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Requested sale selector.
 * @param mutation Receives the complete grant only on success.
 * @return Recognized failures remain owned by this handler, not the ordinary item path.
 */
VendorBundleDisposition prepare_vendor_bundle(std::uint16_t vendorIndex,
                                              std::uint16_t saleIndex,
                                              PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    std::uint32_t sourceHash{};
    if (!supported_sale(vendorIndex, saleIndex, sourceHash)) {
        return VendorBundleDisposition::notApplicable;
    }
    bundles::Definition rule{};
    if (!bundles::find(sourceHash, rule) || rule.vendorIndex != vendorIndex
        || rule.saleIndex != saleIndex) {
        return VendorBundleDisposition::refused;
    }
    const std::lock_guard lock(investment::store::g_mutex);
    const AccountState account = account_snapshot();
    const auto selected = runtime::detail::selected_character_index(account);
    unlocks::Table banks{};
    if (!account::valid(account) || selected >= account.characterCount
        || !investment::store::read_unlocks(banks, static_cast<int>(selected))
        || !eligible(rule, account.characters[selected].characterClass, banks)) {
        return VendorBundleDisposition::refused;
    }
    if (!prepare_record_reward_grant(std::span(rule.rewards.members).first(rule.rewards.count),
                                     kUnclaimedRecordIndex,
                                     mutation)) {
        mutation = {};
        return VendorBundleDisposition::refused;
    }
    mutation.vendorBundle = VendorBundleClaim{
        rule.sourceHash, vendorIndex, saleIndex, banks.accountFlags[rule.claimRow]};
    std::uint16_t claimRow{};
    if (!vendor_bundle_claim_row(mutation, banks, claimRow)) {
        mutation = {};
        return VendorBundleDisposition::refused;
    }
    return VendorBundleDisposition::prepared;
}

/**
 * Recheck the installed payout before preview or commit can mark its source claimed.
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
    bundles::Definition rule{};
    if (!bundles::find(claim.sourceHash, rule) || rule.vendorIndex != claim.vendorIndex
        || rule.saleIndex != claim.saleIndex || mutation.rewardCount != rule.rewards.count
        || claim.beforeClaim != banks.accountFlags[rule.claimRow]
        || !eligible(rule, mutation.beforeCharacter.characterClass, banks)) {
        return false;
    }
    for (std::size_t index = 0; index < rule.rewards.count; ++index) {
        build_data::items::Definition item{};
        const auto& reward = mutation.rewards[index];
        if (!build_data::find_item_definition_index(rule.rewards.members[index].itemDefinitionIndex,
                                                    item)
            || reward.definitionHash != item.definitionHash
            || reward.quantity != rule.rewards.members[index].quantity) {
            return false;
        }
    }
    claimRow = rule.claimRow;
    return true;
}
} // namespace sunrise::state
