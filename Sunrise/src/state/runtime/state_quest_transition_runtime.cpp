#include "state_quest_transition_runtime.h"

#include <limits>

#include "../build_data/vendors/vendor_catalog.h"
#include "../equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../investment/store_internal.h"
#include "state_account_transaction_helpers.h"

namespace sunrise::state {
namespace {

namespace items = build_data::items;
namespace inventory = account::inventory;
namespace loadout = middleware::datagen::family4::loadout;
using namespace runtime::detail;

/** Pursuits have no equipment slot; the loadout resolver publishes them at slot zero. */
constexpr std::uint8_t kPursuitEquipmentSlot = 0;
/** One accepted visit completes the supported binary visit objective. */
constexpr std::int32_t kCompletedVisit = 1;

/** @return The saved bank for a contract whose scope has already been validated. */
[[nodiscard]] investment::store::Bank
quest_bank(const items::QuestTransition& transition) noexcept {
    return transition.scope == items::QuestInitialization::Scope::account
               ? investment::store::Bank::objectiveValues
               : investment::store::Bank::characterObjectValues;
}

/**
 * Resolves saved counters by their declared owner, never by a global override fallback.
 * @param transition Validated quest contract and predicate sources.
 * @param account Locked account snapshot containing the selected character.
 * @param characterIndex Selected character's roster index.
 * @param family Receives resolved predicate inputs; used only on success.
 * @return False for absent, duplicate or unreadable inputs.
 */
[[nodiscard]] bool resolve_inputs(const items::QuestTransition& transition,
                                  const AccountState& account,
                                  std::size_t characterIndex,
                                  Family5State& family) noexcept {
    Family5State global{};
    family = {};
    if (!investment::store::read_family5(global)) {
        return false;
    }
    for (std::size_t index = 0; index < transition.objectiveCount; ++index) {
        const auto& predicate = transition.objectives[index];
        std::optional<std::int32_t> value;
        if (predicate.input == items::QuestPredicate::Input::characterCounter) {
            if (!investment::store::read_character_objective(
                    account.characters[characterIndex].soid, predicate.valueSlot, value)) {
                return false;
            }
        } else if (predicate.input == items::QuestPredicate::Input::accountCounter) {
            std::int32_t counter = 0;
            if (!investment::store::read_unlock(
                    investment::store::Bank::objectiveValues, predicate.valueRow, counter)) {
                return false;
            }
            value = counter;
        } else if (predicate.input == items::QuestPredicate::Input::characterPower) {
            std::int32_t power = 0;
            if (!equipment::light::resolution::character_light(account, characterIndex, power)) {
                return false;
            }
            value = power;
        } else {
            for (std::size_t row = 0; row < global.valueCount; ++row) {
                if (global.values[row].slot == predicate.valueSlot) {
                    if (value.has_value()) {
                        return false;
                    }
                    value = global.values[row].value;
                }
            }
        }
        if (!value.has_value()) {
            return false;
        }
        bool present = false;
        for (std::size_t row = 0; row < family.valueCount; ++row) {
            if (family.values[row].slot == predicate.valueSlot) {
                if (family.values[row].value != *value) {
                    return false;
                }
                present = true;
            }
        }
        if (!present) {
            family.values[family.valueCount++] = {predicate.valueSlot, *value};
        }
    }
    return true;
}

/**
 * A pursuit replacement cannot share a definition with another owned quest row.
 * @param character Selected character whose inventory was validated.
 * @param sourceHash Current-stage definition hash.
 * @param successorHash Next-stage definition hash.
 * @return True when exactly one current stage and no successor are owned.
 */
[[nodiscard]] bool unique_stage(const CharacterState& character,
                                std::uint32_t sourceHash,
                                std::uint32_t successorHash) noexcept {
    std::size_t matches = 0;
    for (const auto& equipped : character.equipment.slots) {
        if (equipped.has_value()
            && (equipped->definitionHash == sourceHash
                || equipped->definitionHash == successorHash)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        const auto hash = character.inventory.values[index].definitionHash;
        if (hash == successorHash) {
            return false;
        }
        matches += static_cast<std::size_t>(hash == sourceHash);
    }
    for (std::size_t index = 0; index < character.stacks.count; ++index) {
        const auto hash = character.stacks.values[index].definitionHash;
        if (hash == sourceHash || hash == successorHash) {
            return false;
        }
    }
    return matches == 1;
}

} // namespace

/**
 * Capture inventory, quest progress and condition inputs under the same store lock.
 * @param sourceInstanceSoid Owned current-stage item to replace.
 * @param transition Decoded installed-content contract, not a client request.
 * @param mutation Receives a complete plan, or stays empty on refusal.
 * @param policy Which reconstructed mechanics may omit unresolved completion effects.
 * @return False unless the replacement fits and the policy can satisfy every objective.
 */
bool prepare_quest_transition(std::uint64_t sourceInstanceSoid,
                              const items::QuestTransition& transition,
                              PendingQuestTransition& mutation,
                              QuestTransitionPolicy policy) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    mutation = {};
    if (!items::valid(transition) || sourceInstanceSoid == 0
        || (policy != QuestTransitionPolicy::requireNoEffects
            && policy != QuestTransitionPolicy::reconstructLinear
            && policy != QuestTransitionPolicy::reconstructPowerGate
            && policy != QuestTransitionPolicy::reconstructVendorVisit)
        || (policy == QuestTransitionPolicy::requireNoEffects
            && transition.completionEffect != items::kUnavailableQuestCompletionEffect)) {
        return false;
    }

