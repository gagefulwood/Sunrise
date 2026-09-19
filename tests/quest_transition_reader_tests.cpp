#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../Sunrise/src/middleware/content/packages/tables/quest_initialization_reader.h"
#include "../Sunrise/src/middleware/content/packages/tables/quest_transition_reader.h"

namespace {

using sunrise::middleware::content::packages::tables::items::read_quest_transition;
using sunrise::state::Family5State;
using sunrise::state::build_data::items::complete;
using sunrise::state::build_data::items::QuestPredicate;
using sunrise::state::build_data::items::QuestTransition;
using sunrise::state::build_data::items::valid;

/** Synthetic item-table indices stay small so malformed fixtures are easy to inspect. */
constexpr std::uint16_t kSourceItem = 10;
constexpr std::uint16_t kSuccessorItem = 11;
constexpr std::size_t kItemCount = 20;
/** The synthetic quest set uses one character-mapped value slot. */
constexpr std::uint16_t kSetValueSlot = 7;
/** The synthetic objective reads this explicit Family-5 value slot. */
constexpr std::uint16_t kObjectiveValueSlot = 462;
/** The synthetic objective table uses row three. */
constexpr std::uint16_t kObjectiveIndex = 3;
/** The supported synthetic expression compares against this signed literal. */
constexpr std::int32_t kObjectiveMinimum = 899;
/** Counter fixtures use the same two requirements as a challenge-and-engram objective pair. */
constexpr std::int32_t kChallengeCount = 3, kEngramCount = 2;
/** A second synthetic input must not be satisfied by the first counter. */
constexpr std::uint16_t kOtherObjectiveValueSlot = kObjectiveValueSlot + 1;
/** The synthetic completion reference remains visible so runtime can refuse unresolved effects. */
constexpr std::uint16_t kCompletionEffect = 42;
// Native layout values stay local so the fixture does not inherit decoder constants.
constexpr std::uint32_t kArrayClass = 0x80800000U;
constexpr std::size_t kItemObjectiveOffset = 0x30, kItemQuestSetOffset = 0x60;
constexpr std::size_t kItemBucketOffset = 0xB8;
constexpr std::uint8_t kPursuitBucket = 40;
constexpr std::uint32_t kObjectiveBlockClass = 0x808077EBU;
constexpr std::uint32_t kQuestSetBlockClass = 0x808077C8U;
constexpr std::uint32_t kObjectiveReferenceClass = 0x808087B1U;
constexpr std::uint32_t kQuestMemberClass = 0x808077CAU;
constexpr std::uint32_t kObjectiveRowClass = 0x8080775FU;
constexpr std::uint32_t kInstructionClass = 0x80807D31U;
/** Objective blocks store three 16-bit completion references before their mode bytes. */
constexpr std::size_t kCompletionEffectOffset = 0x10, kSecondaryEffectOffset = 0x12;
constexpr std::size_t kReservedObjectiveReferenceOffset = 0x14;
/** Native objective modes select ordinary processing, all objectives and automatic completion. */
constexpr std::size_t kProcessingModeOffset = 0x16, kAllObjectivesOffset = 0x17;
constexpr std::size_t kOptionalModeOffset = 0x18, kAutomaticCompletionOffset = 0x19;
constexpr std::size_t kObjectiveLatchOffset = 0x1A, kParentItemOffset = 0x1C;
/** Absent native completion references use all sixteen bits set. */
constexpr std::uint16_t kAbsentReference = 0xFFFFU;
constexpr std::uint8_t kOrdinaryProcessing = 0, kIncludeOptionalObjectives = 0;
constexpr std::uint8_t kAllObjectivesRequired = 1, kAutomaticCompletion = 1;
/** Native quest sets store a 16-bit value slot and a separate one-byte mode. */
constexpr std::size_t kSetValueSlotOffset = 0x10, kSetModeOffset = 0x1C;
constexpr std::uint8_t kSupportedSetMode = 1, kUnsupportedSetMode = 2;
constexpr std::size_t kSetMemberStride = 8, kSetMemberItemOffset = 4;
constexpr std::size_t kSetMemberReservedOffset = 6;
constexpr std::size_t kAccountMapDescriptor = 8, kCharacterMapDescriptor = 24;
/** Native map rows store the destination slot before a reserved 16-bit field. */
constexpr std::size_t kMapSlotOffset = 4, kMapReservedOffset = 6;
/** Native objective rows contain an expression, special flags, a threshold and a modifier. */
constexpr std::size_t kObjectiveExpressionOffset = 8, kObjectiveSpecialFlagsOffset = 0x28;
constexpr std::size_t kObjectiveThresholdOffset = 0x30, kObjectiveModifierOffset = 0x38;
constexpr std::size_t kInstructionStride = 8, kInstructionOperandOffset = 4;
constexpr std::uint32_t kReadValueOpcode = 10, kLiteralOpcode = 11;
constexpr std::uint32_t kGreaterEqualOpcode = 14, kUnusedOperand = 0xFFFFFFFFU;
/** Native value slots must fit the nonnegative range of a signed 16-bit mapping. */
constexpr std::uint32_t kInvalidValueSlot = 0x8000U;
constexpr std::int32_t kBooleanThreshold = 1;
// Synthetic placements are arbitrary; each block and nested array must remain disjoint.
/** These byte capacities leave room for the fixture's blocks and malformed-array cases. */
constexpr std::size_t kDefinitionSize = 0x300, kValueMapSize = 0x100;
constexpr std::size_t kObjectiveTableSize = 0x400;
constexpr std::size_t kObjectiveBlock = 0x100, kQuestSetBlock = 0x140;
constexpr std::size_t kSetHeader = 0x180, kSetRows = 0x190;
constexpr std::size_t kObjectiveReferenceHeader = 0x200, kObjectiveReferences = 0x210;
constexpr std::size_t kCharacterMapHeader = 0x80, kCharacterMapRow = 0x90;
constexpr std::size_t kAccountMapHeader = 0xA0, kAccountMapRow = 0xB0;
/** Row three follows the native 16-byte array header and three 0xA0-byte objective rows. */
constexpr std::size_t kObjectiveTableHeader = 0x40, kObjectiveRow = 0x230;
/** The synthetic objective-table descriptor starts after an unused 64-bit prefix. */
constexpr std::size_t kObjectiveTableDescriptor = 8;
/** Synthetic expression storage leaves room for an unsupported modifier array. */
constexpr std::size_t kExpressionHeader = 0x320, kExpressionRows = 0x330;
constexpr std::size_t kModifierHeader = 0x360;
/** The value-map reader only needs the native class prefix; this row class is synthetic. */
constexpr std::uint32_t kFixtureMapRowClass = 0x80800001U;
constexpr std::uint64_t kSetMemberCount = 4, kObjectiveRowCount = 4;
constexpr std::uint64_t kPredicateInstructionCount = 3;
constexpr std::int32_t kStepValueSpacing = 100;
/** Malformed cases use the third member and one nonzero reserved bit. */
constexpr std::size_t kThirdMemberIndex = 2;
constexpr std::uint16_t kReservedFieldSet = 1;
/** Four instructions exceed the supported three-row predicate grammar. */
constexpr std::uint64_t kTrailingInstructionCount = 4;
/** One more than the fixed objective capacity must be refused. */
constexpr std::uint64_t kObjectiveOverflowCount =
    sunrise::state::build_data::items::kQuestObjectiveCapacity + 1;
/** Signed and wide thresholds prove the reader does not narrow objective values. */
constexpr std::int32_t kNegativeMinimum = -5, kWideMinimum = 70000;

/** Retained build 86657 contains four consecutive members in this quest set. */
constexpr std::uint16_t kRetainedFirstItem = 15284, kRetainedSecondItem = 15285;
constexpr std::uint16_t kRetainedThirdItem = 15286, kRetainedFinalItem = 15287;
/** Build 86657's installed item table contains this many rows. */
constexpr std::size_t kRetainedItemCount = 15424;
/** The retained quest set maps its stage value into character-object row 526. */
constexpr std::uint16_t kRetainedQuestRow = 526;
/** Retained counted objectives use these character-owned value slots. */
constexpr std::uint16_t kRetainedChallengeSlot = 13080, kRetainedEngramSlot = 13081;
/** Retained completion references identify the first two supported stage effects. */
constexpr std::uint16_t kRetainedFirstEffect = 11481, kRetainedSecondEffect = 11484;
/** The retained third stage compares its input against this authored threshold. */
constexpr std::int32_t kRetainedThirdMinimum = 910;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "quest_transition_reader: %s\n", message);
        std::abort();
    }
}

