#include "item_requirement_reader.h"

#include <array>
#include <cstring>
#include <limits>

#include "definition_index_table.h"

namespace sunrise::middleware::content::packages::tables::items {
namespace {

namespace expressions = state::build_data::vendors;

/** The base item's self-relative equipping block starts with its expression group. */
constexpr std::size_t kEquippingBlockField = 0x10;
/** Plug definitions point to their plug block from this item field. */
constexpr std::size_t kPlugBlockField = 0x40;
/** A plug's equip group is separate from its insertion and enabled requirements. */
constexpr std::size_t kPlugEquipGroupOffset = 0xD0;
/** Equip groups contain expression descriptors, each with the serialized descriptor width. */
constexpr std::uint32_t kEquipExpressionClass = 0x80807D2FU;
/** Expression programs contain eight-byte opcode/operand records. */
constexpr std::uint32_t kExpressionInstructionClass = 0x80807D31U;
/** Identity defaults are selected from 48-byte class/race rows. */
constexpr std::uint32_t kIdentityRowClass = 0x808074E8U;
constexpr std::size_t kIdentityRowStride = 48;
/** Race follows the class byte; the row's flag-list descriptor starts at byte 24. */
constexpr std::size_t kIdentityRaceOffset = 1;
constexpr std::size_t kIdentityFlagsOffset = 24;
/** Each identity flag-list member is one unsigned 16-bit slot. */
constexpr std::uint32_t kIdentityFlagClass = 0x808074EBU;

/** @param blob Source bytes. @param offset Field offset. @param value Receives the field. */
template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || sizeof value > blob.size() - offset) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

/**
 * Checks the element type and full extent before walking a serialized array.
 * @param blob Whole serialized definition.
 * @param descriptor Offset of the count/relative pair.
 * @param stride Serialized element size.
 * @param elementClass Required element class for nonempty arrays.
 * @param array Receives the checked view.
 * @return False for a malformed descriptor, wrong type or truncated payload.
 */
[[nodiscard]] bool checked_array(std::span<const std::byte> blob,
                                 std::size_t descriptor,
                                 std::size_t stride,
                                 std::uint32_t elementClass,
                                 Array& array) noexcept {
    return find_optional_array_at(blob, descriptor, array)
           && (array.count == 0 || array.elementClass == elementClass)
           && array.dataOffset <= blob.size()
           && array.count <= (blob.size() - array.dataOffset) / stride;
}

/**
 * Decodes one bounded program without narrowing unknown opcodes or wide operands.
 * @param definition Whole serialized definition.
 * @param descriptor Expression descriptor offset.
 * @param group Staged group to append to; use only after the whole group succeeds.
 * @return False for unsupported content or insufficient remaining capacity.
 */
[[nodiscard]] bool read_program(std::span<const std::byte> definition,
                                std::size_t descriptor,
                                expressions::ExpressionGroup& group) noexcept {
    Array array{};
    if (!checked_array(
            definition, descriptor, kUnlockInstructionStride, kExpressionInstructionClass, array)
        || array.count == 0 || array.count > group.instructions.size() - group.count) {
        return false;
    }
    for (std::size_t index = 0; index < array.count; ++index) {
        const auto at = array.dataOffset + index * kUnlockInstructionStride;
        std::uint32_t opcode = 0, operand = 0;
        if (!read(definition, at, opcode)
            || !read(definition, at + kUnlockInstructionOperandOffset, operand)
            || opcode > (std::numeric_limits<std::uint8_t>::max)()) {
            return false;
        }
        const auto operation = static_cast<expressions::Opcode>(opcode);
        switch (operation) {
        case expressions::Opcode::flag:
        case expressions::Opcode::loadValue:
        case expressions::Opcode::constant:
            // The existing evaluator accepts 16-bit operands; never truncate a native constant.
            if (operand > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            break;
        case expressions::Opcode::logicalNot:
        case expressions::Opcode::lessThan:
            operand = 0;
            break;
        default:
            return false;
        }
        group.instructions[group.count++] = {operation, static_cast<std::uint16_t>(operand)};
    }
    group.ends[group.count - 1] = true;
    return true;
}

} // namespace

/**
 * Decodes a whole group without publishing partial requirements.
 * @param definition Whole base-item or selected-plug definition.
 * @param source Which definition block holds the requirements.
 * @param output Receives the complete group; reset to unavailable on refusal.
 * @return True only when every expression fits the supported representation.
 */
bool read_equip_requirements(std::span<const std::byte> definition,
                             EquipRequirementSource source,
                             expressions::ExpressionGroup& output) noexcept {
    output = {};
    expressions::ExpressionGroup staged{};
    std::size_t field = 0, groupOffset = 0;
    switch (source) {
    case EquipRequirementSource::item:
        field = kEquippingBlockField;
        break;
    case EquipRequirementSource::installedPlug:
        field = kPlugBlockField;
        groupOffset = kPlugEquipGroupOffset;
        break;
    default:
        return false;
    }
    std::int64_t relative = 0;
    if (!read(definition, field, relative)) {
        return false;
    }
    if (relative == 0) {
        output.available = true;
        return true;
    }
    if (relative < -static_cast<std::int64_t>(field)
        || (relative > 0 && static_cast<std::uint64_t>(relative) > definition.size() - field)) {
        return false;
    }
    const auto block = static_cast<std::size_t>(static_cast<std::int64_t>(field) + relative);
    if (groupOffset > definition.size() - block) {
        return false;
    }
    Array group{};
    if (!checked_array(definition,
                       block + groupOffset,
                       kUnlockExpressionFieldSize,
                       kEquipExpressionClass,
                       group)) {
        return false;
    }
    for (std::size_t index = 0; index < group.count; ++index) {
        if (!read_program(
                definition, group.dataOffset + index * kUnlockExpressionFieldSize, staged)) {
            return false;
        }
    }
    staged.available = true;
    output = staged;
    return true;
}

/**
 * Evaluates decoded content through the same group evaluator used by cached State details.
 * @param definition Whole base-item or selected-plug definition.
 * @param source Which definition block holds the requirements.
 * @param inputs Fully resolved server inputs, with unknown reads refused.
 * @param satisfied Receives the complete AND result; cleared on refusal.
 * @return True only when the group was decoded and every expression evaluated.
 */
bool evaluate_equip_requirements(std::span<const std::byte> definition,
                                 EquipRequirementSource source,
                                 const expressions::Inputs& inputs,
                                 bool& satisfied) noexcept {
    satisfied = false;
    expressions::ExpressionGroup group{};
    return read_equip_requirements(definition, source, group)
           && expressions::evaluate(group, inputs, satisfied);
}

/**
 * Reads only identity defaults; zero must still fall through to lower-priority flag sources.
 * @param table Whole content identity table.
 * @param characterClass Class selector from the character identity.
 * @param race Race selector from the character identity.
 * @param flag Requested unlock flag slot.
 * @param logical Receives active or fallthrough only on success.
 * @return False for malformed lists or anything other than one matching identity row.
 */
bool read_identity_flag(std::span<const std::byte> table,
                        std::uint8_t characterClass,
                        std::uint8_t race,
                        std::uint16_t flag,
                        std::uint8_t& logical) noexcept {
    Array rows{};
    if (!checked_array(table, kTableArrayDescriptor, kIdentityRowStride, kIdentityRowClass, rows)) {
        return false;
    }
    bool found = false;
    std::uint8_t staged = 0;
    for (std::size_t index = 0; index < rows.count; ++index) {
        const auto at = rows.dataOffset + index * kIdentityRowStride;
        std::uint8_t rowClass = 0, rowRace = 0;
        if (!read(table, at, rowClass) || !read(table, at + kIdentityRaceOffset, rowRace)) {
            return false;
        }
        if (rowClass != characterClass || rowRace != race) {
            continue;
        }
        Array flags{};
        if (found
            || !checked_array(table,
                              at + kIdentityFlagsOffset,
                              sizeof(std::uint16_t),
                              kIdentityFlagClass,
                              flags)) {
            return false;
        }
        found = true;
        for (std::size_t entry = 0; entry < flags.count; ++entry) {
            std::uint16_t slot = 0;
            if (!read(table, flags.dataOffset + entry * sizeof slot, slot)) {
                return false;
            }
            if (slot == flag) {
                staged = expressions::kFlagActive;
            }
        }
    }
    if (!found) {
        return false;
    }
    logical = staged;
    return true;
}

} // namespace sunrise::middleware::content::packages::tables::items
