#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "state/equipment/light/calculation/equipment_light_calculation.h"

namespace {
namespace calculation = sunrise::state::equipment::light::calculation;
using Slot = sunrise::state::account::inventory::EquipmentSlot;
using Item = calculation::EligibleRewardItem;
/** Three weapons and five armour pieces make one complete reward loadout. */
constexpr std::size_t kSlots = 8;
/** Fixtures use two possible candidates per slot, one ordinary and one Exotic. */
constexpr std::size_t kCandidates = kSlots * 2;
/** The first three semantic slots are weapons; remaining powered slots are armour. */
constexpr unsigned kWeaponMask = 0b111;
/** Synthetic base and upgrade values make incorrect per-slot maxima visible. */
constexpr std::int32_t kBase = 1000, kUpgrade = 1080;

/** @param value Required condition. @param message Description on failure. */
void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::abort();
    }
}

/** @return One non-Exotic fixture item per powered slot. */
std::array<Item, kSlots> ordinary_items() {
    std::array<Item, kSlots> result{};
    for (std::size_t slot = 0; slot < result.size(); ++slot) {
        result[slot] = {static_cast<Slot>(slot), kBase, false};
    }
    return result;
}

/** Checks complete groups, legal Exotic choices, output rounding and invalid-input refusal. */
void verify_examples() {
    auto ordinary = ordinary_items();
    std::int32_t result = -1;
    check(calculation::reward_base(ordinary, result) && result == kBase, "ordinary loadout");
    ++ordinary[0].power;
    check(calculation::reward_base(ordinary, result) && result == kBase, "floor partial average");
    for (auto& item : ordinary) {
        item.power = kBase + 3;
    }
    check(calculation::reward_base(ordinary, result) && result == kBase + 3, "exact Power units");

    std::array<Item, kCandidates> candidates{};
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        candidates[slot] = {static_cast<Slot>(slot), kBase, false};
        candidates[kSlots + slot] = {static_cast<Slot>(slot), kUpgrade, true};
    }
    check(calculation::reward_base(candidates, result)
              && result == (kBase * 6 + kUpgrade * 2) / static_cast<std::int32_t>(kSlots),
          "one Exotic weapon and one Exotic armour, not eight Exotics");
    for (std::size_t slot = kSlots; slot < candidates.size(); ++slot) {
        candidates[slot].power = kBase - 1;
    }
    check(calculation::reward_base(candidates, result) && result == kBase,
          "weaker Exotics are optional");

    ordinary = ordinary_items();
    ordinary[0].exotic = true;
    check(calculation::reward_base(ordinary, result) && result == kBase,
          "one required Exotic fills a missing ordinary slot");
    ordinary[1].exotic = true;
    check(!calculation::reward_base(ordinary, result) && result == kBase,
          "two required Exotic weapons cannot form a legal loadout");
    check(!calculation::reward_base(std::span(ordinary).first(kSlots - 1), result)
              && result == kBase,
          "missing slot never shrinks the divisor");
    check(!calculation::reward_base({}, result) && result == kBase, "empty input refused");
    ordinary = ordinary_items();
    ordinary[0].slot = Slot::artifact;
    check(!calculation::reward_base(ordinary, result) && result == kBase,
          "Artifact is not an eligible powered slot");
    ordinary[0] = {Slot::kinetic, 0, false};
    check(!calculation::reward_base(ordinary, result), "zero Power refused");
    ordinary[0].power = -1;
    check(!calculation::reward_base(ordinary, result), "negative Power refused");
    ordinary = ordinary_items();
    for (auto& item : ordinary) {
        item.power = (std::numeric_limits<std::int32_t>::max)();
    }
    check(calculation::reward_base(ordinary, result)
              && result == (std::numeric_limits<std::int32_t>::max)(),
          "wide totals cannot overflow before averaging");
}

/** Checks the group algorithm against all two-candidate loadouts in deterministic fixtures. */
void verify_exhaustive_choices() {
    /** Fixed fixture count and distinct strides vary which Exotic gives the largest gain. */
    constexpr std::size_t kFixtures = 64;
    constexpr std::size_t kOrdinaryStride = 11, kExoticStride = 17, kSlotStride = 7;
    constexpr std::size_t kVariation = 80;
    for (std::size_t fixture = 0; fixture < kFixtures; ++fixture) {
        std::array<Item, kCandidates> items{};
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            items[slot] = {static_cast<Slot>(slot),
                           kBase
                               + static_cast<std::int32_t>(
                                   (fixture * kOrdinaryStride + slot * kSlotStride) % kVariation),
                           false};
            items[kSlots + slot] = {
                static_cast<Slot>(slot),
                kBase + static_cast<std::int32_t>((fixture * kExoticStride + slot) % kVariation),
                true};
        }
        std::int64_t best = 0;
        for (unsigned choice = 0; choice < (1U << kSlots); ++choice) {
            if (std::popcount(choice & kWeaponMask) > 1
                || std::popcount(choice & ~kWeaponMask) > 1) {
                continue;
            }
            std::int64_t total = 0;
            for (std::size_t slot = 0; slot < kSlots; ++slot) {
                total += items[slot + ((choice & (1U << slot)) != 0 ? kSlots : 0)].power;
            }
            best = (std::max)(best, total);
        }
        std::int32_t actual = 0;
        check(calculation::reward_base(items, actual)
                  && actual == best / static_cast<std::int64_t>(kSlots),
              "matches exhaustive legal-loadout result");
        std::reverse(items.begin(), items.end());
        check(calculation::reward_base(items, actual)
                  && actual == best / static_cast<std::int64_t>(kSlots),
              "ownership traversal order does not affect Power");
    }
}
} // namespace

/** Runs without game data, a client process, or persistent state. */
int main() {
    verify_examples();
    verify_exhaustive_choices();
    std::puts(
        "PASS: reward base, legal Exotic groups, exact Power, refusal and exhaustive choices");
}
