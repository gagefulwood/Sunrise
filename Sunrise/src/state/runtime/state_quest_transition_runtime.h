#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../account/account_state.h"
#include "../build_data/items/quest_transition.h"
#include "../build_data/reward_sites/definition.h"
#include "../unlocks/definition.h"

namespace sunrise::state {

/** Inventory, condition inputs and the decoded contract captured before a stage replacement. */
struct PendingQuestTransition {
    CharacterState beforeCharacter{};
    CharacterState afterCharacter{};
    build_data::items::QuestTransition transition{};
    build_data::reward_sites::ItemProgression itemProgression{};
    build_data::reward_sites::CharacterObjectTransition characterObjectTransition{};
    std::array<std::int32_t, build_data::items::kQuestObjectiveCapacity> inputs{};
    std::uint64_t accountSoid{};
    std::uint64_t sourceInstanceSoid{};
    std::uint64_t successorInstanceSoid{};
    std::size_t characterIndex{};
    std::uint16_t sourceRow{};
    std::uint16_t successorRow{};
    bool prepared{};
};

/** Result of one event-driven search for a supported completed quest stage. */
enum class QuestCompletionPreparation : std::uint8_t {
    noWork,
    ready,
    retry,
};

/**
 * Finds the first supported owned stage completed by one expected selected character.
 * Call only from a concrete progression event; this function does not schedule or poll itself.
 * @param characterSoid Character that produced the progression event.
 * @param mutation Receives one complete Reward Site-backed replacement; cleared when none
 * qualifies.
 * @return Whether work is ready, absent, or should be retried after a store read failure.
 */
[[nodiscard]] QuestCompletionPreparation
prepare_completed_quest_transition(std::uint64_t characterSoid,
                                   PendingQuestTransition& mutation) noexcept;

/**
 * The caller supplies a decoded installed-content contract, never a client-authored plan.
 * @param sourceInstanceSoid Owned current-stage item to replace.
 * @param transition Validated content metadata for one non-final character quest stage.
 * @param mutation Receives the prepared replacement; cleared on failure.
 * @return False when ownership, conditions, saved progress or capacity prevent replacement.
 */
[[nodiscard]] bool prepare_quest_transition(std::uint64_t sourceInstanceSoid,
                                            const build_data::items::QuestTransition& transition,
                                            PendingQuestTransition& mutation) noexcept;

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
