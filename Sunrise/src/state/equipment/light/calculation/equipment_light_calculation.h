#pragma once

#include <span>

#include "../definition.h"

namespace sunrise::state::equipment::light::calculation {

/** One owned item already checked for class, equip-level requirements and its quality cap. */
struct EligibleRewardItem {
    account::inventory::EquipmentSlot slot{};
    std::int32_t power{};
    bool exotic{};
};

/**
 * Finds the strongest full loadout with at most one Exotic weapon and one Exotic armour item.
 * @param items Eligible powered gear from all supported ownership locations; no engram contents.
 * @param output Receives the floored eight-slot average, without Artifact bonus, on success.
 * @return False for invalid candidates or no complete legal loadout; output is unchanged.
 */
[[nodiscard]] bool reward_base(std::span<const EligibleRewardItem> items,
                               std::int32_t& output) noexcept;

/**
 * Computes raw summary arrays and the selected character's merged weighted light values.
 * @param selectedCharacter Raw scores for the selected character.
 * @param profileSlotMaxima Raw profile-owned maximum scores.
 * @param otherCharacterScores Raw score arrays for every other character.
 * @param output Destination replaced only after a complete valid calculation.
 * @return True when the signed total fits the summary's 32-bit fields.
 */
[[nodiscard]] bool evaluate(const SlotScores& selectedCharacter,
                            const SlotScores& profileSlotMaxima,
                            std::span<const SlotScores> otherCharacterScores,
                            Evaluation& output) noexcept;

} // namespace sunrise::state::equipment::light::calculation
