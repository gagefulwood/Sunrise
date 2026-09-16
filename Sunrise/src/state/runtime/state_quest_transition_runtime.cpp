#include "state_quest_transition_runtime.h"

#include <limits>

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

/**
 * Resolves counted progress by character identity, never by an account-wide fallback.
 * @param transition Validated character quest contract.
 * @param characterSoid Selected character's stable identity.
 * @param family Receives resolved predicate inputs; used only on success.
 * @return False for absent, duplicate or unreadable inputs.
 */
[[nodiscard]] bool resolve_inputs(const items::QuestTransition& transition,
                                  std::uint64_t characterSoid,
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
                    characterSoid, predicate.valueSlot, value)) {
                return false;
            }
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
 * @param policy Whether unresolved effects may be omitted for a reconstruction test.
 * @return False unless the single replacement fits and all supported objectives are complete.
 */
bool prepare_quest_transition(std::uint64_t sourceInstanceSoid,
                              const items::QuestTransition& transition,
                              PendingQuestTransition& mutation,
                              QuestTransitionPolicy policy) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    mutation = {};
    if (!items::valid(transition) || sourceInstanceSoid == 0
        || (policy != QuestTransitionPolicy::requireNoEffects
            && policy != QuestTransitionPolicy::reconstructLinear)
        || (policy == QuestTransitionPolicy::requireNoEffects
            && transition.completionEffect != items::kUnavailableQuestCompletionEffect)) {
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
    if (!resolve_inputs(transition, character.soid, family)
        || !items::complete(transition, family)) {
        return false;
    }
    std::int32_t currentValue = 0;
    items::Definition source{}, successor{};
    CharacterItemLocation location{};
    if (!investment::store::read_unlock(
            investment::store::Bank::characterObjectValues, transition.valueRow, currentValue)
        || currentValue != transition.currentValue
        || !build_data::find_item_definition_index(transition.sourceItemIndex, source)
        || !build_data::find_item_definition_index(transition.successorItemIndex, successor)
        || source.bucketId != items::kPursuitBucketId
        || successor.bucketId != items::kPursuitBucketId
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
    afterUnlocks.characterObjectValues[transition.valueRow] = transition.nextValue;
    return true;
}

/**
 * The store rolls back both writes if either the inventory or quest value fails.
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
           && investment::store::write_unlock(investment::store::Bank::characterObjectValues,
                                              transition.valueRow,
                                              transition.nextValue)
           && transaction.commit();
}

} // namespace sunrise::state
