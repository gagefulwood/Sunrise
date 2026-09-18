#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>

#include "middleware/content/packages/tables/item_requirement_reader.h"

namespace {
namespace items = sunrise::middleware::content::packages::tables::items;
namespace expressions = sunrise::state::build_data::vendors;
using Source = items::EquipRequirementSource;

/** Synthetic buffers leave space for both native block layouts and three expression programs. */
constexpr std::size_t kBytes = 640;
using Blob = std::array<std::byte, kBytes>;
/** Native definition fields select equipping at 16 and the plug block at 64. */
constexpr std::size_t kItemField = 16, kPlugField = 64, kBlock = 80;
/** The plug equip group begins 208 bytes into the plug block. */
constexpr std::size_t kPlugGroup = kBlock + 208;
/** Synthetic array locations are disjoint from both native group descriptors. */
constexpr std::size_t kGroupHeader = 336, kPrograms = kGroupHeader + 16;
constexpr std::size_t kProgramStride = 16, kFirstProgramHeader = 416, kProgramSpacing = 64;
/** Serialized element types distinguish expression groups, instructions and identity rows. */
constexpr std::uint32_t kGroupClass = 0x80807D2FU, kInstructionClass = 0x80807D31U;
constexpr std::uint32_t kIdentityClass = 0x808074E8U, kFlagClass = 0x808074EBU;
/** Synthetic selectors use two flags and a distinct progression value slot. */
constexpr std::uint16_t kAllowed = 12, kLocked = 13, kValue = 14;
/** Identity fixtures hold two native 48-byte rows and separate flag arrays. */
constexpr std::size_t kIdentityHeader = 32, kIdentityRows = kIdentityHeader + 16;
constexpr std::size_t kIdentityStride = 48, kFlagsField = 24, kFlagsHeader = 176;

/** @param value Required condition. @param message Description on failure. */
void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::abort();
    }
}

/** @param blob Fixture bytes. @param at Offset. @param value Field to write. */
template <typename T> void put(Blob& blob, std::size_t at, T value) {
    check(at <= blob.size() && sizeof value <= blob.size() - at, "fixture write bounds");
    std::memcpy(blob.data() + at, &value, sizeof value);
}

/**
 * Writes a native array descriptor and matching header.
 * @param blob Fixture buffer.
 * @param at Descriptor offset.
 * @param header Header offset.
 * @param count Element count.
 * @param element Element type.
 */
void array(
    Blob& blob, std::size_t at, std::size_t header, std::uint64_t count, std::uint32_t element) {
    /** The synthetic marker has the native tag-class prefix. */
    constexpr std::uint32_t kMarker = 0x80800001U;
    put(blob, at, count);
    put(blob, at + 8, static_cast<std::int64_t>(header) - static_cast<std::int64_t>(at + 8));
    put(blob, header - sizeof kMarker, kMarker);
    put(blob, header, count);
    put(blob, header + sizeof count, element);
}

/** @return Base flag AND NOT lock, stored as two separate expression programs. */
Blob definition(Source source) {
    Blob blob{};
    const auto field = source == Source::item ? kItemField : kPlugField;
    const auto group = source == Source::item ? kBlock : kPlugGroup;
    put(blob, field, static_cast<std::int64_t>(kBlock - field));
    array(blob, group, kGroupHeader, 2, kGroupClass);
    array(blob, kPrograms, kFirstProgramHeader, 1, kInstructionClass);
    put(blob, kFirstProgramHeader + 16, std::uint32_t{1});
    put(blob, kFirstProgramHeader + 20, std::uint32_t{kAllowed});
    array(blob,
          kPrograms + kProgramStride,
          kFirstProgramHeader + kProgramSpacing,
          2,
          kInstructionClass);
    put(blob, kFirstProgramHeader + kProgramSpacing + 16, std::uint32_t{1});
    put(blob, kFirstProgramHeader + kProgramSpacing + 20, std::uint32_t{kLocked});
    put(blob, kFirstProgramHeader + kProgramSpacing + 24, std::uint32_t{2});
    return blob;
}

