#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "middleware/content/packages/tables/items.h"
#include "state/build_data/cache/records/codec.h"
#include "state/equipment/light/calculation/equipment_light_calculation.h"
#include "state/equipment/light/resolution/configured_equipment_light_resolver.h"

namespace sunrise::state {
/** This equipment-only fixture has no seasonal Artifact bonus. */
std::uint16_t artifact_power_bonus() noexcept {
    return 0;
}
} // namespace sunrise::state

namespace sunrise::state::build_data {
/** Synthetic lookup keeps the real Power resolver independent of installed content. */
bool find_item_definition_hash(std::uint32_t hash, items::Definition& definition) noexcept {
    definition = {};
    definition.definitionHash = hash;
    return true;
}
/** One fixture helmet carries a quality cap below its stored item level. */
bool find_configured_item_detail(std::uint16_t index,
                                 items::details::Definition& definition) noexcept {
    /** Native equipment slot 1 is the helmet; the fixture cap is 1060 Power. */
    constexpr std::int8_t kHelmetSlot = 1;
    constexpr float kLevelCap = 106;
    definition = {};
    definition.definitionIndex = index;
    definition.equipmentSlot = kHelmetSlot;
    definition.levelCap = kLevelCap;
    return true;
}
} // namespace sunrise::state::build_data

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

/**
 * Writes a fixture field without relying on alignment or C++ structure padding.
 * @tparam Value Field type.
 * @param bytes Fixture buffer.
 * @param offset Byte offset of the field.
 * @param value Field value.
 */