    const bool visit = policy == QuestTransitionPolicy::reconstructVendorVisit;
    if (visit
        && (transition.scope != items::QuestInitialization::Scope::account
            || transition.objectiveCount != 1
            || transition.objectives[0].input != items::QuestPredicate::Input::accountCounter
            || transition.objectives[0].minimumValue != kCompletedVisit
            || transition.objectives[0].valueRow == transition.valueRow)) {
        return false;
    }

    AccountState before{};
    Family5State family{};
    if (!investment::store::read_account(before) || !account::valid(before)) {
        return false;
    }
    const auto characterIndex = selected_character_index(before);
    if (characterIndex >= before.characterCount) {
        return false;
    }
    const auto& character = before.characters[characterIndex];
    if (!resolve_inputs(transition, before, characterIndex, family)) {
        return false;
    }
    // Keep the saved input for stale checks; reply credit exists only in the after-image.
    if (visit ? family.values[0].value != 0 : !items::complete(transition, family)) {
        return false;
    }
    std::int32_t currentValue = 0;
    items::Definition source{}, successor{};
    CharacterItemLocation location{};
    if (!investment::store::read_unlock(quest_bank(transition), transition.valueRow, currentValue)
        || currentValue != transition.currentValue
        || !build_data::find_item_definition_index(transition.sourceItemIndex, source)
        || !build_data::find_item_definition_index(transition.successorItemIndex, successor)
        || source.bucketId != items::kPursuitBucketId
        || successor.bucketId != items::kPursuitBucketId
        || (visit
            && transition
                   != items::visit_transition(
                       source.visitGate, source.questInitialization, source.definitionIndex))
        || (policy == QuestTransitionPolicy::reconstructPowerGate
            && transition
                   != items::power_transition(
                       source.powerGate, source.questInitialization, source.definitionIndex))
        || !unique_stage(character, source.definitionHash, successor.definitionHash)
        || !find_character_item_location(character, sourceInstanceSoid, location)
        || location.equipped || character.inventory.values[location.index].quantity != 1
        || (character.inventory.values[location.index].flags & inventory::kLockedItemFlag) != 0
        || character.inventory.values[location.index].definitionHash != source.definitionHash
        || character.nextInventorySerial
               >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
        return false;
    }

    std::uint64_t successorSoid = 0;
    loadout::ResolvedLoadout beforeLoadout{};
    std::uint16_t sourceRow = 0;
    std::uint8_t sourceSlot = 0;
    if (!next_item_instance_soid(before, successorSoid)
        || !loadout::resolve(before, characterIndex, beforeLoadout)
        || !find_unequipped_row(beforeLoadout, sourceInstanceSoid, sourceRow, sourceSlot)
        || sourceSlot != kPursuitEquipmentSlot) {
        return false;
    }

    AccountState after = before;
    auto& changed = after.characters[characterIndex];
    inventory::Item granted{};
    granted.instanceSoid = successorSoid;
    granted.definitionHash = successor.definitionHash;
    granted.quantity = 1;
    granted.level = acquisition_level(character);
    granted.mutationSerial = static_cast<std::int32_t>(changed.nextInventorySerial++);
    // Replace in one candidate so a full bucket does not need a spare grant row.
    changed.inventory.values[location.index] = granted;

    loadout::ResolvedLoadout afterLoadout{};
    std::uint16_t successorRow = 0;
    std::uint8_t successorSlot = 0;
    if (!account::valid(after) || !loadout::resolve(after, characterIndex, afterLoadout)
        || !find_unequipped_row(afterLoadout, successorSoid, successorRow, successorSlot)
        || successorSlot != kPursuitEquipmentSlot
        || loadout_contains(afterLoadout, sourceInstanceSoid)
        || beforeLoadout.itemCount != afterLoadout.itemCount) {
        return false;
    }