/** Known logical bytes and one explicitly unreadable flag, not a default-false state adapter. */
struct State {
    std::uint8_t allowed{expressions::kFlagActive};
    std::uint8_t locked{};
    bool lockKnown{true};
};

/** @param context Fixture state. @param slot Requested flag. @param output Logical byte. */
bool flag(void* context, std::uint16_t slot, std::uint8_t& output) noexcept {
    const auto& state = *static_cast<const State*>(context);
    if (slot == kAllowed) {
        output = state.allowed;
        return true;
    }
    if (slot == kLocked && state.lockKnown) {
        output = state.locked;
        return true;
    }
    return false;
}

/** @param context Unused. @param slot Requested value. @param output Known fixture value. */
bool value(void* context, std::uint16_t slot, std::int32_t& output) noexcept {
    (void)context;
    if (slot != kValue) {
        return false;
    }
    output = 0;
    return true;
}

/** Checks both block routes, AND semantics and refusal before NOT. */
void verify_groups() {
    for (const auto source : {Source::item, Source::installedPlug}) {
        auto blob = definition(source);
        State state{};
        const expressions::Inputs inputs{flag, value, &state};
        bool result = false;
        check(items::evaluate_equip_requirements(blob, source, inputs, result) && result,
              "all base or selected-plug conditions pass");
        state.locked = expressions::kFlagActive;
        check(items::evaluate_equip_requirements(blob, source, inputs, result) && !result,
              "second program is ANDed, not ignored");
        state.lockKnown = false;
        result = true;
        check(!items::evaluate_equip_requirements(blob, source, inputs, result) && !result,
              "unknown flag under NOT refuses the whole group");
        state.allowed = 0;
        check(!items::evaluate_equip_requirements(blob, source, inputs, result) && !result,
              "earlier false does not hide unreadable later program");
        state = {};
        const auto good = blob;
        for (const auto bad : {std::uint32_t{20}, std::uint32_t{257}}) {
            put(blob, kFirstProgramHeader + 16, bad);
            check(!items::evaluate_equip_requirements(blob, source, inputs, result) && !result,
                  "unsupported or narrowing opcode refused");
        }
        blob = good;
        put(blob, kFirstProgramHeader + 20, std::uint32_t{65536});
        check(!items::evaluate_equip_requirements(blob, source, inputs, result),
              "wide flag operand not narrowed");
        put(blob, kFirstProgramHeader + 16, std::uint32_t{11});
        check(!items::evaluate_equip_requirements(blob, source, inputs, result),
              "wide native literal not narrowed");
        blob = good;
        put(blob, kFirstProgramHeader + 8, kFlagClass);
        check(!items::evaluate_equip_requirements(blob, source, inputs, result),
              "wrong instruction element type refused");
        blob = good;
        put(blob, kPrograms, std::uint64_t{0});
        put(blob, kPrograms + 8, std::int64_t{0});
        check(!items::evaluate_equip_requirements(blob, source, inputs, result),
              "empty expression is not a satisfied group");
        blob = good;
        for (const auto relative : {(std::numeric_limits<std::int64_t>::min)(),
                                    (std::numeric_limits<std::int64_t>::max)()}) {
            put(blob, source == Source::item ? kItemField : kPlugField, relative);
            check(!items::evaluate_equip_requirements(blob, source, inputs, result),
                  "block pointer overflow refused");
        }
        blob = good;
        put(blob, kPrograms, (std::numeric_limits<std::uint64_t>::max)());
        check(!items::evaluate_equip_requirements(blob, source, inputs, result),
              "unbounded array count refused");
        blob = good;
        check(!items::evaluate_equip_requirements(
                  std::span(blob).first(kFirstProgramHeader + 20), source, inputs, result),
              "truncated program refused");
        const auto group = source == Source::item ? kBlock : kPlugGroup;
        put(blob, group, std::uint64_t{0});
        put(blob, group + 8, std::int64_t{0});
        check(items::evaluate_equip_requirements(blob, source, {}, result) && result,
              "empty group needs no state reader");
        blob = {};
        check(items::evaluate_equip_requirements(blob, source, {}, result) && result,
              "absent block needs no state reader");
        check(!items::evaluate_equip_requirements({}, source, {}, result) && !result,
              "missing block field is malformed, not absent");
    }
}