template <typename Value> void put(std::span<std::byte> bytes, std::size_t offset, Value value) {
    check(offset <= bytes.size() && sizeof value <= bytes.size() - offset, "fixture bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

/**
 * Builds a serialized descriptor and its separate native array header.
 * @param bytes Fixture buffer.
 * @param descriptor Count/relative pair offset.
 * @param header Array header offset, preceded by its class marker.
 * @param count Number of elements.
 */
void array_header(std::span<std::byte> bytes,
                  std::size_t descriptor,
                  std::size_t header,
                  std::uint64_t count) {
    /** Definition-array headers use tag-class words; these synthetic classes carry no policy. */
    constexpr std::uint32_t kMarker = 0x80800001U, kElement = 0x80800002U;
    /** A descriptor's relative field starts eight bytes after its count. */
    constexpr std::size_t kRelativeOffset = 8;
    put(bytes, descriptor, count);
    put(bytes,
        descriptor + kRelativeOffset,
        static_cast<std::int64_t>(header - descriptor - kRelativeOffset));
    put(bytes, header - sizeof kMarker, kMarker);
    put(bytes, header, count);
    put(bytes, header + sizeof count, kElement);
}

/** Checks content selection, malformed-input refusal, cap persistence and effective Power. */
void verify_quality_caps() {
    namespace items = sunrise::middleware::content::packages::tables::items;
    namespace cache = sunrise::state::build_data::cache::records;
    namespace details = sunrise::state::build_data::items::details;
    namespace light = sunrise::state::equipment::light;
    /** Synthetic layouts retain the native block/descriptor offsets and row widths. */
    constexpr std::size_t kQualityField = 72, kQualityBlock = 80, kVersions = kQualityBlock + 96;
    constexpr std::size_t kVersionHeader = 208, kVersionData = kVersionHeader + 16;
    constexpr std::size_t kCapDescriptor = 8, kCapHeader = 32, kCapData = kCapHeader + 16;
    constexpr std::size_t kCapStride = 8, kCapValue = 4;
    /** Two versions select opposite rows so using the item index or last version fails. */
    constexpr std::uint64_t kRows = 2;
    constexpr float kEarlierCap = 101, kSelectedCap = 106;
    /** Levels above and below the selected cap exercise both fractional paths. */
    constexpr std::int32_t kAboveCapLevel = 110, kExpectedPower = 1060;
    constexpr std::uint8_t kAboveFraction = 7, kBelowFraction = 3;
    constexpr std::int32_t kBelowCapLevel = 100, kBelowPower = 1003, kUncappedPower = 1107;
    /** The item-definition fixture ends after its native 240-byte fixed record. */
    constexpr std::size_t kDefinitionBytes = 240;
    std::array<std::byte, kDefinitionBytes> definition{};
    std::array<std::byte, kCapData + kRows * kCapStride> caps{};
    put(definition, kQualityField, std::int64_t{kQualityBlock - kQualityField});
    array_header(definition, kVersions, kVersionHeader, kRows);
    put(definition, kVersionData, std::int16_t{1});
    array_header(caps, kCapDescriptor, kCapHeader, kRows);
    put(caps, kCapData + kCapValue, kEarlierCap);
    put(caps, kCapData + kCapStride + kCapValue, kSelectedCap);
    float result = 0;
    check(items::read_level_cap(definition, caps, result) && result == kSelectedCap,
          "first quality version selects its own cap row");
    const auto goodDefinition = definition;
    const auto goodCaps = caps;
    for (const auto bad : {std::int16_t{-1}, static_cast<std::int16_t>(kRows)}) {
        put(definition, kVersionData, bad);
        check(!items::read_level_cap(definition, caps, result) && result == kSelectedCap,
              "bad cap index leaves output unchanged");
    }
    definition = goodDefinition;
    for (const float bad : {0.F,
                            -1.F,
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        put(caps, kCapData + kCapStride + kCapValue, bad);
        check(!items::read_level_cap(definition, caps, result) && result == kSelectedCap,
              "invalid authored cap is not treated as absent");
    }
    caps = goodCaps;
    for (const auto relative :
         {(std::numeric_limits<std::int64_t>::min)(), (std::numeric_limits<std::int64_t>::max)()}) {
        put(definition, kQualityField, relative);
        check(!items::read_level_cap(definition, caps, result), "relative overflow refused");
    }
    definition = goodDefinition;
    check(!items::read_level_cap(std::span(definition).first(kVersionData + 1), caps, result),
          "truncated version array refused");
    check(!items::read_level_cap(definition, std::span(caps).first(caps.size() - 1), result),
          "truncated cap rows refused");
    put(definition, kVersions, std::uint64_t{0});
    put(definition, kVersions + sizeof(std::uint64_t), std::int64_t{0});
    check(items::read_level_cap(definition, {}, result) && result == 0,
          "empty version array needs no cap table");
    put(definition, kQualityField, std::int64_t{0});
    check(items::read_level_cap(definition, {}, result) && result == 0,
          "absent quality block has no cap");

    details::Definition detail{};
    detail.levelCap = kSelectedCap;
    cache::ItemDetailRecord record{};
    details::Definition restored{};
    check(cache::encode(detail, record) && cache::decode(record, restored)
              && restored.levelCap == kSelectedCap,
          "build-data cache preserves cap");
    std::int32_t power = 0;
    check(light::item_power(kAboveCapLevel, power, kAboveFraction, restored.levelCap)
              && power == kExpectedPower,
          "quality cap limits whole and fractional Power");
    sunrise::state::AccountState account{};
    account.characterCount = 1;
    account.characters[0].selected = true;
    sunrise::state::account::inventory::Item equipped{};
    equipped.level = kAboveCapLevel;
    equipped.levelFraction = kAboveFraction;
    account.characters[0].equipment.slots[static_cast<std::size_t>(Slot::helmet)] = equipped;
    light::Evaluation evaluation{};
    check(light::resolution::resolve(account, 0, evaluation)
              && evaluation.average == kExpectedPower,
          "production equipment resolver applies the content cap");
    check(light::item_power(kBelowCapLevel, power, kBelowFraction, restored.levelCap)
              && power == kBelowPower,
          "below-cap fractional Power unchanged");
    check(light::item_power(0, power, 0, restored.levelCap) && power == 0,
          "cap never powers an unpowered item");
    check(light::item_power(1, power, 0, restored.levelCap) && power == light::kMinimumItemPower,
          "cap preserves the curve floor");
    check(light::item_power(kAboveCapLevel, power, kAboveFraction, 0) && power == kUncappedPower,
          "absent cap preserves prior behavior");
    for (const float bad :
         {-1.F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        detail.levelCap = bad;
        record.levelCap = bad;
        check(!cache::encode(detail, record), "invalid cap refused by cache writer");
        check(!cache::decode(record, restored), "invalid cap refused by cache reader");
        check(!light::item_power(kAboveCapLevel, power, 0, bad) && power == kUncappedPower,
              "invalid cap leaves Power output unchanged");
    }
}

/**
 * Loads an optional local fixture without bundling content in the repository.
 * @param path User-supplied fixture path.
 * @return Bytes from a nonempty file no larger than one MiB.
 */
std::vector<std::byte> fixture(const char* path) {
    /** Definition fixtures are small; reject accidental large-file inputs. */
    constexpr long kMaximumBytes = 1024 * 1024;
    std::FILE* input = nullptr;
    check(fopen_s(&input, path, "rb") == 0, "fixture opens");
    check(std::fseek(input, 0, SEEK_END) == 0, "fixture seek");
    const auto size = std::ftell(input);
    check(size > 0 && size <= kMaximumBytes, "fixture size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::rewind(input);
    const auto count = std::fread(bytes.data(), 1, bytes.size(), input);
    const auto closed = std::fclose(input);
    check(count == bytes.size() && closed == 0, "fixture read and close");
    return bytes;
}
} // namespace

/**
 * Runs synthetic checks and optionally checks one retained content join.
 * @param argc Argument count, one or four.
 * @param argv Optional item blob, cap table and expected level cap.
 * @return Zero after every check passes.
 */
int main(int argc, char** argv) {
    verify_examples();
    verify_exhaustive_choices();
    verify_quality_caps();
    check(argc == 1 || argc == 4, "optional arguments: item cap-table expected-level-cap");
    if (argc == 4) {
        float cap = 0;
        check(sunrise::middleware::content::packages::tables::items::read_level_cap(
                  fixture(argv[1]), fixture(argv[2]), cap)
                  && cap == std::strtof(argv[3], nullptr),
              "retained item resolves its expected quality cap");
        std::puts("PASS: retained item quality-cap join");
    }
    std::puts(
        "PASS: reward base, legal loadouts, quality caps, cache roundtrip and malformed inputs");
}
