#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "../../account/inventory/inventory_state.h"
#include "../../build_data/items/details/definition.h"

namespace sunrise::state::equipment::light {

/** One present item gives its definition identity and its runtime light score. */
struct ItemScore {
    std::uint16_t definitionIndex{};
    std::int32_t score{};

    [[nodiscard]] bool operator==(const ItemScore&) const noexcept = default;
};

/** An empty optional is the only absent-item marker in the 20 native equipment slots. */
using SlotScores =
    std::array<std::optional<ItemScore>, build_data::items::details::kEquipmentSlotCount>;

/** Item power comes from the item level, at 10 power per level. */
inline constexpr std::int32_t kPowerPerLevel = 10;
/** A powered item never scores below this floor whatever its level. */
inline constexpr std::int32_t kMinimumItemPower = 750;

/**
 * Converts normalized item levels to Power without overflowing the wire integer.
 * @param level Whole item level; zero is unpowered.
 * @param output Receives Power on success; unchanged on failure.
 * @param fraction Tenths of a level, each worth one Power above the floor.
 * @return False for invalid levels or Power outside the signed wire range.
 */
[[nodiscard]] constexpr bool
item_power(std::int32_t level, std::int32_t& output, std::uint8_t fraction = 0) noexcept {
    if (!account::inventory::valid_level(level, fraction)
        || level > ((std::numeric_limits<std::int32_t>::max)() - fraction) / kPowerPerLevel) {
        return false;
    }
    const std::int32_t power = kPowerPerLevel * level + fraction;
    output = level == 0 ? 0 : (power < kMinimumItemPower ? kMinimumItemPower : power);
    return true;
}

/** A slot at or below this score adds nothing and is not counted by the divisor. */
inline constexpr std::int32_t kUnpoweredScore = 0;

/** The raw arrays and totals an equipment-light summary needs. */
struct Evaluation {
    SlotScores profile{};
    SlotScores character{};
    std::int32_t divisor{};
    std::int32_t total{};
    /** Signed integer division cuts the fraction toward zero. */
    std::int32_t average{};
    /** Float division keeps the fractional average, for display later. */
    float averageFloat{};

    [[nodiscard]] bool operator==(const Evaluation&) const noexcept = default;
};

/** One final scalar keeps the integer average it came from. */
struct ScalarValue {
    std::int32_t average{};
    float value{};

    [[nodiscard]] bool operator==(const ScalarValue&) const noexcept = default;
};

} // namespace sunrise::state::equipment::light
