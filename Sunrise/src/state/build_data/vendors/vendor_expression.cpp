#include "vendor_expression.h"

#include <array>
#include <limits>

#include "middleware/content/packages/tables/definition_index_table.h"
#include "middleware/content/packages/tables/field_reader.h"

namespace sunrise::state::build_data::vendors {
namespace {

namespace tables = middleware::content::packages::tables;
/** Native class of an eight-byte unlock instruction inside an interaction or sale gate. */
constexpr std::uint32_t kInstructionClass = 0x80807D31U;
/** Native class of a sale gate list's sixteen-byte expression descriptors. */
constexpr std::uint32_t kProgramListClass = 0x80807D2FU;

/** Appends one checked native instruction array without exposing a partial result. */
[[nodiscard]] bool
append_program(std::span<const std::byte> blob, std::size_t field, Program& program) noexcept {
    tables::Array rows{};
    if (!tables::read_array(blob, field, kInstructionClass, tables::kUnlockInstructionStride, rows)
        || rows.count > program.instructions.size() - program.count) {
        return false;
    }
    for (std::size_t index = 0; index < rows.count; ++index) {
        const auto at = rows.dataOffset + index * tables::kUnlockInstructionStride;
        std::uint32_t opcode = 0;
        std::int32_t operand = 0;
        if (!tables::read(blob, at, opcode)
            || !tables::read(blob, at + tables::kUnlockInstructionOperandOffset, operand)) {
            return false;
        }
        program.instructions[program.count++] = {static_cast<Opcode>(opcode), operand};
    }
    return true;
}

/** What one stack slot holds. The two kinds never substitute for each other. */
enum class Kind : std::uint8_t {
    boolean,
    number,
};

/** One typed stack slot. */
struct Slot {
    Kind kind{};
    bool boolean{};
    std::int32_t number{};
};

/** Fixed evaluation stack, which refuses rather than growing. */
class Stack final {
public:
    /** @param slot Value to push. @return True when there was room. */
    [[nodiscard]] bool push(const Slot& slot) noexcept {
        if (depth_ == slots_.size()) {
            return false;
        }
        slots_[depth_++] = slot;
        return true;
    }

    /** @param kind Required kind. @param slot Receives the value. @return True when it matched. */
    [[nodiscard]] bool pop(Kind kind, Slot& slot) noexcept {
        if (depth_ == 0 || slots_[depth_ - 1].kind != kind) {
            return false;
        }
        slot = slots_[--depth_];
        return true;
    }

    /** @return Slots currently held. */
    [[nodiscard]] std::size_t depth() const noexcept {
        return depth_;
    }

private:
    std::array<Slot, kExpressionStackCapacity> slots_{};
    std::size_t depth_{};
};

/** @param value Boolean result. @return The stack slot holding it. */
[[nodiscard]] Slot boolean_slot(bool value) noexcept {
    return {Kind::boolean, value, 0};
}

/** @param value Numeric result. @return The stack slot holding it. */
[[nodiscard]] Slot number_slot(std::int32_t value) noexcept {
    return {Kind::number, false, value};
}

/**
 * Runs one instruction against the stack.
 * @param instruction Instruction to run.
 * @param inputs Flag and value readers.
 * @param stack Evaluation stack.
 * @return True when the opcode is known and its operands were there in the right kind.
 */
[[nodiscard]] bool
step(const Instruction& instruction, const Inputs& inputs, Stack& stack) noexcept {
    Slot left{};
    Slot right{};
    switch (instruction.opcode) {
    case Opcode::flag: {
        std::uint8_t logical = 0;
        return instruction.operand >= 0
               && instruction.operand <= (std::numeric_limits<std::uint16_t>::max)()
               && inputs.flag(
                   inputs.context, static_cast<std::uint16_t>(instruction.operand), logical)
               && stack.push(boolean_slot(logical == kFlagActive));
    }
    case Opcode::logicalNot:
        return stack.pop(Kind::boolean, left) && stack.push(boolean_slot(!left.boolean));
    case Opcode::logicalAnd:
        return stack.pop(Kind::boolean, right) && stack.pop(Kind::boolean, left)
               && stack.push(boolean_slot(left.boolean && right.boolean));
    case Opcode::loadValue: {
        std::int32_t value = 0;
        return instruction.operand >= 0
               && instruction.operand <= (std::numeric_limits<std::uint16_t>::max)()
               && inputs.value(
                   inputs.context, static_cast<std::uint16_t>(instruction.operand), value)
               && stack.push(number_slot(value));
    }
    case Opcode::constant:
        return stack.push(number_slot(instruction.operand));
    case Opcode::greaterThan:
        return stack.pop(Kind::number, right) && stack.pop(Kind::number, left)
               && stack.push(boolean_slot(left.number > right.number));
    case Opcode::greaterOrEqual:
        return stack.pop(Kind::number, right) && stack.pop(Kind::number, left)
               && stack.push(boolean_slot(left.number >= right.number));
    case Opcode::lessThan:
        // The right operand was pushed last, so it comes off first.
        return stack.pop(Kind::number, right) && stack.pop(Kind::number, left)
               && stack.push(boolean_slot(left.number < right.number));
    default:
        return false;
    }
}

} // namespace

/** Reads one direct native interaction program. */
bool read_program(std::span<const std::byte> blob, std::size_t field, Program& output) noexcept {
    output = {};
    Program parsed{};
    if (!append_program(blob, field, parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

/** Folds a sale's authored program list with logical AND. */
bool read_program_list(std::span<const std::byte> blob,
                       std::size_t field,
                       Program& output) noexcept {
    output = {};
    tables::Array rows{};
    if (!tables::read_array(
            blob, field, kProgramListClass, tables::kUnlockExpressionFieldSize, rows)) {
        return false;
    }
    Program parsed{};
    for (std::size_t index = 0; index < rows.count; ++index) {
        const auto at = rows.dataOffset + index * tables::kUnlockExpressionFieldSize;
        const auto before = parsed.count;
        if (!append_program(blob, at, parsed) || parsed.count == before) {
            return false;
        }
        if (index != 0) {
            if (parsed.count == parsed.instructions.size()) {
                return false;
            }
            parsed.instructions[parsed.count++] = {Opcode::logicalAnd, 0};
        }
    }
    output = parsed;
    return true;
}

/** Evaluates one expression program. */
bool evaluate(std::span<const Instruction> program, const Inputs& inputs, bool& result) noexcept {
    result = false;
    if (program.empty() || inputs.flag == nullptr || inputs.value == nullptr) {
        return false;
    }
    Stack stack;
    for (const Instruction& instruction : program) {
        if (!step(instruction, inputs, stack)) {
            return false;
        }
    }
    Slot final{};
    if (stack.depth() != 1 || !stack.pop(Kind::boolean, final)) {
        return false;
    }
    result = final.boolean;
    return true;
}

} // namespace sunrise::state::build_data::vendors