    mutation.beforeCharacter = character;
    mutation.afterCharacter = changed;
    mutation.transition = transition;
    // complete() proves that each requested slot is present exactly once.
    for (std::size_t index = 0; index < transition.objectiveCount; ++index) {
        for (std::size_t row = 0; row < family.valueCount; ++row) {
            if (family.values[row].slot == transition.objectives[index].valueSlot) {
                mutation.inputs[index] = family.values[row].value;
                break;
            }
        }
    }
    mutation.accountSoid = before.primarySoid;
    mutation.sourceInstanceSoid = sourceInstanceSoid;
    mutation.successorInstanceSoid = successorSoid;
    mutation.characterIndex = characterIndex;
    mutation.sourceRow = sourceRow;
    mutation.successorRow = successorRow;
    mutation.policy = policy;
    mutation.prepared = true;
    return true;
}

/**
 * Selects one eligible first-stage Power gate from installed metadata under the save lock.
 * @param mutation Receives a prepared replacement; empty when none can advance.
 * @return True when one owned first stage can advance using current character Power.
 */
bool prepare_next_power_quest_transition(PendingQuestTransition& mutation) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    mutation = {};
    AccountState account{};
    if (!investment::store::read_account(account) || !account::valid(account)) {
        return false;
    }
    const auto characterIndex = selected_character_index(account);
    if (characterIndex >= account.characterCount) {
        return false;
    }
    const auto& inventory = account.characters[characterIndex].inventory;
    for (std::size_t index = 0; index < inventory.count; ++index) {
        items::Definition source{};
        const auto& owned = inventory.values[index];
        if (!build_data::find_item_definition_hash(owned.definitionHash, source)
            || source.powerGate == items::QuestPowerGate{}) {
            continue;
        }
        const auto transition = items::power_transition(
            source.powerGate, source.questInitialization, source.definitionIndex);
        if (prepare_quest_transition(owned.instanceSoid,
                                     transition,
                                     mutation,
                                     QuestTransitionPolicy::reconstructPowerGate)) {
            return true;
        }
    }
    return false;
}

/**
 * Rebuild the after-image before saving; a caller cannot alter a prepared grant.
 * @param transition Current installed-content contract.
 * @param mutation Captured replacement and condition inputs.
 * @param after Receives the account after-image; use only on success.
 * @param afterUnlocks Receives its unlock banks; use only on success.
 * @return False when the captured state or decoded contract no longer matches.
 */
bool preview_quest_transition(const items::QuestTransition& transition,
                              const PendingQuestTransition& mutation,
                              AccountState& after,
                              unlocks::Table& afterUnlocks) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    PendingQuestTransition rebuilt{};
    if (!mutation.prepared || transition != mutation.transition
        || !prepare_quest_transition(
            mutation.sourceInstanceSoid, transition, rebuilt, mutation.policy)
        || mutation.accountSoid != rebuilt.accountSoid
        || mutation.characterIndex != rebuilt.characterIndex
        || mutation.successorInstanceSoid != rebuilt.successorInstanceSoid
        || mutation.sourceRow != rebuilt.sourceRow || mutation.successorRow != rebuilt.successorRow
        || mutation.inputs != rebuilt.inputs
        || !same_character(mutation.beforeCharacter, rebuilt.beforeCharacter)
        || !same_character(mutation.afterCharacter, rebuilt.afterCharacter)
        || !investment::store::read_account(after)
        || !investment::store::read_unlocks(afterUnlocks,
                                            static_cast<int>(mutation.characterIndex))) {
        return false;
    }
    after.characters[mutation.characterIndex] = rebuilt.afterCharacter;
    if (transition.scope == items::QuestInitialization::Scope::account) {
        afterUnlocks.objectiveValues[transition.valueRow] = transition.nextValue;
    } else {
        afterUnlocks.characterObjectValues[transition.valueRow] = transition.nextValue;
    }
    if (mutation.policy == QuestTransitionPolicy::reconstructVendorVisit) {
        afterUnlocks.objectiveValues[transition.objectives[0].valueRow] = kCompletedVisit;
    }
    return true;
}

/**
 * Inventory, stage and any visit credit share the same rollback boundary.
 * @param transition Current installed-content contract.
 * @param mutation Prepared replacement consumed on every exit.
 * @return True only after the combined transaction commits.
 */
bool commit_quest_transition(const items::QuestTransition& transition,
                             PendingQuestTransition& mutation) noexcept {
    const PendingConsumption consume{mutation};
    investment::store::Transaction transaction;
    AccountState after{};
    unlocks::Table afterUnlocks{};
    return transaction.ready()
           && preview_quest_transition(transition, mutation, after, afterUnlocks)
           && investment::store::write_account(after)
           && investment::store::write_unlock(
               quest_bank(transition), transition.valueRow, transition.nextValue)
           && (mutation.policy != QuestTransitionPolicy::reconstructVendorVisit
               || investment::store::write_unlock(investment::store::Bank::objectiveValues,
                                                  transition.objectives[0].valueRow,
                                                  kCompletedVisit))
           && transaction.commit();
}

