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
/** The synthetic completion reference remains visible to the runtime policy. */
constexpr std::uint16_t kCompletionEffect = 42;

/** Fixed offsets keep each synthetic block and nested array disjoint. */
constexpr std::size_t kObjectiveBlock = 0x100;
constexpr std::size_t kQuestSetBlock = 0x140;
constexpr std::size_t kSetRows = 0x190;
constexpr std::size_t kObjectiveReferences = 0x210;
constexpr std::size_t kObjectiveRow = 0x230;
constexpr std::size_t kExpressionRows = 0x330;
constexpr std::size_t kCharacterMapRow = 0x90;

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
    put(bytes, header - sizeof(std::uint32_t), std::uint32_t{0x80800000U});
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
    Fixture result{std::vector<std::byte>(0x300),
                   std::vector<std::byte>(0x100),
                   std::vector<std::byte>(0x400)};

    // Item +0x30 and +0x60 point at the supported objective and ordered-set blocks.
    put_block(result.definition, 0x30, kObjectiveBlock, 0x808077EBU);
    put_block(result.definition, 0x60, kQuestSetBlock, 0x808077C8U);
    put(result.definition, std::size_t{0xB8}, std::uint8_t{40});

    put_array(result.definition, kObjectiveBlock, 0x200, 1, 0x808087B1U);
    put(result.definition, kObjectiveReferences, kObjectiveIndex);
    put(result.definition, kObjectiveBlock + 0x10, kCompletionEffect);
    put(result.definition, kObjectiveBlock + 0x12, std::uint16_t{0xFFFFU});
    put(result.definition, kObjectiveBlock + 0x14, std::uint16_t{0xFFFFU});
    put(result.definition, kObjectiveBlock + 0x16, std::uint8_t{0});
    put(result.definition, kObjectiveBlock + 0x17, std::uint8_t{1});
    put(result.definition, kObjectiveBlock + 0x18, std::uint8_t{0});
    put(result.definition, kObjectiveBlock + 0x19, std::uint8_t{1});
    put(result.definition, kObjectiveBlock + 0x1A, std::uint16_t{0xFFFFU});
    put(result.definition, kObjectiveBlock + 0x1C, kSourceItem);

    put_array(result.definition, kQuestSetBlock, 0x180, 4, 0x808077CAU);
    put(result.definition, kQuestSetBlock + 0x10, kSetValueSlot);
    put(result.definition, kQuestSetBlock + 0x1C, std::uint8_t{1});
    for (std::size_t index = 0; index < 4; ++index) {
        put(result.definition, kSetRows + index * 8, static_cast<std::int32_t>((index + 1) * 100));
        put(result.definition,
            kSetRows + index * 8 + 4,
            static_cast<std::uint16_t>(kSourceItem + index));
    }

    // Only the character map contains the quest-set slot.
    put_array(result.valueMap, 24, 0x80, 1, 0x80800001U);
    put(result.valueMap, kCharacterMapRow + 4, static_cast<std::int16_t>(kSetValueSlot));

    put_array(result.objectiveTable, 8, 0x40, 4, 0x8080775FU);
    put(result.objectiveTable, kObjectiveRow + 0x30, std::int32_t{1});
    put_array(result.objectiveTable, kObjectiveRow + 8, 0x320, 3, 0x80807D31U);
    put(result.objectiveTable, kExpressionRows, std::uint32_t{10});
    put(result.objectiveTable, kExpressionRows + 4, std::uint32_t{kObjectiveValueSlot});
    put(result.objectiveTable, kExpressionRows + 8, std::uint32_t{11});
    put(result.objectiveTable, kExpressionRows + 12, kObjectiveMinimum);
    put(result.objectiveTable, kExpressionRows + 16, std::uint32_t{14});
    put(result.objectiveTable, kExpressionRows + 20, std::uint32_t{0xFFFFFFFFU});
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

    rejected(value, "truncated item accepted", kSourceItem, 0x30);

    value = fixture();
    put(value.definition, kQuestSetBlock + 0x1C, std::uint8_t{2});
    rejected(value, "unsupported quest-set mode accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * 8 + 4, kSuccessorItem);
    rejected(value, "duplicate member item accepted");

    value = fixture();
    put(value.definition, kSetRows + 6, std::uint16_t{1});
    rejected(value, "quest-set reserved field accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * 8, std::int32_t{100});
    rejected(value, "duplicate current step accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * 8, std::int32_t{200});
    rejected(value, "duplicate next step accepted");

    value = fixture();
    put(value.definition, kSetRows + 2 * 8, std::int32_t{-1});
    rejected(value, "invalid step sentinel accepted");

    rejected(fixture(), "final quest member accepted", kSourceItem + 3);

    value = fixture();
    put_array(value.valueMap, 8, 0xA0, 1, 0x80800001U);
    put(value.valueMap, std::size_t{0xB4}, static_cast<std::int16_t>(kSetValueSlot));
    rejected(value, "duplicate value mapping accepted");

    value = fixture();
    put(value.valueMap, std::size_t{24}, std::uint64_t{0});
    put(value.valueMap, std::size_t{32}, std::int64_t{0});
    put_array(value.valueMap, 8, 0xA0, 1, 0x80800001U);
    put(value.valueMap, std::size_t{0xB4}, static_cast<std::int16_t>(kSetValueSlot));
    rejected(value, "account-scoped quest value accepted");

    value = fixture();
    put(value.valueMap, kCharacterMapRow + 6, std::uint16_t{1});
    rejected(value, "mapping reserved field accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + 0x12, std::uint16_t{1});
    rejected(value, "secondary completion effect accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + 0x17, std::uint8_t{0});
    rejected(value, "any-objective mode accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock + 0x19, std::uint8_t{0});
    rejected(value, "manual completion mode accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + 0x28, std::uint8_t{1});
    rejected(value, "objective special flag accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + 0x30, std::int32_t{2});
    rejected(value, "non-boolean objective threshold accepted");

    value = fixture();
    put(value.objectiveTable, kExpressionRows, std::uint32_t{11});
    rejected(value, "reordered expression accepted");

    value = fixture();
    put(value.objectiveTable, kObjectiveRow + 8, std::uint64_t{4});
    put(value.objectiveTable, std::size_t{0x320}, std::uint64_t{4});
    rejected(value, "trailing expression instruction accepted");

    value = fixture();
    put(value.objectiveTable, kExpressionRows + 20, std::uint32_t{0});
    rejected(value, "comparison operand sentinel accepted");

    value = fixture();
    put_array(value.objectiveTable, kObjectiveRow + 0x38, 0x360, 1, 0x80807D31U);
    rejected(value, "threshold modifier accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock, std::uint64_t{2});
    put(value.definition, std::size_t{0x200}, std::uint64_t{2});
    put(value.definition, kObjectiveReferences + 2, kObjectiveIndex);
    rejected(value, "duplicate objective reference accepted");

    value = fixture();
    put(value.definition, kObjectiveBlock, std::uint64_t{17});
    put(value.definition, std::size_t{0x200}, std::uint64_t{17});
    rejected(value, "objective capacity overflow accepted");

    value = fixture();
    value.objectiveTable.resize(kExpressionRows + 20);
    rejected(value, "truncated full expression accepted");

    value = fixture();
    put(value.objectiveTable, kExpressionRows + 12, std::int32_t{-5});
    QuestTransition signedMinimum = read(value);
    check(signedMinimum.objectives[0].minimumValue == -5,
          "signed expression literal was not preserved");
    put(value.objectiveTable, kExpressionRows + 12, std::int32_t{70000});
    check(read(value).objectives[0].minimumValue == 70000,
          "expression literal narrowed to sixteen bits");

    value = fixture();
    put_array(value.objectiveTable, kObjectiveRow + 8, 0x320, 1, 0x80807D31U);
    put(value.objectiveTable, kObjectiveRow + 0x30, kChallengeCount);
    QuestTransition counted = read(value);
    check(counted.objectives[0] == QuestPredicate{kObjectiveValueSlot, kChallengeCount},
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

    put(value.objectiveTable, kObjectiveRow + 0x30, std::int32_t{70000});
    check(read(value).objectives[0].minimumValue == 70000, "counter threshold narrowed");
    put(value.objectiveTable, kObjectiveRow + 0x30, std::int32_t{-5});
    check(read(value).objectives[0].minimumValue == -5, "counter threshold lost its sign");
    family.valueCount = 0;
    check(!complete(read(value), family), "absent counter defaulted to a completing zero");

    Fixture malformedCounter = value;
    put(malformedCounter.objectiveTable, kExpressionRows + 4, std::uint32_t{0x8000U});
    rejected(malformedCounter, "out-of-range counter slot accepted");
    malformedCounter = value;
    put(malformedCounter.objectiveTable, kExpressionRows, std::uint32_t{11});
    rejected(malformedCounter, "literal-only expression accepted as a counter");
    malformedCounter = value;
    malformedCounter.objectiveTable.resize(kExpressionRows + 4);
    rejected(malformedCounter, "truncated counter operand accepted");
    put_array(value.objectiveTable, kObjectiveRow + 8, 0x320, 2, 0x80807D31U);
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
    expected.objectives[0] = {13080, 3};
    expected.objectives[1] = {13081, 2};
    expected.objectiveCount = 2;
    expected.completionEffect = 11484;
    check(output == expected, "retained counter requirements or completion reference differ");
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
