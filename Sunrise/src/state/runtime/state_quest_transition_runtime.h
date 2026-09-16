#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../account/account_state.h"
#include "../build_data/items/quest_transition.h"
#include "../unlocks/definition.h"

namespace sunrise::state {

/** Completion effects are not implied by the order of quest-set members. */
enum class QuestTransitionPolicy : std::uint8_t {
    /** Refuse any completion reference whose effects have not been supplied. */
    requireNoEffects,
    /** Explicit test policy: replace the member without applying completion effects. */
    reconstructLinear,
    /** Live first-stage Power gates replace the member without interpreting completion effects. */
    reconstructPowerGate,
};

/** Inventory, condition inputs and the decoded contract captured before a stage replacement. */
struct PendingQuestTransition {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    build_data::items::QuestTransition transition{};
    std::array<std::int32_t, build_data::items::kQuestObjectiveCapacity> inputs{};
    std::uint64_t accountSoid{};
    std::uint64_t sourceInstanceSoid{};
    std::uint64_t successorInstanceSoid{};
    std::size_t characterIndex{};
    std::uint16_t sourceRow{};
    std::uint16_t successorRow{};
    QuestTransitionPolicy policy{};
    bool prepared{};
};

/**
 * Selects one eligible first-stage Power gate from the selected character's installed item
 * metadata.
 * @param mutation Receives a prepared replacement; empty when none can advance.
 * @return True when one owned first stage can advance using current character Power.
 */
[[nodiscard]] bool prepare_next_power_quest_transition(PendingQuestTransition& mutation) noexcept;

/**
 * The caller supplies a decoded installed-content contract, never a client-authored plan.
 * @param sourceInstanceSoid Owned current-stage item to replace.
 * @param transition Validated content metadata and save scope for one non-final quest stage.
 * @param mutation Receives the prepared replacement; cleared on failure.
 * @param policy Explicit authorization for unresolved completion effects.
 * @return False when ownership, conditions, saved progress or capacity prevent replacement.
 */
[[nodiscard]] bool prepare_quest_transition(
    std::uint64_t sourceInstanceSoid,
    const build_data::items::QuestTransition& transition,
    PendingQuestTransition& mutation,
    QuestTransitionPolicy policy = QuestTransitionPolicy::requireNoEffects) noexcept;

/**
 * Rebuilds the replacement against the same content contract and unchanged condition inputs.
 * @param transition Current installed-content contract, checked against the prepared copy.
 * @param mutation Prepared replacement without any published state.
 * @param after Receives the complete account after-image; use only on success.
 * @param afterUnlocks Receives matching unlock banks; use only on success.
 * @return False when the selected character, inventory, metadata or quest inputs changed.
 */
[[nodiscard]] bool preview_quest_transition(const build_data::items::QuestTransition& transition,
                                            const PendingQuestTransition& mutation,
                                            AccountState& after,
                                            unlocks::Table& afterUnlocks) noexcept;

/**
 * Item replacement and the active-stage value commit together or neither is saved.
 * @param transition Current installed-content contract, checked again before writes.
 * @param mutation Prepared replacement, consumed on success or failure.
 * @return True only when both inventory and quest state commit.
 */
[[nodiscard]] bool commit_quest_transition(const build_data::items::QuestTransition& transition,
                                           PendingQuestTransition& mutation) noexcept;

} // namespace sunrise::state