/**
 * Supported visit replies never fall back to category-based item acquisition.
 * @param vendorIndex Installed vendor ordinal.
 * @param interactionIndex Selected interaction ordinal.
 * @return True when a content-linked supported visit owns this interaction.
 */
bool vendor_visit_supported(std::uint16_t vendorIndex, std::uint16_t interactionIndex) noexcept {
    namespace vendors = build_data::vendors;
    vendors::IndexEntry entry{};
    vendors::Definition vendor{};
    if (!vendors::find_index(vendorIndex, entry) || !vendors::find(entry.definitionHash, vendor)) {
        return false;
    }
    for (std::size_t index = 0; index < vendor.visitReplyCount; ++index) {
        const auto& reply = vendor.visitReplies[index];
        if (reply.interactionIndex != interactionIndex) {
            continue;
        }
        for (const auto flag : reply.flags) {
            items::Definition source{};
            if (items::find_visit_flag(flag, source)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * An interaction's incomplete flag must join to one owned quest and one active account gate.
 * @param vendorIndex Installed vendor ordinal.
 * @param interactionIndex Selected interaction ordinal.
 * @param replyIndex Selected reply ordinal.
 * @param mutation Receives the prepared visit; cleared on refusal.
 * @return False when the reply, ownership, gate or transition is unsupported.
 */
bool prepare_vendor_visit(std::uint16_t vendorIndex,
                          std::uint16_t interactionIndex,
                          std::uint16_t replyIndex,
                          PendingVendorVisit& mutation) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    mutation = {};
    namespace vendors = build_data::vendors;
    vendors::IndexEntry entry{};
    vendors::Definition vendor{};
    AccountState account{};
    if (!vendors::find_index(vendorIndex, entry) || !vendors::find(entry.definitionHash, vendor)
        || !investment::store::read_account(account) || !account::valid(account)) {
        return false;
    }
    const auto characterIndex = selected_character_index(account);
    if (characterIndex >= account.characterCount) {
        return false;
    }
    const vendors::VisitReply* selected = nullptr;
    for (std::size_t index = 0; index < vendor.visitReplyCount; ++index) {
        const auto& reply = vendor.visitReplies[index];
        if (reply.interactionIndex == interactionIndex && reply.replyIndex == replyIndex) {
            if (selected != nullptr) {
                return false;
            }
            selected = &reply;
        }
    }
    if (selected == nullptr) {
        return false;
    }
    items::QuestTransition transition{};
    std::uint64_t sourceSoid = 0;
    const auto& inventory = account.characters[characterIndex].inventory;
    for (std::size_t index = 0; index < inventory.count; ++index) {
        items::Definition source{};
        const auto& owned = inventory.values[index];
        if (!build_data::find_item_definition_hash(owned.definitionHash, source)
            || source.visitGate == items::QuestVisitGate{}) {
            continue;
        }
        for (std::size_t flag = 0; flag < selected->flags.size(); ++flag) {
            if (selected->flags[flag] != source.visitGate.incompleteFlag) {
                continue;
            }
            const auto gateRow = selected->accountFlagRows[1 - flag];
            std::int32_t gate = 0;
            if (sourceSoid != 0 || gateRow >= unlocks::kAccountFlagCapacity
                || !investment::store::read_unlock(
                    investment::store::Bank::accountFlags, gateRow, gate)
                || gate != unlocks::kFlagSet) {
                return false;
            }
            transition = items::visit_transition(
                source.visitGate, source.questInitialization, source.definitionIndex);
            sourceSoid = owned.instanceSoid;
        }
    }
    if (sourceSoid == 0
        || !prepare_quest_transition(sourceSoid,
                                     transition,
                                     mutation.quest,
                                     QuestTransitionPolicy::reconstructVendorVisit)) {
        return false;
    }
    mutation.vendorIndex = vendorIndex;
    mutation.interactionIndex = interactionIndex;
    mutation.replyIndex = replyIndex;
    return true;
}

/**
 * The selected reply and its gate must still authorize the captured quest transition.
 * @param mutation Prepared visit; consumed on every exit.
 * @return True only after credit, stage and inventory commit together.
 */
bool commit_vendor_visit(PendingVendorVisit& mutation) noexcept {
    const PendingConsumption consume{mutation.quest};
    const std::lock_guard lock(investment::store::g_mutex);
    PendingVendorVisit rebuilt{};
    return mutation.quest.prepared
           && prepare_vendor_visit(
               mutation.vendorIndex, mutation.interactionIndex, mutation.replyIndex, rebuilt)
           && mutation.quest.sourceInstanceSoid == rebuilt.quest.sourceInstanceSoid
           && commit_quest_transition(rebuilt.quest.transition, mutation.quest);
}

} // namespace sunrise::state