/** Checks identity selection, explicit fallthrough and unchanged output on failed reads. */
void verify_identity() {
    Blob blob{};
    array(blob, 8, kIdentityHeader, 2, kIdentityClass);
    put(blob, kIdentityRows, std::uint8_t{1});
    put(blob, kIdentityRows + 1, std::uint8_t{2});
    put(blob, kIdentityRows + kIdentityStride, std::uint8_t{2});
    put(blob, kIdentityRows + kIdentityStride + 1, std::uint8_t{2});
    array(blob, kIdentityRows + kFlagsField, kFlagsHeader, 1, kFlagClass);
    put(blob, kFlagsHeader + 16, kAllowed);
    std::uint8_t logical = 0;
    check(items::read_identity_flag(blob, 1, 2, kAllowed, logical)
              && logical == expressions::kFlagActive,
          "class and race select the identity flag list");
    check(items::read_identity_flag(blob, 2, 2, kAllowed, logical) && logical == 0,
          "other class's empty list falls through, not explicit false");
    check(items::read_identity_flag(blob, 1, 2, kLocked, logical) && logical == 0,
          "unlisted flag remains fallthrough");
    logical = expressions::kFlagActive;
    check(!items::read_identity_flag(blob, 1, 0, kAllowed, logical)
              && logical == expressions::kFlagActive,
          "missing race row refuses without changing output");
    const auto good = blob;
    put(blob, kIdentityRows + kIdentityStride, std::uint8_t{1});
    check(!items::read_identity_flag(blob, 1, 2, kAllowed, logical)
              && logical == expressions::kFlagActive,
          "duplicate identity rows refused");
    blob = good;
    put(blob, kFlagsHeader + 8, kInstructionClass);
    check(!items::read_identity_flag(blob, 1, 2, kAllowed, logical),
          "wrong flag-list type refused");
    blob = good;
    check(!items::read_identity_flag(
              std::span(blob).first(kFlagsHeader + 17), 1, 2, kAllowed, logical),
          "truncated flag slot refused");
}

/** Retained-content checks isolate the identity layer; all other sources are synthetic zero. */
struct IdentityInputs {
    std::span<const std::byte> table;
    std::uint8_t characterClass{};
    std::uint8_t race{};
};

/** @param context Identity fixture. @param slot Flag slot. @param logical Layer result. */
bool identity_flag(void* context, std::uint16_t slot, std::uint8_t& logical) noexcept {
    const auto& identity = *static_cast<const IdentityInputs*>(context);
    return items::read_identity_flag(
        identity.table, identity.characterClass, identity.race, slot, logical);
}
} // namespace

/** Runs the reader/evaluator boundary tests without installed content or a save. */
void verify_item_requirements() {
    verify_groups();
    verify_identity();
    std::puts(
        "PASS: equip groups, selected-plug groups, unknown refusal and identity-layer reader");
}

/**
 * Tests a retained class-specific definition against all nine identity selectors.
 * @param definition Whole serialized gear definition.
 * @param table Content identity table.
 * @param matchingClass Expected matching class, or 3 for class-neutral gear.
 */
void verify_retained_requirements(std::span<const std::byte> definition,
                                  std::span<const std::byte> table,
                                  std::uint8_t matchingClass) {
    /** Native playable classes/races are zero through two; class 3 denotes neutral gear. */
    constexpr std::uint8_t kIdentityCount = 3;
    check(matchingClass <= kIdentityCount, "fixture class range");
    for (std::uint8_t characterClass = 0; characterClass < kIdentityCount; ++characterClass) {
        for (std::uint8_t race = 0; race < kIdentityCount; ++race) {
            IdentityInputs identity{table, characterClass, race};
            const expressions::Inputs inputs{identity_flag, value, &identity};
            bool satisfied = false;
            check(
                items::evaluate_equip_requirements(definition, Source::item, inputs, satisfied)
                    && satisfied
                           == (matchingClass == kIdentityCount || matchingClass == characterClass),
                "retained gear matches expected class under isolated identity inputs");
        }
    }
    std::puts("PASS: retained gear/identity join across nine class/race inputs (isolated layer)");
}
