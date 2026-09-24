#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "middleware/content/packages/tables/unlock_opcode.h"

namespace sunrise::state::build_data::vendors {

/** Stack slots one expression may use. Known gates need two, so this leaves headroom. */
inline constexpr std::size_t kExpressionStackCapacity = 16;
/** The same 128-instruction safety bound used for installed node expressions. */
inline constexpr std::size_t kVendorProgramCapacity = 128;

/** Vendor gates use the shared native unlock instruction values. */
using Opcode = middleware::content::packages::tables::UnlockOpcode;

/** A flag is active only for this logical byte. Logical 0 and 1 are both inactive. */
inline constexpr std::uint8_t kFlagActive = 2;

/** One native postfix instruction; unused operands may carry a negative sentinel. */
struct Instruction {
    Opcode opcode{};
    std::int32_t operand{};
};

/** One bounded native postfix program; an empty authored field has count zero. */
struct Program {
    std::array<Instruction, kVendorProgramCapacity> instructions{};
    std::size_t count{};
};

/**
 * Reads an interaction's direct native instruction array.
 * @param blob Whole installed vendor definition.
 * @param field Offset of the instruction-array descriptor.
 * @param output Receives the program, or an empty program on failure.
 * @return True when the field is empty or its instructions fit the bounded program.
 */
[[nodiscard]] bool
read_program(std::span<const std::byte> blob, std::size_t field, Program& output) noexcept;

/**
 * Reads a sale's list of native programs, joining them with postfix AND.
 * @param blob Whole installed vendor definition.
 * @param field Offset of the program-list descriptor.
 * @param output Receives the joined program, or an empty program on failure.
 * @return True when the field is empty or every listed program is nonempty and fits.
 */
[[nodiscard]] bool
read_program_list(std::span<const std::byte> blob, std::size_t field, Program& output) noexcept;

/** Reads one flag's logical byte. Returns false when the slot has no value. */
using FlagReader = bool (*)(void* context, std::uint16_t slot, std::uint8_t& logical) noexcept;
/** Reads one progression value. Returns false when the slot has no value. */
using ValueReader = bool (*)(void* context, std::uint16_t slot, std::int32_t& value) noexcept;

/** The two readers one evaluation needs, and the caller state they share. */
struct Inputs {
    FlagReader flag{};
    ValueReader value{};
    void* context{};
};

/**
 * Evaluates one expression program.
 * Every failure is refused, never defaulted: bad opcode, bad stack, bad kind, unreadable slot.
 * @param program Instructions in evaluation order.
 * @param inputs Flag and value readers.
 * @param result Receives the expression result, and is cleared on any refusal.
 * @return True only when the whole program evaluated to one boolean.
 */
[[nodiscard]] bool
evaluate(std::span<const Instruction> program, const Inputs& inputs, bool& result) noexcept;

} // namespace sunrise::state::build_data::vendors
