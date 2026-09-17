/**
 * Grants that no purchase pays for: season pass rewards, record rewards, and the
 * default emote collection.
 */
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../build_data/runtime.h"
#include "../build_data/vendors/vendor_catalog.h"
#include "../investment/store_internal.h"
#include "../progression/season_pass_reward_catalog.h"
#include "../unlocks/unlocks_records.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"

namespace sunrise::state {

using namespace runtime::detail;
namespace authored_inventory = account::inventory;
namespace item_details = build_data::items::details;
namespace inventory_buckets = build_data::inventory::buckets;
namespace family4_loadout = middleware::datagen::family4::loadout;

namespace {

/** Build-86657 emote plugs use native inventory bucket 41, without equipment instances. */
constexpr std::uint8_t kEmoteBucket = 41;
/** The installed emote plug category must match before a reward can unlock ownership. */
constexpr std::uint32_t kEmotePlugCategory = 3054419239U;

struct EmoteOwnership {
    std::uint32_t definitionHash;
    std::uint16_t accountRow;
};

/** Only these build-86657 ownership predicates have supported reward bindings. */
constexpr std::array kEmoteOwnership{
    // Coin Flip reads FLAG[7372], mapped to acquired-flags row 4450.
    EmoteOwnership{801733632U, 4450},
    // Knife Trick reads FLAG[7373], mapped to acquired-flags row 4451.
    EmoteOwnership{3921851413U, 4451},
};

/** Build-86657 Zavala identity and Gratitude Package sale in Special Orders. */
constexpr std::uint32_t kZavalaHash = 69482069U;
constexpr std::uint16_t kGratitudeSale = 106;
constexpr std::int32_t kSpecialOrdersCategory = 17;
/** FLAG[7243] uses profile row 217; NOT FLAG[7618] uses account row 4634. */
constexpr std::uint16_t kGratitudeEligibilityRow = 217;
constexpr std::uint16_t kGratitudeClaimRow = 4634;

/** Reward identities do not define the shader or consumable payout quantities. */
constexpr std::array kGratitudeRewardHashes{
    1923236933U, // Veteran of the Hunt emblem.
    801733632U,  // Coin Flip emote.
    3921851413U, // Knife Trick emote.
    690228054U,  // Shrouded Stripes shader.
    1909657913U, // Fireteam Medallion.
    2916406440U, // Boon of the Vanguard.
    3196288028U, // Boon of the Crucible.
    2891979647U, // Finest Matterweave.
};
/** Provisional server policy: one of each reward; retail stack quantities remain unresolved. */
constexpr std::int32_t kGratitudeRewardQuantity = 1;

/**
 * Rechecks the installed free sale and both account-scoped purchase predicates.
 * @param vendorIndex Installed vendor selector.
 * @return True only while the gift is eligible and unclaimed.
 */
bool gratitude_eligible(std::uint16_t vendorIndex) noexcept {
    build_data::vendors::IndexEntry entry{};
    build_data::vendors::Definition vendor{};
    build_data::vendors::SaleRow sale{};
    build_data::items::Definition item{};
    std::int32_t eligible{}, claimed{};
    return build_data::vendors::find_index(vendorIndex, entry)
           && entry.definitionHash == kZavalaHash && build_data::vendors::find(kZavalaHash, vendor)
           && build_data::vendors::sale_row(vendor, kGratitudeSale, sale)
           && sale.categoryIndex == kSpecialOrdersCategory && sale.costQuantity == 0
           && sale.costItemIndex == build_data::vendors::kAbsentCostItem
           && build_data::find_item_definition_index(sale.itemIndex, item)
           && item.definitionHash == kGratitudePackageHash
           && investment::store::read_unlock(
               investment::store::Bank::profileFlags, kGratitudeEligibilityRow, eligible)
           && investment::store::read_unlock(
               investment::store::Bank::accountFlags, kGratitudeClaimRow, claimed)
           && eligible == unlocks::kFlagSet && claimed == unlocks::kFlagClear;
}

/**
 * A gift claim must carry each supported reward once, without an unrelated record claim.
 * @param mutation Prepared reward batch under the investment lock.
 * @return False for a stale claim or an incomplete, repeated or unrelated reward set.
 */
bool gratitude_matches(const PendingRecordRewardGrant& mutation) noexcept {
    if (!mutation.gratitudeVendorIndex.has_value()
        || mutation.claimedRecordIndex != kUnclaimedRecordIndex
        || mutation.rewardCount != kGratitudeRewardHashes.size()
        || !gratitude_eligible(*mutation.gratitudeVendorIndex)) {
        return false;
    }
    for (const auto hash : kGratitudeRewardHashes) {
        std::size_t count{};
        for (std::size_t index = 0; index < mutation.rewardCount; ++index) {
            count += mutation.rewards[index].definitionHash == hash;
        }
        if (count != 1) {
            return false;
        }
    }
    return true;
}

/** @param hash Installed emote hash. @return Its supported ownership binding, or null. */
const EmoteOwnership* emote_ownership(std::uint32_t hash) noexcept {
    for (const auto& binding : kEmoteOwnership) {
        if (binding.definitionHash == hash) {
            return &binding;
        }
    }
    return nullptr;
}

/**
 * Writes permanent ownership and the gift claim inside the caller's transaction.
 * @param mutation Validated batch under the investment transaction lock.
 * @return False when any write fails; the caller must roll back the whole transaction.
 */
bool write_reward_unlocks(const PendingRecordRewardGrant& mutation) noexcept {
    for (std::size_t index = 0; index < mutation.rewardCount; ++index) {
        const auto& reward = mutation.rewards[index];
        if (reward.kind == RecordRewardKind::accountUnlock
            && !investment::store::write_unlock(investment::store::Bank::accountFlags,
                                                static_cast<std::uint16_t>(reward.stateIndex),
                                                unlocks::kFlagSet)) {
            return false;
        }
    }
    return !mutation.gratitudeVendorIndex.has_value()
           || investment::store::write_unlock(
               investment::store::Bank::accountFlags, kGratitudeClaimRow, unlocks::kFlagSet);
}

[[nodiscard]] bool materialize_record_reward(const AccountState& current,
                                             const PendingRecordRewardGrant& mutation,
                                             AccountState& after) noexcept;

/**
 * Checks that a staged grant is the one the authored reward row describes.
 * @param reward Authored season pass reward row.
 * @return False when the grant's item, quantity or bundle does not match the row.
 */
[[nodiscard]] bool reward_matches(const build_data::season_pass::Reward& reward,
                                  const PendingSeasonPassReward& mutation) noexcept {
    if (!mutation.prepared || mutation.sourceDefinitionHash != reward.itemHash) {
        return false;
    }
    if (const auto* item = std::get_if<PendingItemAcquisition>(&mutation.grant)) {
        if (reward.quantity != 1) {
            return false;
        }
        if (reward.itemHash != progression::season_pass::kLegendaryEngramHash
            && reward.itemHash != progression::season_pass::kExoticEngramHash) {
            return item->acquiredDefinitionHash == reward.itemHash;
        }
        return progression::season_pass::contains_engram_reward(
            reward.itemHash,
            item->acquiredDefinitionHash,
            static_cast<std::uint8_t>(item->afterCharacter.characterClass));
    }
    if (const auto* profile = std::get_if<PendingProfileItemAcquisition>(&mutation.grant)) {
        return profile->acquiredDefinitionHash == reward.itemHash
               && profile->acquiredQuantity - profile->previousQuantity
                      == static_cast<std::int32_t>(reward.quantity);
    }
    if (const auto* bundle = std::get_if<PendingDirectItemBundle>(&mutation.grant)) {
        build_data::season_pass::Package package{};
        return reward.quantity == 1 && bundle->sourceDefinitionHash == reward.itemHash
               && build_data::find_season_pass_package(reward.itemHash, package);
    }
    const auto* resources = std::get_if<PendingRecordRewardGrant>(&mutation.grant);
    if (resources == nullptr || reward.quantity != 1
        || reward.itemHash != progression::season_pass::kDestinationResourceBundleHash
        || resources->rewardCount != progression::season_pass::kDestinationResourceHashes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < resources->rewardCount; ++index) {
        if (resources->rewards[index].definitionHash
                != progression::season_pass::kDestinationResourceHashes[index]
            || resources->rewards[index].quantity
                   != progression::season_pass::kDestinationResourceQuantity) {
            return false;
        }
    }
    return true;
}

} // namespace

/** Commits a reward and its claim together after every outbound byte has been staged. */
bool commit_season_pass_reward(PendingSeasonPassReward& mutation) noexcept {
    const PendingConsumption consume{mutation};
    build_data::season_pass::Reward reward{};
    if (!build_data::find_season_pass_reward(mutation.rewardIndex, reward)
        || !reward_matches(reward, mutation)) {
        revoke_season_pass_reward(mutation.rewardIndex);
        return false;
    }

    investment::store::g_mutex.lock();
    AccountState after{};
    bool ready = false;
    if (const auto* item = std::get_if<PendingItemAcquisition>(&mutation.grant)) {
        ready = materialize_item_acquisition(investment::store::account(), *item, after);
    } else if (const auto* profile = std::get_if<PendingProfileItemAcquisition>(&mutation.grant)) {
        ready = materialize_profile_acquisition(investment::store::account(), *profile, after);
    } else if (const auto* bundle = std::get_if<PendingDirectItemBundle>(&mutation.grant)) {
        ready = materialize_direct_item_bundle(investment::store::account(), *bundle, after);
    } else if (const auto* resources = std::get_if<PendingRecordRewardGrant>(&mutation.grant)) {
        ready = materialize_record_reward(investment::store::account(), *resources, after);
    }
    if (ready) {
        ready = investment::store::write_account(after);
    }
    investment::store::g_mutex.unlock();
    if (!ready) {
        // The claim was written when the reward was prepared, so a refused install undoes it.
        revoke_season_pass_reward(mutation.rewardIndex);
    }
    return ready;
}

namespace {

/**
 * Rebuilds the account after-image for one prepared reward set and rejects any drift.
 * @param current Live account the mutation was prepared against.
 * @param after Receives the after-image; its content is unusable on failure.
 * @return False when the account moved or a reward row breaks its own item rules.
 */
[[nodiscard]] bool materialize_record_reward(const AccountState& current,
                                             const PendingRecordRewardGrant& mutation,
                                             AccountState& after) noexcept {
    if (!mutation.prepared || mutation.rewardCount == 0
        || mutation.rewardCount > mutation.rewards.size() || mutation.accountSoid == 0
        || mutation.characterSoid == 0 || mutation.characterIndex >= current.characterCount
        || current.primarySoid != mutation.accountSoid
        || !same_character(current.characters[mutation.characterIndex], mutation.beforeCharacter)
        || !same_profile_inventory(
            current, mutation.beforeProfileItems, mutation.beforeProfileItemCount)
        || !mutation.beforeCharacter.selected
        || mutation.beforeCharacter.soid != mutation.characterSoid) {
        return false;
    }
    if (mutation.gratitudeVendorIndex.has_value() && !gratitude_matches(mutation)) {
        return false;
    }

    after = current;
    if (mutation.vendorBundle.has_value()) {
        unlocks::Table banks{};
        std::uint16_t claimRow{};
        if (!investment::store::read_unlocks(banks, static_cast<int>(mutation.characterIndex))
            || !vendor_bundle_claim_row(mutation, banks, claimRow)) {
            return false;
        }
    }
    after.characters[mutation.characterIndex] = mutation.afterCharacter;
    after.profileItems = mutation.afterProfileItems;
    after.profileItemCount = mutation.afterProfileItemCount;
    family4_loadout::ResolvedLoadout loadout{};
    if (!account::valid(after) || !valid_profile_inventory(after)
        || !family4_loadout::resolve(after, mutation.characterIndex, loadout)) {
        return false;
    }

    for (std::size_t index = 0; index < mutation.rewardCount; ++index) {
        const PreparedRecordReward& reward = mutation.rewards[index];
        build_data::items::Definition item{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (reward.definitionHash == authored_inventory::kNoDefinitionHash || reward.quantity <= 0
            || reward.afterQuantity < reward.quantity || reward.mutationSerial < 0
            || !build_data::find_item_definition_hash(reward.definitionHash, item)
            || !build_data::find_configured_item_detail(item.definitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)) {
            return false;
        }
        if (reward.kind == RecordRewardKind::accountUnlock) {
            const auto* binding = emote_ownership(reward.definitionHash);
            std::int32_t saved{};
            if (binding == nullptr || item.bucketId != kEmoteBucket
                || item.plugCategoryHash != kEmotePlugCategory || detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::instanced
                || reward.stateIndex != binding->accountRow || reward.quantity != 1
                || reward.afterQuantity != 1 || reward.instanceSoid != 0
                || reward.mutationSerial != 0 || reward.inventoryRow != 0
                || reward.appendedProfileResident
                || !investment::store::read_unlock(
                    investment::store::Bank::accountFlags, binding->accountRow, saved)
                || (saved != unlocks::kFlagClear && saved != unlocks::kFlagSet)
                || saved != reward.previousUnlock) {
                return false;
            }
        } else if (reward.kind == RecordRewardKind::characterInstance) {
            if (reward.quantity != 1 || reward.afterQuantity != 1 || reward.instanceSoid == 0
                || reward.appendedProfileResident || !detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::instanced
                || bucket.arraySelector != inventory_buckets::ArraySelector::character
                || reward.stateIndex >= mutation.afterCharacter.inventory.count) {
                return false;
            }
            const auto& granted = mutation.afterCharacter.inventory.values[reward.stateIndex];
            std::uint16_t row = 0;
            std::uint8_t slot = 0;
            if (granted.instanceSoid != reward.instanceSoid
                || granted.definitionHash != reward.definitionHash || granted.quantity != 1
                || granted.mutationSerial != reward.mutationSerial
                || !find_unequipped_row(loadout, reward.instanceSoid, row, slot)
                || row != reward.inventoryRow) {
                return false;
            }
        } else if (reward.kind == RecordRewardKind::characterStack) {
            if (reward.instanceSoid != 0 || reward.appendedProfileResident
                || detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::stackable
                || bucket.arraySelector != inventory_buckets::ArraySelector::character
                || reward.afterQuantity > detail.maxStackSize
                || reward.stateIndex >= mutation.afterCharacter.stacks.count) {
                return false;
            }
            const auto& granted = mutation.afterCharacter.stacks.values[reward.stateIndex];
            if (granted.definitionHash != reward.definitionHash
                || granted.quantity != reward.afterQuantity
                || granted.mutationSerial != reward.mutationSerial) {
                return false;
            }
        } else {
            if (reward.kind != RecordRewardKind::profileStack
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::stackable
                || bucket.arraySelector != inventory_buckets::ArraySelector::profile
                || reward.afterQuantity > detail.maxStackSize
                || reward.stateIndex >= mutation.afterProfileItemCount) {
                return false;
            }
            const auto& granted = mutation.afterProfileItems[reward.stateIndex];
            const bool actionSource =
                build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
            if (granted.instanceSoid != reward.instanceSoid
                || granted.definitionHash != reward.definitionHash
                || granted.quantity != reward.afterQuantity
                || granted.mutationSerial != reward.mutationSerial
                || actionSource != (reward.instanceSoid != 0)
                || reward.appendedProfileResident
                       != (actionSource && reward.stateIndex >= mutation.beforeProfileItemCount)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

/**
 * Prepares every reward over one locked account and ownership snapshot.
 * @param rewards Installed item selectors and quantities to grant.
 * @param claimedRecordIndex Preclaimed record, or kUnclaimedRecordIndex for direct rewards.
 * @param mutation Receives the batch; use only when preparation succeeds.
 * @return False when any item, ownership encoding or capacity is unsupported.
 */
bool prepare_record_reward_grant(std::span<const DirectRecordReward> rewards,
                                 std::uint16_t claimedRecordIndex,
                                 PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    if (rewards.empty() || rewards.size() > mutation.rewards.size()) {
        return false;
    }
    const std::lock_guard lock(investment::store::g_mutex);
    const AccountState account = account_snapshot();
    const std::size_t characterIndex = selected_character_index(account);
    if (!account::valid(account) || !valid_profile_inventory(account)
        || characterIndex >= account.characterCount) {
        return false;
    }

    AccountState working = account;
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        const DirectRecordReward& requested = rewards[index];
        build_data::items::Definition item{};
        item_details::Definition detail{};
        inventory_buckets::Descriptor bucket{};
        if (requested.quantity <= 0
            || !build_data::find_item_definition_index(requested.itemDefinitionIndex, item)
            || !build_data::find_configured_item_detail(requested.itemDefinitionIndex, detail)
            || detail.definitionIndex != item.definitionIndex
            || detail.definitionHash != item.definitionHash || detail.bucketId != item.bucketId
            || !build_data::find_inventory_bucket_descriptor(item.bucketId, bucket)) {
            return false;
        }
        if (detail.instancedDefinitionState == item_details::InstancedDefinitionState::stackable
            || emote_ownership(item.definitionHash) != nullptr) {
            for (std::size_t prior = 0; prior < index; ++prior) {
                if (mutation.rewards[prior].definitionHash == item.definitionHash) {
                    return false;
                }
            }
        }

        PreparedRecordReward prepared{};
        prepared.definitionHash = item.definitionHash;
        prepared.quantity = requested.quantity;
        if (const auto* binding = emote_ownership(item.definitionHash)) {
            std::int32_t saved{};
            if (requested.quantity != 1 || item.bucketId != kEmoteBucket
                || item.plugCategoryHash != kEmotePlugCategory || detail.equipmentSlot.has_value()
                || detail.instancedDefinitionState
                       != item_details::InstancedDefinitionState::instanced
                || !investment::store::read_unlock(
                    investment::store::Bank::accountFlags, binding->accountRow, saved)
                || (saved != unlocks::kFlagClear && saved != unlocks::kFlagSet)) {
                return false;
            }
            prepared.stateIndex = binding->accountRow;
            prepared.previousUnlock = static_cast<std::uint8_t>(saved);
            prepared.afterQuantity = 1;
            prepared.kind = RecordRewardKind::accountUnlock;
        } else if (bucket.arraySelector == inventory_buckets::ArraySelector::profile) {
            if (detail.instancedDefinitionState
                != item_details::InstancedDefinitionState::stackable) {
                return false;
            }
            PendingProfileItemAcquisition staged{};
            const bool actionSource =
                build_data::is_profile_action_source(item.definitionIndex, item.bucketId);
            if (!finalize_profile_item_acquisition(working,
                                                   working,
                                                   item.definitionHash,
                                                   detail,
                                                   actionSource,
                                                   requested.quantity,
                                                   {.direct = true},
                                                   staged)) {
                return false;
            }
            working.profileItems = staged.afterItems;
            working.profileItemCount = staged.afterItemCount;
            prepared.instanceSoid = staged.acquiredInstanceSoid;
            prepared.stateIndex = staged.profileIndex;
            prepared.afterQuantity = staged.acquiredQuantity;
            prepared.mutationSerial = staged.acquiredMutationSerial;
            prepared.kind = RecordRewardKind::profileStack;
            prepared.appendedProfileResident = staged.appended && staged.actionSource;
        } else if (bucket.arraySelector == inventory_buckets::ArraySelector::character
                   && detail.instancedDefinitionState
                          == item_details::InstancedDefinitionState::instanced) {
            if (requested.quantity != 1 || !detail.equipmentSlot.has_value()) {
                return false;
            }
            PendingItemAcquisition staged{};
            if (!finalize_item_acquisition(
                    working, working, item.definitionHash, false, {.direct = true}, staged)) {
                return false;
            }
            working.characters[characterIndex] = staged.afterCharacter;
            prepared.instanceSoid = staged.acquiredInstanceSoid;
            prepared.stateIndex = staged.inventoryIndex;
            prepared.afterQuantity = 1;
            prepared.mutationSerial =
                staged.afterCharacter.inventory.values[staged.inventoryIndex].mutationSerial;
            prepared.inventoryRow = staged.inventoryRow;
            prepared.kind = RecordRewardKind::characterInstance;
        } else if (bucket.arraySelector == inventory_buckets::ArraySelector::character
                   && detail.instancedDefinitionState
                          == item_details::InstancedDefinitionState::stackable
                   && !detail.equipmentSlot.has_value()) {
            CharacterState& character = working.characters[characterIndex];
            if (requested.quantity > detail.maxStackSize
                || character.nextInventorySerial
                       >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
                return false;
            }
            std::size_t stackIndex = character.stacks.count;
            for (std::size_t candidate = 0; candidate < character.stacks.count; ++candidate) {
                if (character.stacks.values[candidate].definitionHash == item.definitionHash) {
                    stackIndex = candidate;
                    break;
                }
            }
            const bool appended = stackIndex == character.stacks.count;
            if ((appended && stackIndex >= character.stacks.values.size())
                || (!appended
                    && character.stacks.values[stackIndex].quantity
                           > detail.maxStackSize - requested.quantity)) {
                return false;
            }
            auto& stack = character.stacks.values[stackIndex];
            if (appended) {
                stack.definitionHash = item.definitionHash;
                ++character.stacks.count;
            }
            stack.quantity += requested.quantity;
            stack.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
            prepared.stateIndex = stackIndex;
            prepared.afterQuantity = stack.quantity;
            prepared.mutationSerial = stack.mutationSerial;
            prepared.kind = RecordRewardKind::characterStack;
        } else {
            return false;
        }
        mutation.rewards[index] = prepared;
    }

    family4_loadout::ResolvedLoadout loadout{};
    if (!account::valid(working) || !valid_profile_inventory(working)
        || !family4_loadout::resolve(working, characterIndex, loadout)) {
        return false;
    }
    mutation.beforeCharacter = account.characters[characterIndex];
    mutation.afterCharacter = working.characters[characterIndex];
    mutation.beforeProfileItems = account.profileItems;
    mutation.afterProfileItems = working.profileItems;
    mutation.claimedRecordIndex = claimedRecordIndex;
    mutation.accountSoid = account.primarySoid;
    mutation.characterSoid = account.characters[characterIndex].soid;
    mutation.characterIndex = characterIndex;
    mutation.beforeProfileItemCount = account.profileItemCount;
    mutation.afterProfileItemCount = working.profileItemCount;
    mutation.rewardCount = rewards.size();
    mutation.prepared = true;
    return true;
}

/**
 * Resolves the provisional payout through installed content before preparing any claim.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Gift sale selector.
 * @param mutation Receives the grant, or a cleared value on failure.
 * @return False when any reward is unavailable or the gift transaction is refused.
 */
bool prepare_gratitude_package(std::uint16_t vendorIndex,
                               std::uint16_t saleIndex,
                               PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    std::array<DirectRecordReward, kGratitudeRewardHashes.size()> rewards{};
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        build_data::items::Definition item{};
        if (!build_data::find_item_definition_hash(kGratitudeRewardHashes[index], item)) {
            return false;
        }
        rewards[index] = {item.definitionIndex, kGratitudeRewardQuantity};
    }
    return prepare_gratitude_package(vendorIndex, saleIndex, rewards, mutation);
}

/**
 * Binds a complete server payout to the gift's still-unclaimed account state.
 * @param vendorIndex Installed vendor selector.
 * @param saleIndex Gift sale selector.
 * @param rewards Explicit server-owned payout; stack quantities must be supplied by its policy.
 * @param mutation Receives the atomic grant, or a cleared value on failure.
 * @return True only when the full payout and claim can be prepared without writes.
 */
bool prepare_gratitude_package(std::uint16_t vendorIndex,
                               std::uint16_t saleIndex,
                               std::span<const DirectRecordReward> rewards,
                               PendingRecordRewardGrant& mutation) noexcept {
    mutation = {};
    const std::lock_guard lock(investment::store::g_mutex);
    if (saleIndex != kGratitudeSale || rewards.size() != kGratitudeRewardHashes.size()
        || !gratitude_eligible(vendorIndex)
        || !prepare_record_reward_grant(rewards, kUnclaimedRecordIndex, mutation)) {
        mutation = {};
        return false;
    }
    mutation.gratitudeVendorIndex = vendorIndex;
    if (!gratitude_matches(mutation)) {
        mutation = {};
        return false;
    }
    return true;
}

bool preview_record_reward_grant(const PendingRecordRewardGrant& mutation,
                                 AccountState& after) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    after = {};
    return materialize_record_reward(account_snapshot(), mutation, after);
}

/**
 * Preview inventory, reward ownership and account claims in the same publication.
 * @param mutation Prepared grant checked against saved state.
 * @param after Receives the candidate account; use only on success.
 * @param afterUnlocks Receives unlocks with pending ownership and account claims applied.
 * @return False for stale state, failed reads or a changed bundle binding.
 */
bool preview_record_reward_grant(const PendingRecordRewardGrant& mutation,
                                 AccountState& after,
                                 unlocks::Table& afterUnlocks) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    after = {};
    afterUnlocks = {};
    if (!materialize_record_reward(account_snapshot(), mutation, after)
        || !investment::store::read_unlocks(afterUnlocks,
                                            static_cast<int>(mutation.characterIndex))) {
        return false;
    }
    if (mutation.vendorBundle.has_value()) {
        std::uint16_t claimRow{};
        if (!vendor_bundle_claim_row(mutation, afterUnlocks, claimRow)) {
            return false;
        }
        afterUnlocks.accountFlags[claimRow] = unlocks::kFlagSet;
    }
    for (std::size_t index = 0; index < mutation.rewardCount; ++index) {
        const auto& reward = mutation.rewards[index];
        if (reward.kind == RecordRewardKind::accountUnlock) {
            afterUnlocks.accountFlags[reward.stateIndex] = unlocks::kFlagSet;
        }
    }
    if (mutation.gratitudeVendorIndex.has_value()) {
        afterUnlocks.accountFlags[kGratitudeClaimRow] = unlocks::kFlagSet;
    }
    return true;
}

/**
 * Inventory, permanent ownership and account claims share one rollback boundary.
 * @param mutation Prepared grant consumed on success or refusal.
 * @return False when the grant is stale or any write fails.
 */
bool commit_record_reward(PendingRecordRewardGrant& mutation) noexcept {
    const PendingConsumption consume{mutation};
    bool ready = false;
    {
        investment::store::Transaction transaction;
        AccountState after{};
        unlocks::Table banks{};
        std::uint16_t claimRow{};
        ready = transaction.ready()
                && materialize_record_reward(investment::store::account(), mutation, after);
        if (ready && mutation.vendorBundle.has_value()) {
            ready =
                investment::store::read_unlocks(banks, static_cast<int>(mutation.characterIndex))
                && vendor_bundle_claim_row(mutation, banks, claimRow)
                && investment::store::write_unlock(
                    investment::store::Bank::accountFlags, claimRow, unlocks::kFlagSet);
        }
        ready = ready && write_reward_unlocks(mutation) && investment::store::write_account(after)
                && transaction.commit();
    }
    if (!ready && mutation.claimedRecordIndex != kUnclaimedRecordIndex) {
        // The claim was written when the reward was prepared, so a refused install undoes it.
        unlocks::records::revoke(mutation.claimedRecordIndex);
    }
    return ready;
}

namespace {

/**
 * Emote definitions seeded into the collection item's wheel lanes.
 * All four are universal Common emotes, so no class or race can reject a seeded lane.
 */
constexpr std::uint32_t kYesEmoteDefinitionHash = 3184938442U;
constexpr std::uint32_t kNopeEmoteDefinitionHash = 48790291U;
constexpr std::uint32_t kCasualSitEmoteDefinitionHash = 383973261U;
constexpr std::uint32_t kCheerEmoteDefinitionHash = 2834933816U;

/** Lane order is the client's own wheel layout. */
constexpr std::array<std::uint32_t, authored_inventory::kEmoteCollectionSocketLaneCount>
    kEmoteCollectionDefaultPlugHashes{
        kCheerEmoteDefinitionHash,     // lane 0 -- top
        kCasualSitEmoteDefinitionHash, // lane 1 -- bottom
        kYesEmoteDefinitionHash,       // lane 2 -- left
        kNopeEmoteDefinitionHash,      // lane 3 -- right
    };

/**
 * Resolves and cross-checks the "Emotes" collection item's own configured content.
 * @param definition Receives the matching native item-definition row.
 * @return True only when both rows agree, declare no native equipment slot, and carry exactly the
 *         expected 4 ordinary socket lanes.
 */
[[nodiscard]] bool
resolve_emote_collection_definition(build_data::items::Definition& definition) noexcept {
    item_details::Definition detail{};
    return build_data::find_item_definition_hash(authored_inventory::kEmoteCollectionDefinitionHash,
                                                 definition)
           && definition.definitionHash == authored_inventory::kEmoteCollectionDefinitionHash
           && build_data::find_configured_item_detail(definition.definitionIndex, detail)
           && detail.definitionIndex == definition.definitionIndex
           && detail.definitionHash == authored_inventory::kEmoteCollectionDefinitionHash
           && detail.bucketId == definition.bucketId && !detail.equipmentSlot.has_value()
           && detail.ordinarySocketState == item_details::OrdinarySocketState::present
           && detail.ordinarySocketCount == authored_inventory::kEmoteCollectionSocketLaneCount;
}

/** Checks every seeded plug is installed and allowed in its lane before an item can carry it. */
[[nodiscard]] bool default_plugs_valid(std::uint16_t collectionDefinitionIndex) noexcept {
    for (std::size_t lane = 0; lane < kEmoteCollectionDefaultPlugHashes.size(); ++lane) {
        build_data::items::Definition plugDefinition{};
        if (!build_data::find_item_definition_hash(kEmoteCollectionDefaultPlugHashes[lane],
                                                   plugDefinition)
            || !build_data::is_socket_plug_allowed(collectionDefinitionIndex,
                                                   static_cast<std::uint8_t>(lane),
                                                   plugDefinition.definitionIndex)) {
            return false;
        }
    }
    return true;
}

/** Checks an equipped collection item's plugs so a stale set is repaired, not trusted. */
[[nodiscard]] bool socket_state_sound(const authored_inventory::Item& item,
                                      std::uint16_t collectionDefinitionIndex) noexcept {
    if (item.sockets.policy != authored_inventory::SocketPolicy::authored
        || item.sockets.plugCount != authored_inventory::kEmoteCollectionSocketLaneCount) {
        return false;
    }
    for (std::size_t lane = 0; lane < authored_inventory::kEmoteCollectionSocketLaneCount; ++lane) {
        const std::optional<std::uint32_t>& plugHash = item.sockets.plugs[lane];
        build_data::items::Definition plugDefinition{};
        if (!plugHash.has_value()
            || !build_data::find_item_definition_hash(*plugHash, plugDefinition)
            || !build_data::is_socket_plug_allowed(collectionDefinitionIndex,
                                                   static_cast<std::uint8_t>(lane),
                                                   plugDefinition.definitionIndex)) {
            return false;
        }
    }
    return true;
}

} // namespace

/**
 * Equips each character with the "Emotes" collection item in the emote slot.
 * Its content declares no native slot, so every resolver reaches it through
 * resolve_native_equipment_slot. Its 4 lanes have no native default and are seeded here.
 */
EmoteCollectionOutcome ensure_character_emote_collection() noexcept {
    // The wheel occupies the authored emote equipment slot.
    constexpr std::size_t kEmoteCollectionSlot =
        static_cast<std::size_t>(authored_inventory::EquipmentSlot::emote);

    // Nothing can be concluded before these domains publish, so this is a retry, not a verdict.
    if (!build_data::item_definitions_ready() || !build_data::configured_item_details_ready()
        || !build_data::socket_plug_rules_ready()) {
        return EmoteCollectionOutcome::notReady;
    }
    // Published and still unresolved means the build cannot carry the wheel; a retry never helps.
    build_data::items::Definition collectionDefinition{};
    if (!resolve_emote_collection_definition(collectionDefinition)
        || !default_plugs_valid(collectionDefinition.definitionIndex)) {
        return EmoteCollectionOutcome::unsupported;
    }

    investment::store::g_mutex.lock();
    AccountState candidate = investment::store::account();
    if (!account::valid(candidate)) {
        investment::store::g_mutex.unlock();
        return EmoteCollectionOutcome::notReady;
    }
    bool changed = false;
    bool failed = false;
    for (std::size_t characterIndex = 0; characterIndex < candidate.characterCount && !failed;
         ++characterIndex) {
        CharacterState& character = candidate.characters[characterIndex];
        auto& collectionSlot = character.equipment.slots[kEmoteCollectionSlot];
        const bool present =
            collectionSlot.has_value()
            && collectionSlot->definitionHash == authored_inventory::kEmoteCollectionDefinitionHash;
        if (present && socket_state_sound(*collectionSlot, collectionDefinition.definitionIndex)) {
            continue;
        }
        if (character.nextInventorySerial
            >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
            failed = true;
            break;
        }
        // A repair owns the definition, the sockets and the serial. Every other field, the
        // item-state flags above all, belongs to the player and survives.
        authored_inventory::Item granted = present ? *collectionSlot : authored_inventory::Item{};
        if (!present) {
            std::uint64_t instanceSoid = 0;
            if (!next_item_instance_soid(candidate, instanceSoid)) {
                failed = true;
                break;
            }
            granted.instanceSoid = instanceSoid;
            granted.level = 0;
            granted.quantity = 1;
        }
        granted.definitionHash = authored_inventory::kEmoteCollectionDefinitionHash;
        granted.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
        // Replaced whole: lanes past the used prefix must be empty for the block to validate.
        granted.sockets = authored_inventory::Sockets{};
        granted.sockets.policy = authored_inventory::SocketPolicy::authored;
        granted.sockets.plugCount = kEmoteCollectionDefaultPlugHashes.size();
        for (std::size_t lane = 0; lane < kEmoteCollectionDefaultPlugHashes.size(); ++lane) {
            granted.sockets.plugs[lane] = kEmoteCollectionDefaultPlugHashes[lane];
        }
        collectionSlot = granted;
        changed = true;
    }
    if (failed) {
        investment::store::g_mutex.unlock();
        return EmoteCollectionOutcome::failed;
    }
    if (!changed) {
        investment::store::g_mutex.unlock();
        return EmoteCollectionOutcome::ready;
    }
    if (!account::valid(candidate)) {
        investment::store::g_mutex.unlock();
        return EmoteCollectionOutcome::failed;
    }
    if (!investment::store::write_account(candidate)) {
        investment::store::g_mutex.unlock();
        return EmoteCollectionOutcome::failed;
    }
    investment::store::g_mutex.unlock();
    return EmoteCollectionOutcome::ready;
}

} // namespace sunrise::state
