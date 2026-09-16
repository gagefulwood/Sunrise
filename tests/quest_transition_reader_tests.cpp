#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../Sunrise/src/middleware/content/packages/tables/quest_initialization_reader.h"
#include "../Sunrise/src/middleware/content/packages/tables/quest_transition_reader.h"
#include "../Sunrise/src/state/build_data/cache/records/codec.h"

namespace {

using sunrise::middleware::content::packages::tables::items::read_prime_decryption_binding;
using sunrise::middleware::content::packages::tables::items::read_quest_transition;
using sunrise::state::Family5State;
using sunrise::state::build_data::items::complete;
using sunrise::state::build_data::items::QuestCounterBinding;
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
/** The synthetic completion reference remains visible to the runtime policy. */
constexpr std::uint16_t kCompletionEffect = 42;

// Native layout values stay local so the fixture does not inherit decoder constants.
/** Native arrays carry this class marker before their repeated count. */
constexpr std::uint32_t kArrayClass = 0x80800000U;
/** Native item fields point to the objective, quest-set and presence-flag blocks. */
constexpr std::size_t kItemObjectiveOffset = 0x30, kItemQuestSetOffset = 0x60,
                      kItemUnlockOffset = 0x90;
/** Native item +0xB8 identifies its inventory bucket; pursuits use bucket 40. */
constexpr std::size_t kItemBucketOffset = 0xB8;
constexpr std::uint8_t kPursuitBucket = 40;
/** Native block classes identify objective, ordered-set and presence-flag payloads. */
constexpr std::uint32_t kObjectiveBlockClass = 0x808077EBU, kQuestSetBlockClass = 0x808077C8U,
                        kUnlockBlockClass = 0x808077ABU;
/** Native element classes identify objective indices, quest members and presence flags. */
constexpr std::uint32_t kObjectiveReferenceClass = 0x808087B1U, kQuestMemberClass = 0x808077CAU,
                        kPresenceFlagClass = 0x80807D4BU;
/** Native objective and numeric-instruction arrays require these element classes. */
constexpr std::uint32_t kObjectiveRowClass = 0x8080775FU, kInstructionClass = 0x80807D31U;
/** Objective blocks store three 16-bit completion references before their mode bytes. */
constexpr std::size_t kCompletionEffectOffset = 0x10, kSecondaryEffectOffset = 0x12,
                      kReservedObjectiveReferenceOffset = 0x14;
/** Native objective modes select ordinary processing, all objectives and automatic completion. */
constexpr std::size_t kProcessingModeOffset = 0x16, kAllObjectivesOffset = 0x17,
                      kOptionalModeOffset = 0x18, kAutomaticCompletionOffset = 0x19;
/** Native objective blocks end with a latch reference and the quest-set owner's item index. */
constexpr std::size_t kObjectiveLatchOffset = 0x1A, kParentItemOffset = 0x1C;
/** Absent native completion references use all sixteen bits set. */
constexpr std::uint16_t kAbsentReference = 0xFFFFU;
/** Mode zero includes optional objectives and selects ordinary objective processing. */
constexpr std::uint8_t kOrdinaryProcessing = 0, kIncludeOptionalObjectives = 0;
/** Native mode one requires all objectives and completes the quest automatically. */
constexpr std::uint8_t kAllObjectivesRequired = 1, kAutomaticCompletion = 1;
/** Native quest sets store a 16-bit value slot and a separate one-byte mode. */
constexpr std::size_t kSetValueSlotOffset = 0x10, kSetModeOffset = 0x1C;
/** Only native quest-set mode one supports these transitions. */
constexpr std::uint8_t kSupportedSetMode = 1;
/** Native members contain a signed value, a 16-bit item index and a reserved 16-bit field. */
constexpr std::size_t kSetMemberStride = 8, kSetMemberItemOffset = 4, kSetMemberReservedOffset = 6;
/** Native value-map descriptors select account and character save banks. */
constexpr std::size_t kAccountMapDescriptor = 8, kCharacterMapDescriptor = 24;
/** Native map rows store the destination slot before a reserved 16-bit field. */
constexpr std::size_t kMapSlotOffset = 4, kMapReservedOffset = 6;
/** Each native value-map row pairs a 32-bit hash with a 16-bit slot and reserved field. */
constexpr std::size_t kValueMapRowStride = 8;
/** Native objective rows contain an expression, special flags, a threshold and a modifier. */
constexpr std::size_t kObjectiveExpressionOffset = 8, kObjectiveSpecialFlagsOffset = 0x28,
                      kObjectiveThresholdOffset = 0x30, kObjectiveModifierOffset = 0x38;
/** Native numeric instructions contain a 32-bit opcode followed by a 32-bit operand. */
constexpr std::size_t kInstructionStride = 8, kInstructionOperandOffset = 4;
/** Native numeric opcodes read a value, push a signed literal and compare greater-or-equal. */
constexpr std::uint32_t kReadValueOpcode = 10, kLiteralOpcode = 11, kGreaterEqualOpcode = 14;
/** Native operators without an operand carry all thirty-two bits set. */
constexpr std::uint32_t kUnusedOperand = 0xFFFFFFFFU;
/** Native value slots must fit the nonnegative range of a signed 16-bit mapping. */
constexpr std::uint32_t kInvalidValueSlot = 0x8000U;
/** Boolean objective expressions complete at threshold one. */
constexpr std::int32_t kBooleanThreshold = 1;
/** Build 86657 identifies the Prime-decryption objective by this hash. */
constexpr std::uint32_t kPrimeDecryptionObjectiveHash = 4243255788U;

// Synthetic placements are arbitrary; each block and nested array must remain disjoint.
/** These byte capacities leave room for the fixture's blocks and malformed-array cases. */
constexpr std::size_t kDefinitionSize = 0x300, kValueMapSize = 0x100, kObjectiveTableSize = 0x400;
/** Item-blob placements leave each block's native class prefix and array header intact. */
constexpr std::size_t kObjectiveBlock = 0x100, kQuestSetBlock = 0x140, kSetHeader = 0x180,
                      kSetRows = 0x190, kObjectiveReferenceHeader = 0x200,
                      kObjectiveReferences = 0x210, kUnlockBlock = 0x250,
                      kPresenceFlagHeader = 0x280, kPresenceFlags = 0x290;
/** The optional account map must not overlap the character map in duplicate-mapping cases. */
constexpr std::size_t kCharacterMapHeader = 0x80, kCharacterMapRow = 0x90, kAccountMapHeader = 0xA0,
                      kAccountMapRow = 0xB0;
/** Row three follows the native 16-byte array header and three 0xA0-byte objective rows. */
constexpr std::size_t kObjectiveTableHeader = 0x40, kObjectiveRow = 0x230;
/** The synthetic objective-table descriptor starts after an unused 64-bit prefix. */
constexpr std::size_t kObjectiveTableDescriptor = 8;
/** Synthetic expression storage leaves room for an unsupported modifier array. */
constexpr std::size_t kExpressionHeader = 0x320, kExpressionRows = 0x330, kModifierHeader = 0x360;
/** The value-map reader only requires the native class prefix; this row class is synthetic. */
constexpr std::uint32_t kFixtureMapRowClass = 0x80800001U;
/** Four quest members use distinct step values spaced by one hundred. */
constexpr std::uint64_t kSetMemberCount = 4;
constexpr std::int32_t kStepValueSpacing = 100;
/** Four objective rows make the selected row three available. */
constexpr std::uint64_t kObjectiveRowCount = 4;
/** The synthetic predicate reads one value, pushes one literal and compares them. */
constexpr std::uint64_t kPredicateInstructionCount = 3;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "quest_transition_reader: %s\n", message);
        std::abort();
    }
}

