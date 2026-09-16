#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../investment/investment.h"
#include "../../unlocks/definition.h"
#include "quest_initialization.h"

namespace sunrise::state::build_data::items {

/** The all-one reference means the quest metadata names no completion effect. */
inline constexpr std::uint16_t kUnavailableQuestCompletionEffect = 0xFFFFU;
/** The all-one item-table index cannot name a quest member. */
inline constexpr std::uint16_t kUnavailableQuestItemIndex = 0xFFFFU;
/** The reserved all-one index leaves this many usable 16-bit quest item indices. */
inline constexpr std::size_t kQuestItemIndexCapacity = kUnavailableQuestItemIndex;
/** Authored quest value slots fit the nonnegative half of a signed 16-bit mapping. */
inline constexpr std::uint16_t kQuestValueSlotLimit = kUnlockValueSlotLimit;
/** Transition evaluation accepts at most sixteen objective references. */
inline constexpr std::size_t kQuestObjectiveCapacity = 16;
/** Installed Power objectives read slot 462; live reconstruction uses selected-character Power. */
inline constexpr std::uint16_t kQuestCharacterPowerSlot = 462;

/** One supported objective requires an explicit value slot to reach a signed minimum. */
struct QuestPredicate {
    /** Earned counters and derived Power belong to the selected character, not global overrides. */
    enum class Input : std::uint8_t { family5, characterCounter, characterPower };
    std::uint16_t valueSlot{};
    std::int32_t minimumValue{};
    Input input{Input::family5};

    bool operator==(const QuestPredicate&) const = default;
};

/** Metadata needed to replace one completed non-final quest member. */
struct QuestTransition {
    std::uint16_t sourceItemIndex{};
    std::uint16_t successorItemIndex{};
    std::int32_t currentValue{};
    std::int32_t nextValue{};
    std::uint16_t valueRow{};
    std::array<QuestPredicate, kQuestObjectiveCapacity> objectives{};
    std::size_t objectiveCount{};
    std::uint16_t completionEffect{kUnavailableQuestCompletionEffect};

    bool operator==(const QuestTransition&) const = default;
};

/**
 * Checks the bounded mechanics fields without authorizing the completion effect.
 * @param quest Decoded non-final quest transition.
 * @return True when every field and unused objective slot has a supported shape.
 */
[[nodiscard]] constexpr bool valid(const QuestTransition& quest) noexcept {
    if (quest.sourceItemIndex == kUnavailableQuestItemIndex
        || quest.successorItemIndex == kUnavailableQuestItemIndex
        || quest.sourceItemIndex == quest.successorItemIndex
        || quest.currentValue == kUnsetQuestValue || quest.currentValue == kInvalidQuestInitialValue
        || quest.nextValue == kUnsetQuestValue || quest.nextValue == kInvalidQuestInitialValue
        || quest.currentValue == quest.nextValue
        || quest.valueRow >= unlocks::kCharacterObjectValueCapacity || quest.objectiveCount == 0
        || quest.objectiveCount > quest.objectives.size()) {
        return false;
    }
    for (std::size_t index = 0; index < quest.objectives.size(); ++index) {
        const QuestPredicate& predicate = quest.objectives[index];
        if ((index < quest.objectiveCount
             && (predicate.valueSlot >= kQuestValueSlotLimit
                 || (predicate.input != QuestPredicate::Input::family5
                     && predicate.input != QuestPredicate::Input::characterCounter
                     && predicate.input != QuestPredicate::Input::characterPower)
                 || (predicate.input == QuestPredicate::Input::characterPower
                     && predicate.valueSlot != kQuestCharacterPowerSlot)))
            || (index >= quest.objectiveCount && predicate != QuestPredicate{})) {
            return false;
        }
    }
    return true;
}

/** A first-stage Power gate shares its source value and bank row with quest initialization. */
struct QuestPowerGate {
    std::int32_t minimumPower{};
    std::int32_t successorValue{};
    std::uint16_t successorItemIndex{};
    std::uint16_t completionEffect{kUnavailableQuestCompletionEffect};

    bool operator==(const QuestPowerGate&) const = default;
};

/**
 * Empty gates are valid; live gates must start a character-owned quest with a distinct successor.
 * @param gate Installed single-Power gate, or empty.
 * @param initial First-stage value and character bank row.
 * @param sourceIndex Owning item-table index.
 * @return False for unsupported values, scope or item references.
 */
[[nodiscard]] constexpr bool valid(const QuestPowerGate& gate,
                                   const QuestInitialization& initial,
                                   std::uint16_t sourceIndex) noexcept {
    return gate == QuestPowerGate{}
           || (valid(initial) && initial.scope == QuestInitialization::Scope::character
               && gate.minimumPower > 0 && gate.successorValue != kUnsetQuestValue
               && gate.successorValue != kInvalidQuestInitialValue
               && gate.successorValue != initial.value && sourceIndex != kUnavailableQuestItemIndex
               && gate.successorItemIndex != kUnavailableQuestItemIndex
               && gate.successorItemIndex != sourceIndex);
}

/**
 * Expands a first-stage gate without trusting the seeded global Power override.
 * @param gate Nonempty installed Power gate.
 * @param initial First-stage saved value and row.
 * @param sourceIndex Owning item-table index.
 * @return Empty for an invalid gate, otherwise one character-Power predicate.
 */
[[nodiscard]] constexpr QuestTransition power_transition(const QuestPowerGate& gate,
                                                         const QuestInitialization& initial,
                                                         std::uint16_t sourceIndex) noexcept {
    if (gate == QuestPowerGate{} || !valid(gate, initial, sourceIndex)) {
        return {};
    }
    QuestTransition result{};
    result.sourceItemIndex = sourceIndex;
    result.successorItemIndex = gate.successorItemIndex;
    result.currentValue = initial.value;
    result.nextValue = gate.successorValue;
    result.valueRow = initial.row;
    result.objectives[0] = {
        kQuestCharacterPowerSlot, gate.minimumPower, QuestPredicate::Input::characterPower};
    result.objectiveCount = 1;
    result.completionEffect = gate.completionEffect;
    return result;
}

/**
 * Checks every objective against one unambiguous resolved input value.
 * @param quest Validated transition metadata; its completion effect is not an eligibility gate.
 * @param family Explicit values; the caller must resolve character counters for their owner.
 * @return False for malformed state, missing or duplicate values, or an unmet objective.
 */
[[nodiscard]] inline bool complete(const QuestTransition& quest,
                                   const Family5State& family) noexcept {
    if (!valid(quest) || family.valueCount > family.values.size()) {
        return false;
    }
    for (std::size_t objective = 0; objective < quest.objectiveCount; ++objective) {
        const QuestPredicate& predicate = quest.objectives[objective];
        const UnlockValueOverride* found = nullptr;
        for (std::size_t value = 0; value < family.valueCount; ++value) {
            if (family.values[value].slot != predicate.valueSlot) {
                continue;
            }
            if (found != nullptr) {
                return false;
            }
            found = &family.values[value];
        }
        if (found == nullptr || found->value < predicate.minimumValue) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::state::build_data::items
