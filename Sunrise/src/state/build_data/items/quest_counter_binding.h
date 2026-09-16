#pragma once

#include <cstdint>

#include "../../investment/investment.h"
#include "quest_initialization.h"

namespace sunrise::state::build_data::items {

/** One counted objective is eligible only while its owned quest member is active. */
struct QuestCounterBinding {
    std::int32_t stageValue{};
    std::int32_t threshold{};
    std::uint16_t stageRow{};
    std::uint16_t valueSlot{};

    bool operator==(const QuestCounterBinding&) const = default;
};

/** @return True for an empty binding or a positive counter within the saved and native banks. */
[[nodiscard]] constexpr bool valid(const QuestCounterBinding& binding) noexcept {
    return binding == QuestCounterBinding{}
           || (binding.stageValue != kUnsetQuestValue
               && binding.stageValue != kInvalidQuestInitialValue && binding.threshold > 0
               && binding.stageRow < unlocks::kCharacterObjectValueCapacity
               && binding.valueSlot < kFamily5ValueSlotLimit);
}

} // namespace sunrise::state::build_data::items