/**
 * Writes one native scalar without crossing the synthetic blob's boundary.
 * @tparam Value Native scalar type whose width is emitted.
 * @param bytes Destination blob.
 * @param offset Byte offset of the scalar.
 * @param value Scalar to copy unchanged.
 */
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

/**
 * Block pointers are field-relative, with a native class ID immediately before the payload.
 * @param bytes Blob containing both pointer and payload.
 * @param field Pointer field offset.
 * @param block Payload offset.
 * @param blockClass Native payload class.
 */
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
    // First-acquisition metadata requires the item's authored presence-flag array.
    put_block(result.definition, kItemUnlockOffset, kUnlockBlock, kUnlockBlockClass);
    put_array(result.definition, kUnlockBlock, kPresenceFlagHeader, 1, kPresenceFlagClass);
    put(result.definition, kPresenceFlags, kSetValueSlot);
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
    expected.currentValue = 100;
    expected.nextValue = 200;
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
    put(value.definition, kQuestSetBlock + kSetModeOffset, std::uint8_t{2});
    rejected(value, "unsupported quest-set mode accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * kSetMemberStride + kSetMemberItemOffset, kSuccessorItem);
    rejected(value, "duplicate member item accepted");

    value = fixture();
    put(value.definition, kSetRows + kSetMemberReservedOffset, std::uint16_t{1});
    rejected(value, "quest-set reserved field accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * kSetMemberStride, std::int32_t{100});
    rejected(value, "duplicate current step accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * kSetMemberStride, std::int32_t{200});
    rejected(value, "duplicate next step accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * kSetMemberStride, std::int32_t{-1});
    rejected(value, "invalid step sentinel accepted");

    rejected(fixture(), "final quest member accepted", kSourceItem + 3);

    value = fixture();
    put_array(value.valueMap, kAccountMapDescriptor, kAccountMapHeader, 1, kFixtureMapRowClass);
    put(value.valueMap, kAccountMapRow + kMapSlotOffset, static_cast<std::int16_t>(kSetValueSlot));
    rejected(value, "duplicate value mapping accepted");

    value = fixture();
    put(value.valueMap, kCharacterMapDescriptor, std::uint64_t{0});
    put(value.valueMap, kCharacterMapDescriptor + sizeof(std::uint64_t), std::int64_t{0});
    put_array(value.valueMap, kAccountMapDescriptor, kAccountMapHeader, 1, kFixtureMapRowClass);
    put(value.valueMap, kAccountMapRow + kMapSlotOffset, static_cast<std::int16_t>(kSetValueSlot));
    check(read(value).scope
              == sunrise::state::build_data::items::QuestInitialization::Scope::account,
          "account-scoped quest value lost its scope");

    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              1,
              kInstructionClass);
    rejected(value, "unmapped account counter accepted");
    put_array(value.valueMap, kAccountMapDescriptor, kAccountMapHeader, 2, kFixtureMapRowClass);
    put(value.valueMap,
        kAccountMapRow + kValueMapRowStride + kMapSlotOffset,
        static_cast<std::int16_t>(kObjectiveValueSlot));
    check(read(value).objectives[0]
              == QuestPredicate{kObjectiveValueSlot,
                                kBooleanThreshold,
                                QuestPredicate::Input::accountCounter,
                                1},
          "account counter did not retain its mapped row");
    put_array(value.valueMap, kCharacterMapDescriptor, kCharacterMapHeader, 1, kFixtureMapRowClass);
    put(value.valueMap,
        kCharacterMapRow + kMapSlotOffset,
        static_cast<std::int16_t>(kObjectiveValueSlot));
    rejected(value, "ambiguous account counter mapping accepted");

    value = fixture();
    put(value.valueMap, kCharacterMapRow + kMapReservedOffset, std::uint16_t{1});
    rejected(value, "mapping reserved field accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kSecondaryEffectOffset, std::uint16_t{1});
    rejected(value, "secondary completion effect accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kAllObjectivesOffset, std::uint8_t{0});
    rejected(value, "any-objective mode accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + kAutomaticCompletionOffset, std::uint8_t{0});
    rejected(value, "manual completion mode accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + kObjectiveSpecialFlagsOffset, std::uint8_t{1});
    rejected(value, "objective special flag accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, std::int32_t{2});
    rejected(value, "non-boolean objective threshold accepted");

    value = fixture();
    put(value.objectiveTable, kExpressionRows, kLiteralOpcode);
    rejected(value, "reordered expression accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + kObjectiveExpressionOffset, std::uint64_t{4});
    put(value.objectiveTable, kExpressionHeader, std::uint64_t{4});
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
    put(value.definition, kObjectiveReferences + sizeof(std::uint16_t), kObjectiveIndex);
    rejected(value, "duplicate objective reference accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock, std::uint64_t{17});
    put(value.definition, kObjectiveReferenceHeader, std::uint64_t{17});
    rejected(value, "objective capacity overflow accepted");

    value = fixture();
    value.objectiveTable.resize(kExpressionRows + 2 * kInstructionStride
                                + kInstructionOperandOffset);
    rejected(value, "truncated full expression accepted");

    value = fixture();
    put(value.objectiveTable,
        kExpressionRows + kInstructionStride + kInstructionOperandOffset,
        std::int32_t{-5});
    QuestTransition signedMinimum = read(value);
    check(signedMinimum.objectives[0].minimumValue == -5,
          "signed expression literal was not preserved");
    put(value.objectiveTable,
        kExpressionRows + kInstructionStride + kInstructionOperandOffset,
        std::int32_t{70000});
    check(read(value).objectives[0].minimumValue == 70000,
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

    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, std::int32_t{70000});
    check(read(value).objectives[0].minimumValue == 70000, "counter threshold narrowed");
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, std::int32_t{-5});
    check(read(value).objectives[0].minimumValue == -5, "counter threshold lost its sign");
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

/** Checks first-stage Power gates without broadening later-stage or effect handling. */
void verify_power_gate() {
    namespace items = sunrise::state::build_data::items;
    namespace cache = sunrise::state::build_data::cache::records;
    using sunrise::middleware::content::packages::tables::items::read_power_quest_gate;
    Fixture value = fixture();
    const auto readGate = [&](std::uint16_t index = kSourceItem) {
        return read_power_quest_gate(value.definition,
                                     index,
                                     value.definition,
                                     kItemCount,
                                     value.valueMap,
                                     value.objectiveTable);
    };
    const items::QuestPowerGate expected{kObjectiveMinimum, 200, kSuccessorItem, kCompletionEffect};
    check(readGate() == expected, "first Power gate metadata differs");
    check(readGate(kSuccessorItem) == items::QuestPowerGate{}, "later stage auto-bound");
    items::Definition item{}, decoded{};
    item.definitionIndex = kSourceItem;
    item.questInitialization = {100, 0, items::QuestInitialization::Scope::character};
    item.powerGate = expected;
    cache::ItemRecord record{};
    check(cache::encode(item, record) && cache::decode(record, decoded)
              && decoded.powerGate == expected,
          "Power gate cache round trip");
    auto transition = items::power_transition(expected, item.questInitialization, kSourceItem);
    check(items::valid(transition)
              && transition.objectives[0].input == QuestPredicate::Input::characterPower
              && transition.completionEffect == kCompletionEffect,
          "Power gate lost derived input or completion reference");
    record.questSuccessorItemIndex = kSourceItem;
    check(!cache::decode(record, decoded), "cached self-transition accepted");
    put(value.objectiveTable,
        kExpressionRows + kInstructionOperandOffset,
        std::uint32_t{kOtherObjectiveValueSlot});
    check(readGate() == items::QuestPowerGate{}, "unrelated global input bound as Power");
    value = fixture();
    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              1,
              kInstructionClass);
    check(readGate() == items::QuestPowerGate{}, "counted objective bound as Power");
}

/** Checks event identity, direct-counter requirements and cached binding validation. */
void verify_binding() {
    Fixture value = fixture();
    const auto binding = [&] {
        return read_prime_decryption_binding(value.definition,
                                             kSourceItem,
                                             value.definition,
                                             kItemCount,
                                             value.valueMap,
                                             value.objectiveTable);
    };
    check(binding() == QuestCounterBinding{}, "unrelated objective bound to Prime event");
    put(value.objectiveTable, kObjectiveRow, kPrimeDecryptionObjectiveHash);
    check(binding() == QuestCounterBinding{}, "comparison accepted as earned counter");
    put_array(value.objectiveTable,
              kObjectiveRow + kObjectiveExpressionOffset,
              kExpressionHeader,
              1,
              kInstructionClass);
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kEngramCount);
    const QuestCounterBinding expected{100, kEngramCount, 0, kObjectiveValueSlot};
    check(binding() == expected, "binding lost metadata stage or counter");
    namespace cache = sunrise::state::build_data::cache::records;
    sunrise::state::build_data::items::Definition item{}, decoded{};
    item.primeDecryption = expected;
    cache::ItemRecord record{};
    check(cache::encode(item, record) && cache::decode(record, decoded)
              && decoded.primeDecryption == expected,
          "cached binding round trip");
    record.primeThreshold = -1;
    check(!cache::decode(record, decoded), "negative cached threshold accepted");
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, std::int32_t{0});
    check(binding() == QuestCounterBinding{}, "zero threshold accepted");
    put(value.objectiveTable, kObjectiveRow + kObjectiveThresholdOffset, kEngramCount);
    put(value.objectiveTable,
        kExpressionRows + kInstructionOperandOffset,
        std::uint32_t{sunrise::state::kFamily5ValueSlotLimit});
    check(binding() == QuestCounterBinding{}, "unpublishable slot accepted");
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
    check(read_quest_transition(item, 15284, item, 15424, valueMap, objectives, output),
          "retained first-stage transition rejected");
    QuestTransition expected{};
    expected.sourceItemIndex = 15284;
    expected.successorItemIndex = 15285;
    expected.currentValue = 100;
    expected.nextValue = 200;
    expected.valueRow = 526;
    expected.objectives[0] = {462, 899};
    expected.objectiveCount = 1;
    expected.completionEffect = 11481;
    check(output == expected, "retained first-stage transition differs");
    using sunrise::middleware::content::packages::tables::items::read_power_quest_gate;
    check(read_power_quest_gate(item, 15284, item, 15424, valueMap, objectives)
              == sunrise::state::build_data::items::QuestPowerGate{899, 200, 15285, 11481},
          "retained first-stage Power gate differs");
    // Shared-parser extraction must leave the existing first-acquisition contract unchanged.
    const auto initial =
        sunrise::middleware::content::packages::tables::items::read_quest_initialization(
            item, 15284, item, 15424, valueMap);
    check(initial.scope == sunrise::state::build_data::items::QuestInitialization::Scope::character
              && initial.value == expected.currentValue && initial.row == expected.valueRow,
          "first-acquisition metadata regressed");
    const auto second = read_file(retainedDirectory, "81327ADC.bin");
    check(read_quest_transition(second, 15285, second, 15424, valueMap, objectives, output),
          "counted-objective stage rejected");
    expected.sourceItemIndex = 15285;
    expected.successorItemIndex = 15286;
    expected.currentValue = 200;
    expected.nextValue = 300;
    expected.objectives[0] = {13080, 3, QuestPredicate::Input::characterCounter};
    expected.objectives[1] = {13081, 2, QuestPredicate::Input::characterCounter};
    expected.objectiveCount = 2;
    expected.completionEffect = 11484;
    check(output == expected, "retained counter requirements or completion reference differ");
    check(read_prime_decryption_binding(second, 15285, second, 15424, valueMap, objectives)
              == QuestCounterBinding{200, 2, 526, 13081},
          "retained Prime objective identity or binding differs");
    check(!complete(output, Family5State{}), "decoding metadata supplied missing earned progress");
    const auto third = read_file(retainedDirectory, "81327ADF.bin");
    check(read_quest_transition(third, 15286, third, 15424, valueMap, objectives, output)
              && output.sourceItemIndex == 15286 && output.successorItemIndex == 15287
              && output.currentValue == 300 && output.nextValue == 400
              && output.objectives[0].minimumValue == 910,
          "non-first supported stage rejected");
    const auto final = read_file(retainedDirectory, "81327AE2.bin");
    check(!read_quest_transition(final, 15287, final, 15424, valueMap, objectives, output),
          "final completion accepted as a replacement");
}

/**
 * Checks account-scoped visit mechanics without authorizing any vendor reply or effect.
 * @param retainedDirectory Optional read-only installed-content fixture directory.
 */
void verify_retained_visits(const char* retainedDirectory) {
    using Scope = sunrise::state::build_data::items::QuestInitialization::Scope;
    struct Case {
        const char* file;
        std::uint16_t source, successor, stageRow, counterSlot, counterRow, completionEffect;
    };
    // Independent build-86657 item, saved-row and effect references for three first-stage visits.
    constexpr Case cases[]{
        {"81327AE5.bin", 15288, 15289, 5765, 13084, 5766, 11494},
        {"813277B4.bin", 15162, 15163, 5701, 12910, 5702, 11097},
        {"81327CAA.bin", 15392, 15393, 5858, 13213, 5859, 11814},
    };
    /** Build 86657's dense item table contains this many definitions. */
    constexpr std::size_t kRetainedItemCount = 15424;
    /** These authored first and second step identifiers are not earned counters. */
    constexpr std::int32_t kFirstStep = 100, kSecondStep = 200;
    /** Each retained visit objective completes at one. */
    constexpr std::int32_t kVisitThreshold = 1;
    const auto values = read_file(retainedDirectory, "81319320.bin");
    const auto objectives = read_file(retainedDirectory, "81319344.bin");
    for (const auto& entry : cases) {
        const auto item = read_file(retainedDirectory, entry.file);
        QuestTransition output{};
        check(read_quest_transition(
                  item, entry.source, item, kRetainedItemCount, values, objectives, output),
              "retained account visit mechanics rejected");
        QuestTransition expected{};
        expected.sourceItemIndex = entry.source;
        expected.successorItemIndex = entry.successor;
        expected.currentValue = kFirstStep;
        expected.nextValue = kSecondStep;
        expected.valueRow = entry.stageRow;
        expected.objectives[0] = {entry.counterSlot,
                                  kVisitThreshold,
                                  QuestPredicate::Input::accountCounter,
                                  entry.counterRow};
        expected.objectiveCount = 1;
        expected.completionEffect = entry.completionEffect;
        expected.scope = Scope::account;
        check(output == expected, "retained account visit contract differs");
        check(read_prime_decryption_binding(
                  item, entry.source, item, kRetainedItemCount, values, objectives)
                  == QuestCounterBinding{},
              "account visit was classified as Prime decryption");
    }
    std::puts("PASS: three retained account visit-objective contracts");
}

} // namespace

/** @param retainedDirectory Optional read-only fixture directory; null runs synthetic cases only.
 */
void verify_quest_transition_reader(const char* retainedDirectory) {
    verify_generated();
    verify_power_gate();
    verify_binding();
    if (retainedDirectory != nullptr) {
        verify_retained(retainedDirectory);
        verify_retained_visits(retainedDirectory);
        std::puts("PASS: retained stage metadata and unchanged first-acquisition contract");
    }
    std::puts("PASS: generated metadata, full predicates and unsupported cases");
}
