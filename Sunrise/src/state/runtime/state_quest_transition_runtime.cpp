#include "state_quest_transition_runtime.h"

#include <limits>

#include "../../core/logging/log.h"
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

/** Why a build-bound completion definition could not supply the supported operation pair. */
enum class CompletionResolution : std::uint8_t {
    ready,
    missingCoverage,
    unsupportedShape,
    unresolvedOperations,
    transitionMismatch,
};

/**
 * Emits one debug refusal without logging an ordinarily unmet quest predicate.
 * @param transition Installed transition naming the site and source item.
 * @param reason Static reason token for the failed contract.
 */
void log_refusal(const items::QuestTransition& transition, const char* reason) noexcept {
    core::log::writef(core::log::Channel::state,
                      core::log::Level::debug,
                      "ev=quest_completion stage=prepare result=refused reason=%s site=%u "
                      "source_item=%u",
                      reason,
                      transition.completionEffect,
                      transition.sourceItemIndex);
}

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
 * @return The first failed contract, or ready after both operations match quest metadata.
 */
[[nodiscard]] CompletionResolution
resolve_completion(const items::QuestTransition& transition,
                   reward_sites::ItemProgression& item,
                   reward_sites::CharacterObjectTransition& characterObject) noexcept {
    reward_sites::Definition site{};
    if (transition.completionEffect == items::kUnavailableQuestCompletionEffect
        || !reward_sites::find(transition.completionEffect, site)) {
        return CompletionResolution::missingCoverage;
    }
    if (site.itemProgressionCount != kQuestCompletionOperationCount
        || site.characterObjectTransitionCount != kQuestCompletionOperationCount) {
        return CompletionResolution::unsupportedShape;
    }
    if (!read_completion_operations(site, item, characterObject)) {
        return CompletionResolution::unresolvedOperations;
    }
    return completion_matches_transition(transition, item, characterObject)
               ? CompletionResolution::ready
               : CompletionResolution::transitionMismatch;
}

/**
 * Resolves supported predicate inputs from current server state.
 * Slot 462's native Power aggregation remains unverified.
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
    equipment::light::Evaluation configuredPower{};
    bool globalLoaded = false;
    bool configuredPowerLoaded = false;
    family = {};
    for (std::size_t index = 0; index < transition.objectiveCount; ++index) {
        const auto& predicate = transition.objectives[index];
        std::optional<std::int32_t> value;
        if (predicate.input == items::QuestPredicate::Input::characterCounter) {
            if (!investment::store::read_character_objective(
                    account.characters[characterIndex].soid, predicate.valueSlot, value)) {
                return false;
            }
        } else if (predicate.input == items::QuestPredicate::Input::powerCondition) {
            // Slot 462 currently uses the configured aggregate, not character_light().
            if (!configuredPowerLoaded
                && !equipment::light::resolution::resolve(
                    account, characterIndex, configuredPower)) {
                return false;
            }
            configuredPowerLoaded = true;
            value = configuredPower.average;
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
    if (!find_character_item_location(character, sourceInstanceSoid, location)) {
        return false;
    }
    if (location.equipped) {
        return false;
    }
    if (!unique_stage(character, source.definitionHash, successor.definitionHash)) {
        return false;
    }
    const auto* owned = character_item_at(character, location);
    return owned != nullptr && owned->quantity == 1
           && (owned->flags & inventory::kLockedItemFlag) == 0
           && owned->definitionHash == source.definitionHash
           && character.nextInventorySerial
                  < static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
}

[[nodiscard]] bool
character_value_matches(const reward_sites::CharacterObjectTransition& transition) noexcept {
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
    if (!items::valid(transition) || sourceInstanceSoid == 0) {
        log_refusal(transition, "invalid_contract");
        return false;
    }
    const CompletionResolution completion =
        resolve_completion(transition, itemProgression, characterObjectTransition);
    if (completion != CompletionResolution::ready) {
        const char* reason = "transition_mismatch";
        if (completion == CompletionResolution::missingCoverage) {
            reason = "missing_coverage";
        } else if (completion == CompletionResolution::unsupportedShape) {
            reason = "unsupported_shape";
        } else if (completion == CompletionResolution::unresolvedOperations) {
            reason = "unresolved_operations";
        }
        log_refusal(transition, reason);
        return false;
    }

    AccountState before{};
    Family5State family{};
    if (!investment::store::read_account(before) || !account::valid(before)) {
        log_refusal(transition, "state_unavailable");
        return false;
    }
    const auto characterIndex = selected_character_index(before);
    if (characterIndex >= before.characterCount) {
        return false;
    }
    const auto& character = before.characters[characterIndex];
    if (!resolve_inputs(transition, before, characterIndex, family)) {
        log_refusal(transition, "unresolved_input");
        return false;
    }
    if (!items::complete(transition, family)) {
        return false;
    }
    items::Definition source{}, successor{};
    CharacterItemLocation location{};
    if (!character_value_matches(characterObjectTransition)) {
        log_refusal(transition, "state_mismatch");
        return false;
    }
    if (!resolve_progression_items(itemProgression, source, successor)) {
        log_refusal(transition, "installed_identity_mismatch");
        return false;
    }
    if (!resolve_owned_source(character, sourceInstanceSoid, source, successor, location)) {
        log_refusal(transition, "ownership_mismatch");
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
        log_refusal(transition, "replacement_unavailable");
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
        log_refusal(transition, "replacement_mismatch");
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
QuestCompletionPreparation
prepare_completed_quest_transition(std::uint64_t characterSoid,
                                   PendingQuestTransition& mutation) noexcept {
    mutation = {};
    if (characterSoid == 0) {
        return QuestCompletionPreparation::noWork;
    }
    AccountState account{};
    {
        const std::lock_guard lock(investment::store::g_mutex);
        if (!investment::store::read_account(account)) {
            return QuestCompletionPreparation::retry;
        }
    }
    if (!account::valid(account)) {
        return QuestCompletionPreparation::noWork;
    }
    const std::size_t characterIndex = selected_character_index(account);
    if (characterIndex >= account.characterCount
        || account.characters[characterIndex].soid != characterSoid) {
        return QuestCompletionPreparation::noWork;
    }
    const auto& character = account.characters[characterIndex];
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        const auto& owned = character.inventory.values[index];
        items::Definition definition{};
        items::QuestTransition transition{};
        if (build_data::find_item_definition_hash(owned.definitionHash, definition)
            && build_data::find_quest_transition(definition.definitionIndex, transition)
            && prepare_quest_transition(owned.instanceSoid, transition, mutation)) {
            return mutation.beforeCharacter.soid == characterSoid
                       ? QuestCompletionPreparation::ready
                       : QuestCompletionPreparation::noWork;
        }
    }
    return QuestCompletionPreparation::noWork;
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
        log_refusal(transition, "stale_state");
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