template <typename Value> void put(std::vector<std::byte>& bytes, std::size_t offset, Value value) {
    check(offset <= bytes.size() && bytes.size() - offset >= sizeof value,
          "fixture write out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

/**
 * Array offsets are relative to the descriptor's pointer field, not its end.
 * @param bytes Synthetic blob containing both descriptor and rows.
 * @param descriptor Descriptor offset.
 * @param header Array header offset.
 * @param count Number of rows.
 * @param elementClass Serialized row class.
 */
void put_array(std::vector<std::byte>& bytes,
               std::size_t descriptor,
               std::size_t header,
               std::uint64_t count,
               std::uint32_t elementClass) {
    // Definition arrays repeat their count and place a class marker before the header.
    put(bytes, descriptor, count);
    put(bytes,
        descriptor + sizeof(std::uint64_t),
        static_cast<std::int64_t>(header)
            - static_cast<std::int64_t>(descriptor + sizeof(std::uint64_t)));
    put(bytes, header - sizeof(std::uint32_t), kArrayClass);
    put(bytes, header, count);
    put(bytes, header + sizeof(std::uint64_t), elementClass);
}

void put_block(std::vector<std::byte>& bytes,
               std::size_t field,
               std::size_t block,
               std::uint32_t blockClass) {
    put(bytes, field, static_cast<std::int64_t>(block) - static_cast<std::int64_t>(field));
    put(bytes, block - sizeof(std::uint32_t), blockClass);
}

struct Fixture {
    std::vector<std::byte> definition;
    std::vector<std::byte> valueMap;
    std::vector<std::byte> objectiveTable;
};

/** @return A complete synthetic quest and its separate value-map and objective blobs. */
Fixture fixture() {
    Fixture result{std::vector<std::byte>(kDefinitionSize),
                   std::vector<std::byte>(kValueMapSize),
                   std::vector<std::byte>(kObjectiveTableSize)};

    put_block(result.definition, kItemObjectiveOffset, kObjectiveBlock, kObjectiveBlockClass);
    put_block(result.definition, kItemQuestSetOffset, kQuestSetBlock, kQuestSetBlockClass);
    put(result.definition, kItemBucketOffset, kPursuitBucket);

    put_array(
        result.definition, kObjectiveBlock, kObjectiveReferenceHeader, 1, kObjectiveReferenceClass);
    put(result.definition, kObjectiveReferences, kObjectiveIndex);
    put(result.definition, kObjectiveBlock + kCompletionEffectOffset, kCompletionEffect);
    put(result.definition, kObjectiveBlock + kSecondaryEffectOffset, kAbsentReference);
    put(result.definition, kObjectiveBlock + kReservedObjectiveReferenceOffset, kAbsentReference);
    put(result.definition, kObjectiveBlock + kProcessingModeOffset, kOrdinaryProcessing);
    put(result.definition, kObjectiveBlock + kAllObjectivesOffset, kAllObjectivesRequired);
    put(result.definition, kObjectiveBlock + kOptionalModeOffset, kIncludeOptionalObjectives);
    put(result.definition, kObjectiveBlock + kAutomaticCompletionOffset, kAutomaticCompletion);
    put(result.definition, kObjectiveBlock + kObjectiveLatchOffset, kAbsentReference);
    put(result.definition, kObjectiveBlock + kParentItemOffset, kSourceItem);

    put_array(result.definition, kQuestSetBlock, kSetHeader, kSetMemberCount, kQuestMemberClass);
    put(result.definition, kQuestSetBlock + kSetValueSlotOffset, kSetValueSlot);
    put(result.definition, kQuestSetBlock + kSetModeOffset, kSupportedSetMode);
    for (std::size_t index = 0; index < kSetMemberCount; ++index) {
        put(result.definition,
            kSetRows + index * kSetMemberStride,
            static_cast<std::int32_t>((index + 1) * kStepValueSpacing));
        put(result.definition,
            kSetRows + index * kSetMemberStride + kSetMemberItemOffset,
            static_cast<std::uint16_t>(kSourceItem + index));
    }

    // Only the character map contains the quest-set slot.
    put_array(
        result.valueMap, kCharacterMapDescriptor, kCharacterMapHeader, 1, kFixtureMapRowClass);
    put(result.valueMap,
        kCharacterMapRow + kMapSlotOffset,
        static_cast<std::int16_t>(kSetValueSlot));

    put_array(result.objectiveTable,
              kObjectiveTableDescriptor,
              kObjectiveTableHeader,
              kObjectiveRowCount,
              kObjectiveRowClass);
    put(result.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kBooleanThreshold);
    put_array(result.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              kPredicateInstructionCount,
              kInstructionClass);
    put(result.objectiveTable, kExpressionRows, kReadValueOpcode);
    put(result.objectiveTable,
        kExpressionRows + kInstructionOperandOffset,
        std::uint32_t{kObjectiveValueSlot});
    put(result.objectiveTable, kExpressionRows + kInstructionStride, kLiteralOpcode);
    put(result.objectiveTable,
        kExpressionRows + kInstructionStride + kInstructionOperandOffset,
        kObjectiveMinimum);
    put(result.objectiveTable, kExpressionRows + 2 * kInstructionStride, kGreaterEqualOpcode);
    put(result.objectiveTable,
        kExpressionRows + 2 * kInstructionStride + kInstructionOperandOffset,
        kUnusedOperand);
    return result;
}

/**
 * Reads a fixture expected to satisfy the whole transition contract.
 * @param value Synthetic content blobs.
 * @param item Current quest member.
 * @return The decoded transition, or aborts the check on refusal.
 */
QuestTransition read(const Fixture& value, std::uint16_t item = kSourceItem) {
    QuestTransition output{};
    check(read_quest_transition(value.definition,
                                item,
                                value.definition,
                                kItemCount,
                                value.valueMap,
                                value.objectiveTable,
                                output),
          "valid fixture rejected");
    return output;
}

/**
 * A failed read must also clear a previously valid output.
 * @param value Altered content expected to fail.
 * @param message Label for an unexpected success.
 * @param item Current quest member.
 * @param definitionSize Optional truncation; all bits set keeps the whole item blob.
 */
void rejected(const Fixture& value,
              const char* message,
              std::uint16_t item = kSourceItem,
              std::size_t definitionSize = static_cast<std::size_t>(-1)) {
    QuestTransition output = read(fixture());
    const auto definition =
        definitionSize == static_cast<std::size_t>(-1)
            ? std::span<const std::byte>{value.definition}
            : std::span<const std::byte>{value.definition}.first(definitionSize);
    check(
        !read_quest_transition(
            definition, item, definition, kItemCount, value.valueMap, value.objectiveTable, output),
        message);
    check(output == QuestTransition{}, "failure did not clear output");
}

/**
 * Retained metadata is optional and opened read-only.
 * @param directory Caller-supplied private fixture directory.
 * @param name Retained blob filename.
 * @return Complete bytes, or aborts the check on an I/O failure.
 */
std::vector<std::byte> read_file(const char* directory, const char* name) {
    const std::string path = std::string(directory) + "/" + name;
    std::FILE* stream = nullptr;
#ifdef _MSC_VER
    check(fopen_s(&stream, path.c_str(), "rb") == 0, "optional retained file could not be opened");
#else
    stream = std::fopen(path.c_str(), "rb");
#endif
    check(stream != nullptr, "optional retained file could not be opened");
    check(std::fseek(stream, 0, SEEK_END) == 0, "retained file seek failed");
    const long length = std::ftell(stream);
    check(length >= 0 && std::fseek(stream, 0, SEEK_SET) == 0, "retained file length failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    check(bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), stream) == bytes.size(),
          "retained file read failed");
    check(std::fclose(stream) == 0, "retained file close failed");
    return bytes;
}

/** Checks supported predicates, malformed boundaries and every unsupported metadata mode. */
void verify_generated() {
    Fixture value = fixture();
    QuestTransition expected{};
    expected.sourceItemIndex = kSourceItem;
    expected.successorItemIndex = kSuccessorItem;
    expected.currentValue = kStepValueSpacing;
    expected.nextValue = 2 * kStepValueSpacing;
    expected.valueRow = 0;
    expected.objectives[0] = QuestPredicate{kObjectiveValueSlot, kObjectiveMinimum};
    expected.objectiveCount = 1;
    expected.completionEffect = kCompletionEffect;
    check(read(value) == expected, "decoded transition differs");
    check(valid(expected), "decoded transition is invalid");

    Family5State family{};
    family.values[0] = {kObjectiveValueSlot, kObjectiveMinimum};
    family.valueCount = 1;
    check(complete(expected, family), "explicit threshold value did not complete");
    family.values[0].value = kObjectiveMinimum - 1;
    check(!complete(expected, family), "below-threshold value completed");
    family.values[0].value = kObjectiveMinimum + 1;
    check(complete(expected, family), "above-threshold value did not complete");
    family.valueCount = 0;
    check(!complete(expected, family), "missing value defaulted to zero");
    family.values[0] = {kObjectiveValueSlot, kObjectiveMinimum};
    family.values[1] = family.values[0];
    family.valueCount = 2;
    check(!complete(expected, family), "duplicate Family-5 value was accepted");
    family.valueCount = family.values.size() + 1;
    check(!complete(expected, family), "malformed Family-5 count was accepted");
    QuestTransition malformed = expected;
    malformed.objectiveCount = malformed.objectives.size() + 1;
    check(!complete(malformed, Family5State{}), "malformed transition completed");

    rejected(value, "truncated item accepted", kSourceItem, kItemObjectiveOffset);

    value = fixture();
    put(value.definition, kQuestSetBlock + kSetModeOffset, kUnsupportedSetMode);
    rejected(value, "unsupported quest-set mode accepted");

    value = fixture();
    put(value.definition,
        kSetRows + kThirdMemberIndex * kSetMemberStride + kSetMemberItemOffset,
        kSuccessorItem);
    rejected(value, "duplicate member item accepted");

    value = fixture();
    put(value.definition, kSetRows + kSetMemberReservedOffset, kReservedFieldSet);
    rejected(value, "quest-set reserved field accepted");

    value = fixture();
    put(value.definition, kSetRows + kThirdMemberIndex * kSetMemberStride, kStepValueSpacing);
    rejected(value, "duplicate current step accepted");

    value = fixture();
    put(value.definition, kSetRows + kThirdMemberIndex * kSetMemberStride, 2 * kStepValueSpacing);
    rejected(value, "duplicate next step accepted");

    value = fixture();
    put(value.definition,
        kSetRows + kThirdMemberIndex * kSetMemberStride,
        sunrise::state::build_data::items::kUnsetQuestValue);
    rejected(value, "invalid step sentinel accepted");

    rejected(fixture(),
             "final quest member accepted",
             static_cast<std::uint16_t>(kSourceItem + kSetMemberCount - 1));

    value = fixture();
    put_array(value.valueMap, kAccountMapDescriptor, kAccountMapHeader, 1, kFixtureMapRowClass);
    put(value.valueMap, kAccountMapRow + kMapSlotOffset, static_cast<std::int16_t>(kSetValueSlot));
    rejected(value, "duplicate value mapping accepted");

    value = fixture();
    put(value.valueMap, kCharacterMapDescriptor, std::uint64_t{0});
    put(value.valueMap, kCharacterMapDescriptor + sizeof(std::uint64_t), std::int64_t{0});
    put_array(value.valueMap, kAccountMapDescriptor, kAccountMapHeader, 1, kFixtureMapRowClass);
    put(value.valueMap, kAccountMapRow + kMapSlotOffset, static_cast<std::int16_t>(kSetValueSlot));
    rejected(value, "account-scoped quest value accepted");

    value = fixture();
    put(value.valueMap, kCharacterMapRow + kMapReservedOffset, kReservedFieldSet);
    rejected(value, "mapping reserved field accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kSecondaryEffectOffset, kReservedFieldSet);
    rejected(value, "secondary completion effect accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kAllObjectivesOffset, std::uint8_t{0});
    rejected(value, "any-objective mode accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kAutomaticCompletionOffset, std::uint8_t{0});
    rejected(value, "manual completion mode accepted");

    value = fixture();
    put(value.objectiveTable,
        kObjectiveRow + kObjectiveSpecialFlagsOffset,
        static_cast<std::uint8_t>(kReservedFieldSet));
    rejected(value, "objective special flag accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kEngramCount);
    rejected(value, "non-boolean objective threshold accepted");

    value = fixture();
    put(value.objectiveTable, kExpressionRows, kLiteralOpcode);
    rejected(value, "reordered expression accepted");

    value = fixture();
    put(value.objectiveTable,
        kObjectiveRow + kObjectiveExpressionOffset,
        kTrailingInstructionCount);
    put(value.objectiveTable, kExpressionHeader, kTrailingInstructionCount);
    rejected(value, "trailing expression instruction accepted");

    value = fixture();
    put(value.objectiveTable,
        kExpressionRows + 2 * kInstructionStride + kInstructionOperandOffset,
        std::uint32_t{0});
    rejected(value, "comparison operand sentinel accepted");

    value = fixture();
    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveModifierOffset,
              kModifierHeader,
              1,
              kInstructionClass);
    rejected(value, "threshold modifier accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock, std::uint64_t{2});
    put(value.definition, kObjectiveReferenceHeader, std::uint64_t{2});
    put(value.definition, kObjectiveReferences + sizeof(kObjectiveIndex), kObjectiveIndex);
    rejected(value, "duplicate objective reference accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock, kObjectiveOverflowCount);
    put(value.definition, kObjectiveReferenceHeader, kObjectiveOverflowCount);
    rejected(value, "objective capacity overflow accepted");

    value = fixture();
    value.objectiveTable.resize(kExpressionRows + 2 * kInstructionStride
                                + kInstructionOperandOffset);
    rejected(value, "truncated full expression accepted");

    value = fixture();
    put(value.objectiveTable,
        kExpressionRows + kInstructionStride + kInstructionOperandOffset,
        kNegativeMinimum);
    QuestTransition signedMinimum = read(value);
    check(signedMinimum.objectives[0].minimumValue == kNegativeMinimum,
          "signed expression literal was not preserved");
    put(value.objectiveTable,
        kExpressionRows + kInstructionStride + kInstructionOperandOffset,
        kWideMinimum);
    check(read(value).objectives[0].minimumValue == kWideMinimum,
          "expression literal narrowed to sixteen bits");

    value = fixture();
    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              1,
              kInstructionClass);
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kChallengeCount);
    QuestTransition counted = read(value);
    check(counted.objectives[0]
              == QuestPredicate{kObjectiveValueSlot,
                                kChallengeCount,
                                QuestPredicate::Input::characterCounter},
          "direct counter did not use the objective threshold");
    family = {};
    family.valueCount = 1;
    family.values[0] = {kObjectiveValueSlot, kChallengeCount - 1};
    check(!complete(counted, family), "incomplete counter completed");
    family.values[0].value = kChallengeCount;
    check(complete(counted, family), "counter threshold did not complete");
    ++family.values[0].value;
    check(complete(counted, family), "counter above threshold did not complete");

    counted.objectiveCount = 2;
    counted.objectives[1] = {kOtherObjectiveValueSlot, kEngramCount};
    check(!complete(counted, family), "missing second counter completed");
    family.valueCount = 2;
    family.values[1] = {kOtherObjectiveValueSlot, kEngramCount - 1};
    check(!complete(counted, family), "one completed counter satisfied both objectives");
    family.values[1].value = kEngramCount;
    check(complete(counted, family), "both completed counters rejected");
    family.values[0].value = kChallengeCount - 1;
    check(!complete(counted, family), "second counter masked incomplete first counter");

    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kWideMinimum);
    check(read(value).objectives[0].minimumValue == kWideMinimum, "counter threshold narrowed");
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kNegativeMinimum);
    check(read(value).objectives[0].minimumValue == kNegativeMinimum,
          "counter threshold lost its sign");
    family.valueCount = 0;
    check(!complete(read(value), family), "absent counter defaulted to a completing zero");

    Fixture malformedCounter = value;
    put(malformedCounter.objectiveTable,
        kExpressionRows + kInstructionOperandOffset,
        kInvalidValueSlot);
    rejected(malformedCounter, "out-of-range counter slot accepted");
    malformedCounter = value;
    put(malformedCounter.objectiveTable, kExpressionRows, kLiteralOpcode);
    rejected(malformedCounter, "literal-only expression accepted as a counter");
    malformedCounter = value;
    malformedCounter.objectiveTable.resize(kExpressionRows + kInstructionOperandOffset);
    rejected(malformedCounter, "truncated counter operand accepted");
    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              2,
              kInstructionClass);
    rejected(value, "counter with extra instruction accepted");
}

/**
 * Tests the reader against retained content independently of the synthetic builder.
 * @param retainedDirectory Read-only directory containing the retained item and table blobs.
 */
void verify_retained(const char* retainedDirectory) {
    const std::vector<std::byte> item = read_file(retainedDirectory, "81327AD4.bin");
    const std::vector<std::byte> valueMap = read_file(retainedDirectory, "81319320.bin");
    const std::vector<std::byte> objectives = read_file(retainedDirectory, "81319344.bin");
    QuestTransition output{};
    check(read_quest_transition(
              item, kRetainedFirstItem, item, kRetainedItemCount, valueMap, objectives, output),
          "retained first-stage transition rejected");
    QuestTransition expected{};
    expected.sourceItemIndex = kRetainedFirstItem;
    expected.successorItemIndex = kRetainedSecondItem;
    expected.currentValue = kStepValueSpacing;
    expected.nextValue = 2 * kStepValueSpacing;
    expected.valueRow = kRetainedQuestRow;
    expected.objectives[0] = {kObjectiveValueSlot, kObjectiveMinimum};
    expected.objectiveCount = 1;
    expected.completionEffect = kRetainedFirstEffect;
    check(output == expected, "retained first-stage transition differs");
    // Shared-parser extraction must leave the existing first-acquisition contract unchanged.
    const auto initial =
        sunrise::middleware::content::packages::tables::items::read_quest_initialization(
            item, kRetainedFirstItem, item, kRetainedItemCount, valueMap);
    check(initial.scope == sunrise::state::build_data::items::QuestInitialization::Scope::character
              && initial.value == expected.currentValue && initial.row == expected.valueRow,
          "first-acquisition metadata regressed");
    const auto second = read_file(retainedDirectory, "81327ADC.bin");
    check(
        read_quest_transition(
            second, kRetainedSecondItem, second, kRetainedItemCount, valueMap, objectives, output),
        "counted-objective stage rejected");
    expected.sourceItemIndex = kRetainedSecondItem;
    expected.successorItemIndex = kRetainedThirdItem;
    expected.currentValue = 2 * kStepValueSpacing;
    expected.nextValue = 3 * kStepValueSpacing;
    expected.objectives[0] = {
        kRetainedChallengeSlot, kChallengeCount, QuestPredicate::Input::characterCounter};
    expected.objectives[1] = {
        kRetainedEngramSlot, kEngramCount, QuestPredicate::Input::characterCounter};
    expected.objectiveCount = 2;
    expected.completionEffect = kRetainedSecondEffect;
    check(output == expected, "retained counter requirements or completion reference differ");
    check(!complete(output, Family5State{}), "decoding metadata supplied missing earned progress");
    const auto third = read_file(retainedDirectory, "81327ADF.bin");
    check(read_quest_transition(
              third, kRetainedThirdItem, third, kRetainedItemCount, valueMap, objectives, output)
              && output.sourceItemIndex == kRetainedThirdItem
              && output.successorItemIndex == kRetainedFinalItem
              && output.currentValue == 3 * kStepValueSpacing
              && output.nextValue == 4 * kStepValueSpacing
              && output.objectives[0].minimumValue == kRetainedThirdMinimum,
          "non-first supported stage rejected");
    const auto final = read_file(retainedDirectory, "81327AE2.bin");
    check(!read_quest_transition(
              final, kRetainedFinalItem, final, kRetainedItemCount, valueMap, objectives, output),
          "final completion accepted as a replacement");
}

} // namespace

/** @param retainedDirectory Optional read-only fixture directory; null runs synthetic cases only.
 */
void verify_quest_transition_reader(const char* retainedDirectory) {
    verify_generated();
    if (retainedDirectory != nullptr) {
        verify_retained(retainedDirectory);
        std::puts("PASS: retained stage metadata and unchanged first-acquisition contract");
    }
    std::puts("PASS: generated metadata, full predicates and unsupported cases");
}
