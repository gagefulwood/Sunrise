#include "equipment_light_calculation.h"

#include <algorithm>
#include <array>
#include <limits>

namespace sunrise::state::equipment::light::calculation {
namespace {

/** Reward Power averages three weapon slots and five armour slots. */
constexpr std::size_t kRewardSlotCount =
    static_cast<std::size_t>(account::inventory::EquipmentSlot::classItem) + 1;
/** Weapon and armour Exotic limits apply independently. */
constexpr std::size_t kRewardWeaponCount =
    static_cast<std::size_t>(account::inventory::EquipmentSlot::heavy) + 1;

/**
 * Only one slot in a weapon or armour group may use its Exotic candidate.
 * @param ordinary Best non-Exotic Power per slot; zero means absent.
 * @param exotic Best Exotic Power per slot, with the same extent as ordinary.
 * @return Highest complete group total, or zero when no legal group exists.
 */
[[nodiscard]] std::int64_t best_reward_group(std::span<const std::int32_t> ordinary,
                                             std::span<const std::int32_t> exotic) noexcept {
    std::int64_t best = 0;
    // The final choice uses no Exotic, including when every Exotic is weaker.
    for (std::size_t selected = 0; selected <= ordinary.size(); ++selected) {
        std::int64_t total = 0;
        for (std::size_t slot = 0; slot < ordinary.size(); ++slot) {
            const std::int32_t power = slot == selected ? exotic[slot] : ordinary[slot];
            if (power == 0) {
                total = 0;
                break;
            }
            total += power;
        }
        best = (std::max)(best, total);
    }
    return best;
}

/**
 * Applies strict score upgrades from one profile or other-character source.
 * @param selectedCharacter Selected scores that define which slots may be upgraded.
 * @param candidate Candidate scores from one independent ownership source.
 * @param aggregate Selected-seeded values updated in place.
 */
void merge_strict_upgrades(const SlotScores& selectedCharacter,
                           const SlotScores& candidate,
                           SlotScores& aggregate) noexcept {
    for (std::size_t slotIndex = 0; slotIndex < aggregate.size(); ++slotIndex) {
        // Candidates cannot create a slot absent from the selected character.
        if (!selectedCharacter[slotIndex].has_value() || !candidate[slotIndex].has_value()) {
            continue;
        }

        // A strict compare keeps the selected item winning when scores tie.
        if (candidate[slotIndex]->score > aggregate[slotIndex]->score) {
            aggregate[slotIndex] = candidate[slotIndex];
        }
    }
}

/**
 * Builds the private score set used only for the weighted aggregate.
 * @param selectedCharacter Selected scores that define eligible slots and tie ownership.
 * @param profileSlotMaxima Profile-owned candidates applied before other characters.
 * @param otherCharacterScores Other-character candidates in stable account order.
 * @return Selected-seeded scores with every strict upgrade applied.
 */
[[nodiscard]] SlotScores
merge_aggregate(const SlotScores& selectedCharacter,
                const SlotScores& profileSlotMaxima,
                const std::span<const SlotScores> otherCharacterScores) noexcept {
    SlotScores aggregate = selectedCharacter;
    merge_strict_upgrades(selectedCharacter, profileSlotMaxima, aggregate);
    for (const SlotScores& otherCharacter : otherCharacterScores) {
        merge_strict_upgrades(selectedCharacter, otherCharacter, aggregate);
    }
    return aggregate;
}

/** Adds one present score. An absent slot must add zero. */
void add_slot_score(const SlotScores& scores,
                    const std::size_t slotIndex,
                    std::int64_t& total) noexcept {
    if (scores[slotIndex].has_value()) {
        total += scores[slotIndex]->score;
    }
}

/**
 * Sums every slot score in 64-bit storage.
 * The native producer totals all 20 slots, so an unpowered slot adds its zero rather than being
 * left out.
 * @param aggregate Selected-compatible maximum scores.
 * @return Signed wide total before the summary-field range check.
 */
[[nodiscard]] std::int64_t slot_total(const SlotScores& aggregate) noexcept {
    std::int64_t total = 0;
    for (std::size_t slotIndex = 0; slotIndex < aggregate.size(); ++slotIndex) {
        add_slot_score(aggregate, slotIndex, total);
    }
    return total;
}

/** @param value Signed wide total. @return True when the native summary field can store it. */
[[nodiscard]] bool fits_summary_integer(const std::int64_t value) noexcept {
    return value >= std::numeric_limits<std::int32_t>::min()
           && value <= std::numeric_limits<std::int32_t>::max();
}

/** @param aggregate Merged slot scores. @return How many slots carry a positive score. */
[[nodiscard]] std::int32_t divisor_for(const SlotScores& aggregate) noexcept {
    std::int32_t counted = 0;
    for (const std::optional<ItemScore>& score : aggregate) {
        if (score.has_value() && score->score > kUnpoweredScore) {
            ++counted;
        }
    }
    return counted;
}

} // namespace

/**
 * Computes reward base from eligible gear without changing displayed equipment Power.
 * @param items Owned, eligible, quality-capped gear; Artifact and engrams are excluded.
 * @param output Receives the floored eight-slot average only on success.
 * @return False for invalid candidates or no complete loadout within both Exotic limits.
 */
bool reward_base(std::span<const EligibleRewardItem> items, std::int32_t& output) noexcept {
    std::array<std::int32_t, kRewardSlotCount> ordinary{}, exotic{};
    for (const EligibleRewardItem& item : items) {
        const auto slot = static_cast<std::size_t>(item.slot);
        if (slot >= ordinary.size() || item.power <= 0) {
            return false;
        }
        auto& highest = item.exotic ? exotic[slot] : ordinary[slot];
        highest = (std::max)(highest, item.power);
    }
    const auto weapons = best_reward_group(std::span(ordinary).first(kRewardWeaponCount),
                                           std::span(exotic).first(kRewardWeaponCount));
    const auto armour = best_reward_group(std::span(ordinary).subspan(kRewardWeaponCount),
                                          std::span(exotic).subspan(kRewardWeaponCount));
    if (weapons == 0 || armour == 0) {
        return false;
    }
    output =
        static_cast<std::int32_t>((weapons + armour) / static_cast<std::int64_t>(kRewardSlotCount));
    return true;
}

/** Computes raw summary arrays and the selected character's merged weighted light values. */
bool evaluate(const SlotScores& selectedCharacter,
              const SlotScores& profileSlotMaxima,
              std::span<const SlotScores> otherCharacterScores,
              Evaluation& output) noexcept {
    const SlotScores aggregate =
        merge_aggregate(selectedCharacter, profileSlotMaxima, otherCharacterScores);
    const std::int64_t wideTotal = slot_total(aggregate);
    if (!fits_summary_integer(wideTotal)) {
        return false;
    }

    Evaluation candidate;
    candidate.profile = profileSlotMaxima;
    candidate.character = selectedCharacter;
    candidate.divisor = divisor_for(aggregate);
    candidate.total = static_cast<std::int32_t>(wideTotal);
    // A character wearing nothing powered has no average to take, and dividing would trap.
    candidate.average = candidate.divisor == 0 ? 0 : candidate.total / candidate.divisor;
    candidate.averageFloat = candidate.divisor == 0 ? 0.0F
                                                    : static_cast<float>(candidate.total)
                                                          / static_cast<float>(candidate.divisor);
    output = candidate;
    return true;
}

} // namespace sunrise::state::equipment::light::calculation
