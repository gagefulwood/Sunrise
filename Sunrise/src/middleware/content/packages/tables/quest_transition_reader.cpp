#include "quest_transition_reader.h"

#include <array>
#include <bitset>

#include "definition_index_table.h"
#include "internal.h"
#include "quest_initialization_reader.h"

namespace sunrise::middleware::content::packages::tables::items {
namespace {

using Predicate = state::build_data::items::QuestPredicate;
using Transition = state::build_data::items::QuestTransition;
using Scope = state::build_data::items::QuestInitialization::Scope;

/** Objective block +0x10 names the effect whose interpretation is outside this reader. */
constexpr std::size_t kCompletionEffectOffset = 0x10;
/** This transition contract carries only the primary completion-effect reference. */
constexpr std::size_t kSecondaryCompletionEffectOffset = 0x12;
/** Objective block +0x14 must carry its all-one reserved reference. */
constexpr std::size_t kUnsupportedObjectiveReferenceOffset = 0x14;
/** Objective block +0x16 must select ordinary objective processing. */
constexpr std::size_t kObjectiveProcessingModeOffset = 0x16;
/** Objective block +0x17 must require every listed objective. */
constexpr std::size_t kObjectiveAllRequiredOffset = 0x17;
/** Objective block +0x18 must not exclude optional objectives. */
constexpr std::size_t kObjectiveOptionalModeOffset = 0x18;
/** Objective block +0x19 must select automatic completion processing. */
constexpr std::size_t kObjectiveAutomaticOffset = 0x19;
/** Objective block +0x1A has no supported completion latch. */
constexpr std::size_t kObjectiveLatchOffset = 0x1A;

/** Objective row +0x08 holds its numeric expression. */
constexpr std::size_t kObjectiveExpressionOffset = 0x08;
/** All eight objective special-flag bytes must be zero. */
constexpr std::size_t kObjectiveSpecialFlagsOffset = 0x28;
/** Objective row +0x38 holds an unsupported threshold modifier expression. */
constexpr std::size_t kObjectiveModifierOffset = 0x38;
/** Numeric expression arrays contain 8-byte opcode/operand rows. */
constexpr std::uint32_t kNumericInstructionRowClass = 0x80807D31U;
/** The supported VALUE, CONSTANT, GE program has exactly three instructions. */
constexpr std::uint64_t kPredicateInstructionCount = 3;
/** A counted objective reads its value with one VALUE instruction. */
constexpr std::uint64_t kCounterInstructionCount = 1;
/** Expression operators without operands carry the all-one 32-bit sentinel. */
constexpr std::uint32_t kUnusedInstructionOperand = 0xFFFFFFFFU;
/** Boolean objective expressions complete only at threshold one. */
constexpr std::int32_t kBooleanObjectiveThreshold = 1;
/**
 * Reads a direct counter or a VALUE, signed CONSTANT, GE predicate.
 * @param table Objective-table bytes.
 * @param descriptor Numeric-expression descriptor offset.
 * @param threshold Signed completion threshold applied to the expression result.
 * @param output Receives the predicate; cleared on failure.
 * @return False when any instruction, operand, class, count, or bound differs.
 */
[[nodiscard]] bool read_predicate(std::span<const std::byte> table,
                                  std::size_t descriptor,
                                  std::int32_t threshold,
                                  Predicate& output) noexcept {
    output = {};
    Array program{};
    if (!find_array_at(table, descriptor, program)
        || program.elementClass != kNumericInstructionRowClass
        || (program.count != kPredicateInstructionCount
            && program.count != kCounterInstructionCount)
        || program.dataOffset > table.size()
        || program.count > (table.size() - program.dataOffset) / kUnlockInstructionStride) {
        return false;
    }

    std::uint32_t valueOpcode = 0, valueSlot = 0;
    if (!read(table, program.dataOffset, valueOpcode)
        || !read(table, program.dataOffset + kUnlockInstructionOperandOffset, valueSlot)
        || valueOpcode != kUnlockReadValueOpcode
        || valueSlot >= state::build_data::items::kQuestValueSlotLimit) {
        return false;
    }
    if (program.count == kCounterInstructionCount) {
        output = {
            static_cast<std::uint16_t>(valueSlot), threshold, Predicate::Input::characterCounter};
        return true;
    }

    std::uint32_t literalOpcode = 0;
    std::int32_t literal = 0;
    std::uint32_t comparisonOpcode = 0, comparisonOperand = 0;
    const std::size_t literalAt = program.dataOffset + kUnlockInstructionStride;
    const std::size_t comparisonAt = literalAt + kUnlockInstructionStride;
    if (threshold != kBooleanObjectiveThreshold || !read(table, literalAt, literalOpcode)
        || !read(table, literalAt + kUnlockInstructionOperandOffset, literal)
        || !read(table, comparisonAt, comparisonOpcode)
        || !read(table, comparisonAt + kUnlockInstructionOperandOffset, comparisonOperand)
        || literalOpcode != kUnlockLiteralOpcode || comparisonOpcode != kUnlockGreaterEqualOpcode
        || comparisonOperand != kUnusedInstructionOperand) {
        return false;
    }
    const auto input = valueSlot == state::build_data::items::kEquipmentPowerValueSlot
                           ? Predicate::Input::equipmentPower
                           : Predicate::Input::family5;
    output = {static_cast<std::uint16_t>(valueSlot), literal, input};
    return true;
}

/**
 * Reads one supported objective row and its complete predicate.
 * @param table Objective-table bytes.
 * @param rows Bounded dense objective rows.
 * @param objectiveIndex Objective row ordinal.
 * @param output Receives the predicate; cleared on failure.
 * @return False for an absent row, special mode, modifier, or unsupported program.
 */
[[nodiscard]] bool read_objective(std::span<const std::byte> table,
                                  const Array& rows,
                                  std::uint16_t objectiveIndex,
                                  Predicate& output) noexcept {
    output = {};
    std::size_t row = 0;
    std::uint64_t specialFlags = 0;
    std::int32_t threshold = 0;
    Array modifier{};
    return element_offset(rows.dataOffset, rows.count, kObjectiveRowStride, objectiveIndex, row)
           && row <= table.size() && table.size() - row >= kObjectiveRowStride
           && read(table, row + kObjectiveSpecialFlagsOffset, specialFlags) && specialFlags == 0
           && read(table, row + kObjectiveCompletionValueOffset, threshold)
           && find_optional_array_at(table, row + kObjectiveModifierOffset, modifier)
           && modifier.count == 0
           && read_predicate(table, row + kObjectiveExpressionOffset, threshold, output);
}

} // namespace

/**
 * Reads one bounded non-final quest transition and its supported objective predicates.
 * @param definition Current pursuit item definition.
 * @param itemIndex Current item's item-table index.
 * @param parent Quest-set owner selected by quest_parent; may be definition itself.
 * @param itemCount Exclusive bound for item-table indices.
 * @param valueMap Blob containing all four unlock value maps.
 * @param objectiveTable Dense objective definition table.
 * @param output Cleared on failure; receives mechanics metadata on success.
 * @return False for final, malformed, ambiguous, or unsupported metadata.
 */
bool read_quest_transition(std::span<const std::byte> definition,
                           std::uint16_t itemIndex,
                           std::span<const std::byte> parent,
                           std::size_t itemCount,
                           std::span<const std::byte> valueMap,
                           std::span<const std::byte> objectiveTable,
                           Transition& output) noexcept {
    output = {};
    const std::uint16_t parentIndex = quest_parent(definition);
    detail::QuestSet set{};
    if (itemIndex >= itemCount || parentIndex >= itemCount
        || !detail::read_quest_set(parent, itemCount, set) || set.members.count < 2) {
        return false;
    }

    std::bitset<state::build_data::items::kQuestItemIndexCapacity> seenItems;
    std::size_t sourcePosition = static_cast<std::size_t>(set.members.count);
    for (std::size_t index = 0; index < set.members.count; ++index) {
        std::int32_t value = 0;
        std::uint16_t member = 0;
        if (!detail::read_quest_member(parent, set, index, itemCount, value, member)
            || member == state::build_data::items::kUnavailableQuestItemIndex
            || value == state::build_data::items::kUnsetQuestValue
            || value == state::build_data::items::kInvalidQuestInitialValue
            || seenItems.test(member)) {
            return false;
        }
        seenItems.set(member);
        if (member == itemIndex) {
            sourcePosition = index;
        }
    }
    if (sourcePosition >= set.members.count - 1) {
        return false;
    }

    Transition candidate{};
    candidate.sourceItemIndex = itemIndex;
    if (!detail::read_quest_member(parent,
                                   set,
                                   sourcePosition,
                                   itemCount,
                                   candidate.currentValue,
                                   candidate.sourceItemIndex)
        || !detail::read_quest_member(parent,
                                      set,
                                      sourcePosition + 1,
                                      itemCount,
                                      candidate.nextValue,
                                      candidate.successorItemIndex)
        || candidate.sourceItemIndex != itemIndex) {
        return false;
    }
    std::size_t currentMatches = 0, nextMatches = 0;
    for (std::size_t index = 0; index < set.members.count; ++index) {
        std::int32_t value = 0;
        std::uint16_t member = 0;
        if (!detail::read_quest_member(parent, set, index, itemCount, value, member)) {
            return false;
        }
        currentMatches += value == candidate.currentValue ? 1U : 0U;
        nextMatches += value == candidate.nextValue ? 1U : 0U;
    }
    if (currentMatches != 1 || nextMatches != 1) {
        return false;
    }

    Scope scope = Scope::none;
    if (!detail::map_quest_value_slot(valueMap, set.valueSlot, scope, candidate.valueRow)
        || scope != Scope::character) {
        return false;
    }

    std::size_t objectiveBlock = 0;
    Array objectiveReferences{}, objectiveRows{};
    if (!detail::read_quest_objectives(definition, objectiveBlock, objectiveReferences)
        || objectiveReferences.count == 0 || objectiveReferences.count > candidate.objectives.size()
        || !find_array(objectiveTable, kObjectiveRowClass, objectiveRows)
        || objectiveRows.dataOffset > objectiveTable.size()
        || objectiveRows.count
               > (objectiveTable.size() - objectiveRows.dataOffset) / kObjectiveRowStride) {
        return false;
    }

    std::uint16_t secondaryEffect = 0, reservedReference = 0, latch = 0;
    std::uint8_t processingMode = 0, allRequired = 0, optionalMode = 0, automatic = 0;
    if (!read(definition, objectiveBlock + kCompletionEffectOffset, candidate.completionEffect)
        || !read(definition, objectiveBlock + kSecondaryCompletionEffectOffset, secondaryEffect)
        || secondaryEffect != state::build_data::items::kUnavailableQuestCompletionEffect
        || !read(
            definition, objectiveBlock + kUnsupportedObjectiveReferenceOffset, reservedReference)
        || reservedReference != state::build_data::items::kUnavailableQuestCompletionEffect
        || !read(definition, objectiveBlock + kObjectiveProcessingModeOffset, processingMode)
        || processingMode != 0
        || !read(definition, objectiveBlock + kObjectiveAllRequiredOffset, allRequired)
        || allRequired != 1
        || !read(definition, objectiveBlock + kObjectiveOptionalModeOffset, optionalMode)
        || optionalMode != 0
        || !read(definition, objectiveBlock + kObjectiveAutomaticOffset, automatic)
        || automatic != 1 || !read(definition, objectiveBlock + kObjectiveLatchOffset, latch)
        || latch != state::build_data::items::kUnavailableQuestCompletionEffect) {
        return false;
    }

    std::array<std::uint16_t, state::build_data::items::kQuestObjectiveCapacity> seenObjectives{};
    for (std::size_t index = 0; index < objectiveReferences.count; ++index) {
        std::uint16_t objectiveIndex = 0;
        if (!read(definition,
                  objectiveReferences.dataOffset + index * sizeof objectiveIndex,
                  objectiveIndex)
            || objectiveIndex == state::build_data::items::kUnavailableQuestItemIndex) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (seenObjectives[prior] == objectiveIndex) {
                return false;
            }
        }
        seenObjectives[index] = objectiveIndex;
        if (!read_objective(
                objectiveTable, objectiveRows, objectiveIndex, candidate.objectives[index])) {
            return false;
        }
    }
    candidate.objectiveCount = static_cast<std::size_t>(objectiveReferences.count);
    if (!state::build_data::items::valid(candidate)) {
        return false;
    }
    output = candidate;
    return true;
}

} // namespace sunrise::middleware::content::packages::tables::items
