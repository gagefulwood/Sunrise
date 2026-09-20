#include "state_quest_transition_runtime.h"

#include <limits>

#include "../equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../investment/store_internal.h"
#include "state_account_transaction_helpers.h"

namespace sunrise::state {
namespace {

namespace items = build_data::items;
namespace reward_sites = build_data::reward_sites;
namespace inventory = account::inventory;
namespace loadout = middleware::datagen::family4::loadout;
using namespace runtime::detail;

/** Pursuits have no equipment slot; the loadout resolver publishes them at slot zero. */
constexpr std::uint8_t kPursuitEquipmentSlot = 0;
/** Supported quest completions replace one item stage and advance one character-object row. */
constexpr std::size_t kQuestCompletionOperationCount = 1;

/**
 * Reads the one-for-one operation shape supported by quest completion.
 * @param site Resolved Reward Site definition.
 * @param item Receives its single item replacement.
 * @param characterObject Receives its single character-object transition.
 * @return False when the site has another shape or an incomplete operation range.
 */
[[nodiscard]] bool
read_completion_operations(const reward_sites::Definition& site,
                           reward_sites::ItemProgression& item,
                           reward_sites::CharacterObjectTransition& characterObject) noexcept {
    std::array<reward_sites::ItemProgression, kQuestCompletionOperationCount> itemRows{};
    std::array<reward_sites::CharacterObjectTransition, kQuestCompletionOperationCount>
        characterRows{};
    std::size_t itemCount = 0;
    std::size_t characterCount = 0;
    if (site.itemProgressionCount != itemRows.size()
        || site.characterObjectTransitionCount != characterRows.size()
        || !reward_sites::item_progressions(site, itemRows, itemCount)
        || !reward_sites::character_object_transitions(site, characterRows, characterCount)
        || itemCount != itemRows.size() || characterCount != characterRows.size()) {
        return false;
    }
    item = itemRows.front();
    characterObject = characterRows.front();
    return true;
}

/**
 * Checks that resolved Reward Site operations implement the installed quest transition.
 * @param transition Installed quest metadata.
 * @param item Resolved item replacement.
 * @param characterObject Resolved character-object transition.
 * @return True when both sources describe the same completion.
 */
[[nodiscard]] bool completion_matches_transition(
    const items::QuestTransition& transition,
    const reward_sites::ItemProgression& item,
    const reward_sites::CharacterObjectTransition& characterObject) noexcept {
    return item.sourceItemIndex == transition.sourceItemIndex
           && item.successorItemIndex == transition.successorItemIndex
           && characterObject.rowIndex == transition.valueRow
           && characterObject.expectedValue == transition.currentValue
           && characterObject.nextValue == transition.nextValue;
}

/**
 * Resolves the currently supported one-for-one quest completion shape.
 * @param transition Installed quest metadata naming the Reward Site.
 * @param item Receives the site's item replacement.
 * @param characterObject Receives the site's selected-character compare-and-set.
 * @return False when the site is absent, has another shape, or disagrees with quest metadata.
 */
[[nodiscard]] bool
resolve_completion(const items::QuestTransition& transition,
                   reward_sites::ItemProgression& item,
                   reward_sites::CharacterObjectTransition& characterObject) noexcept {
    reward_sites::Definition site{};
    if (transition.completionEffect == items::kUnavailableQuestCompletionEffect
        || !reward_sites::find(transition.completionEffect, site)) {
        return false;
    }
    return read_completion_operations(site, item, characterObject)
           && completion_matches_transition(transition, item, characterObject);
}

/**
 * Resolves each native predicate from its authoritative server state.
 * @param transition Validated character quest contract.
 * @param account Validated account containing the selected character and equipment.
 * @param characterIndex Selected character row.
 * @param family Receives resolved predicate inputs; used only on success.
 * @return False for absent, duplicate or unreadable inputs.
 */
[[nodiscard]] bool resolve_inputs(const items::QuestTransition& transition,
                                  const AccountState& account,
                                  std::size_t characterIndex,
                                  Family5State& family) noexcept {
    Family5State global{};
    equipment::light::Evaluation equipmentPower{};
    bool globalLoaded = false;
    bool equipmentPowerLoaded = false;
    family = {};
    for (std::size_t index = 0; index < transition.objectiveCount; ++index) {
        const auto& predicate = transition.objectives[index];
        std::optional<std::int32_t> value;
        if (predicate.input == items::QuestPredicate::Input::characterCounter) {
            if (!investment::store::read_character_objective(
                    account.characters[characterIndex].soid, predicate.valueSlot, value)) {
                return false;
            }
        } else if (predicate.input == items::QuestPredicate::Input::equipmentPower) {
            if (!equipmentPowerLoaded
                && !equipment::light::resolution::resolve(
                    account, characterIndex, equipmentPower)) {
                return false;
            }
            equipmentPowerLoaded = true;
            value = equipmentPower.average;
        } else if (predicate.input == items::QuestPredicate::Input::family5) {
            if (!globalLoaded && !investment::store::read_family5(global)) {
                return false;
            }
            globalLoaded = true;
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

/**
 * Resolves and validates both item definitions named by a Reward Site progression.
 * @param progression Authored item replacement.
 * @param source Receives the installed source definition.
 * @param successor Receives the installed successor definition.
 * @return False when either identity changed or either item is not a pursuit.
 */
[[nodiscard]] bool resolve_progression_items(const reward_sites::ItemProgression& progression,
                                             items::Definition& source,
                                             items::Definition& successor) noexcept {
    return build_data::find_item_definition_index(progression.sourceItemIndex, source)
           && build_data::find_item_definition_index(progression.successorItemIndex, successor)
           && source.definitionHash == progression.sourceItemHash
           && successor.definitionHash == progression.successorItemHash
           && source.bucketId == items::kPursuitBucketId
           && successor.bucketId == items::kPursuitBucketId;
}

/**
 * Resolves the owned source item and checks that it is safe to replace.
 * @param character Selected character.
 * @param sourceInstanceSoid Owned source instance.
 * @param source Source-stage definition.
 * @param successor Successor-stage definition.
 * @param location Receives the source inventory location.
 * @return False when ownership is ambiguous or the source cannot be replaced in place.
 */
[[nodiscard]] bool resolve_owned_source(const CharacterState& character,
                                        std::uint64_t sourceInstanceSoid,
                                        const items::Definition& source,
                                        const items::Definition& successor,
                                        CharacterItemLocation& location) noexcept {
    if (!unique_stage(character, source.definitionHash, successor.definitionHash)
        || !find_character_item_location(character, sourceInstanceSoid, location)) {
        return false;
    }
    const auto& owned = character.inventory.values[location.index];
    return !location.equipped && owned.quantity == 1
           && (owned.flags & inventory::kLockedItemFlag) == 0
           && owned.definitionHash == source.definitionHash
           && character.nextInventorySerial
                  < static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
}

/**
 * Checks the compare value required by one character-object transition.
 * @param transition Authored state transition.
 * @return True when the current selected-character value matches the expected value.
 */
[[nodiscard]] bool
expected_character_value(const reward_sites::CharacterObjectTransition& transition) noexcept {
    std::int32_t currentValue = 0;
    return investment::store::read_unlock(
               investment::store::Bank::characterObjectValues, transition.rowIndex, currentValue)
           && currentValue == transition.expectedValue;
}

/**
 * Resolves the replaced pursuit row and checks that no source identity survives.
 * @param account Candidate account after-image.
 * @param characterIndex Selected character row.
 * @param beforeLoadout Loadout before replacement.
 * @param sourceInstanceSoid Retired source instance.
 * @param successorInstanceSoid New successor instance.
 * @param successorRow Receives the successor loadout row.
 * @return False when replacement changed loadout cardinality or pursuit placement.
 */
[[nodiscard]] bool resolve_replaced_pursuit(const AccountState& account,
                                            std::size_t characterIndex,
                                            const loadout::ResolvedLoadout& beforeLoadout,
                                            std::uint64_t sourceInstanceSoid,
                                            std::uint64_t successorInstanceSoid,
                                            std::uint16_t& successorRow) noexcept {
    loadout::ResolvedLoadout afterLoadout{};
    std::uint8_t successorSlot = 0;
    return account::valid(account) && loadout::resolve(account, characterIndex, afterLoadout)
           && find_unequipped_row(afterLoadout, successorInstanceSoid, successorRow, successorSlot)
           && successorSlot == kPursuitEquipmentSlot
           && !loadout_contains(afterLoadout, sourceInstanceSoid)
           && beforeLoadout.itemCount == afterLoadout.itemCount;
}

/**
 * Checks that a rebuilt transition matches every prepared value.
 * @param prepared Caller-held transition plan.
 * @param rebuilt Plan rebuilt from current persisted state.
 * @return True when no prepared input or after-image changed.
 */
[[nodiscard]] bool same_prepared_transition(const PendingQuestTransition& prepared,
                                            const PendingQuestTransition& rebuilt) noexcept {
    return prepared.accountSoid == rebuilt.accountSoid
           && prepared.characterIndex == rebuilt.characterIndex
           && prepared.successorInstanceSoid == rebuilt.successorInstanceSoid
           && prepared.sourceRow == rebuilt.sourceRow
           && prepared.successorRow == rebuilt.successorRow
           && prepared.itemProgression == rebuilt.itemProgression
           && prepared.characterObjectTransition == rebuilt.characterObjectTransition
           && prepared.inputs == rebuilt.inputs
           && same_character(prepared.beforeCharacter, rebuilt.beforeCharacter)
           && same_character(prepared.afterCharacter, rebuilt.afterCharacter);
}

} // namespace

/**
 * Capture inventory, quest progress and condition inputs under the same store lock.
 * @param sourceInstanceSoid Owned current-stage item to replace.
 * @param transition Decoded installed-content contract, not a client request.
 * @param mutation Receives a complete plan, or stays empty on refusal.
 * @return False unless the single replacement fits and all supported objectives are complete.
 */
bool prepare_quest_transition(std::uint64_t sourceInstanceSoid,
                              const items::QuestTransition& transition,
                              PendingQuestTransition& mutation) noexcept {
    const std::lock_guard lock(investment::store::g_mutex);
    mutation = {};
    reward_sites::ItemProgression itemProgression{};
    reward_sites::CharacterObjectTransition characterObjectTransition{};
    if (!items::valid(transition) || sourceInstanceSoid == 0
        || !resolve_completion(transition, itemProgression, characterObjectTransition)) {
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
    if (!resolve_inputs(transition, before, characterIndex, family)
        || !items::complete(transition, family)) {
        return false;
    }
    items::Definition source{}, successor{};
    CharacterItemLocation location{};
    if (!expected_character_value(characterObjectTransition)
        || !resolve_progression_items(itemProgression, source, successor)
        || !resolve_owned_source(character, sourceInstanceSoid, source, successor, location)) {
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

    std::uint16_t successorRow = 0;
    if (!resolve_replaced_pursuit(after,
                                  characterIndex,
                                  beforeLoadout,
                                  sourceInstanceSoid,
                                  successorSoid,
                                  successorRow)) {
        return false;
    }

    mutation.beforeCharacter = character;
    mutation.afterCharacter = changed;
    mutation.transition = transition;
    mutation.itemProgression = itemProgression;
    mutation.characterObjectTransition = characterObjectTransition;
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
    mutation.prepared = true;
    return true;
}

/** Prepares one event-driven owned-stage completion without polling at login. */
bool prepare_completed_quest_transition(PendingQuestTransition& mutation) noexcept {
    mutation = {};
    AccountState account{};
    {
        const std::lock_guard lock(investment::store::g_mutex);
        if (!investment::store::read_account(account) || !account::valid(account)) {
            return false;
        }
    }
    const std::size_t characterIndex = selected_character_index(account);
    if (characterIndex >= account.characterCount) {
        return false;
    }
    const auto& character = account.characters[characterIndex];
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        const auto& owned = character.inventory.values[index];
        items::Definition definition{};
        items::QuestTransition transition{};
        if (build_data::find_item_definition_hash(owned.definitionHash, definition)
            && build_data::find_quest_transition(definition.definitionIndex, transition)
            && prepare_quest_transition(owned.instanceSoid, transition, mutation)) {
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
        || !prepare_quest_transition(mutation.sourceInstanceSoid, transition, rebuilt)) {
        return false;
    }
    if (!same_prepared_transition(mutation, rebuilt) || !investment::store::read_account(after)
        || !investment::store::read_unlocks(afterUnlocks,
                                            static_cast<int>(mutation.characterIndex))) {
        return false;
    }
    after.characters[mutation.characterIndex] = rebuilt.afterCharacter;
    afterUnlocks.characterObjectValues[rebuilt.characterObjectTransition.rowIndex] =
        rebuilt.characterObjectTransition.nextValue;
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
                                              mutation.characterObjectTransition.rowIndex,
                                              mutation.characterObjectTransition.nextValue)
           && transaction.commit();
}

} // namespace sunrise::state
