#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "middleware/content/packages/tables/item_bundle_reader.h"
#include "middleware/content/packages/tables/vendor_bundle_reader.h"
#include "state/build_data/cache/records/codec.h"
#include "state/build_data/season_pass/season_pass_catalog.h"

namespace {
namespace tables = sunrise::middleware::content::packages::tables;
namespace bundles = sunrise::state::build_data::vendors::bundles;
using Bytes = std::vector<std::byte>;
/** The retained Solstice cases contain five items; this is not a parser limit. */
constexpr std::size_t kArmourPieces = 5;
/** Both retained direct-sack families use selection mode 255. */
constexpr std::uint32_t kDirectMode = 255;
constexpr std::size_t kModeOffset = 8;

/** Native descriptors have count/relative fields, preceded headers have marker/count/class. */
constexpr std::size_t kDescriptorBytes = 16, kPointerOffset = 8, kHeaderBytes = 16;
constexpr std::uint32_t kArrayMarker = 0x80809FBDU;
/** Synthetic blob prefix leaves room for the largest inline structure used here. */
constexpr std::size_t kPrefixBytes = 256;
/** Deliberately unrelated to installed item indices, pool ordinals and saved flag rows. */
constexpr std::uint16_t kFirstItem = 40, kClaimSlot = 60, kFirstFlag = 70;
constexpr std::uint32_t kGroup = 1234;
constexpr std::size_t kItemCount = 100;
/** Native serialized classes and strides, independent of the production reader's constants. */
constexpr std::uint32_t kSackClass = 0x808077CCU, kParameterClass = 0x808077CFU;
constexpr std::uint32_t kListClass = 0x8080748CU, kRewardClass = 0x8080748EU;
constexpr std::uint32_t kSaleClass = 0x80807861U, kExpressionClass = 0x80807D2FU;
constexpr std::uint32_t kInstructionClass = 0x80807D31U, kPoolClass = 0x80807C4FU;
constexpr std::uint32_t kMapClass = 0x80807D48U;
constexpr std::size_t kParameterStride = 12, kListStride = 24, kRewardStride = 80;
constexpr std::size_t kSaleStride = 184, kMapStride = 8, kInstructionStride = 8;
/** Native field offsets exercised by the synthetic content. */
constexpr std::size_t kSackPointer = 0x58, kSackBlock = 128, kTableArray = 8;
constexpr std::size_t kSalesArray = 48, kPurchaseArray = 8;
constexpr std::size_t kQuantityOffset = 4, kChildOffset = 8, kWeightOffset = 12;
constexpr std::size_t kGroupOffset = 20, kConditionOffset = 32;
/** Native expression opcodes and Hunter class predicate. */
constexpr std::uint32_t kFlagOpcode = 1, kNotOpcode = 2, kAndOpcode = 4, kPoolOpcode = 12;
constexpr std::uint32_t kHunterFlag = 239;
/** Native sentinel for a direct reward with no child selectors. */
constexpr std::uint32_t kNoChildren = 0xFFFFFFFFU;

/** @param passed Required condition. @param label Failure description. */
void check(bool passed, const char* label) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::abort();
    }
}

/** @param bytes Mutable fixture. @param at Field offset. @param value Serialized field. */
template <typename Value> void put(Bytes& bytes, std::size_t at, Value value) {
    check(at <= bytes.size() && bytes.size() - at >= sizeof value, "fixture write bounds");
    std::memcpy(bytes.data() + at, &value, sizeof value);
}

/**
 * Append one native array with a self-relative descriptor and repeated count header.
 * @param bytes Mutable fixture blob.
 * @param descriptor Descriptor position in the existing blob.
 * @param count Number of rows.
 * @param stride Bytes per row.
 * @param type Serialized element class.
 * @return Offset of the first row.
 */
std::size_t array(Bytes& bytes,
                  std::size_t descriptor,
                  std::size_t count,
                  std::size_t stride,
                  std::uint32_t type) {
    const auto marker = bytes.size();
    const auto header = marker + sizeof(kArrayMarker);
    const auto data = header + kHeaderBytes;
    bytes.resize(data + count * stride);
    put(bytes, descriptor, static_cast<std::uint64_t>(count));
    put(bytes,
        descriptor + kPointerOffset,
        static_cast<std::int64_t>(header) - static_cast<std::int64_t>(descriptor + kPointerOffset));
    put(bytes, marker, kArrayMarker);
    put(bytes, header, static_cast<std::uint64_t>(count));
    put(bytes, header + kPointerOffset, type);
    return data;
}

/** Mutable native-layout fixture with deliberately relocated content relationships. */
struct Fixture {
    Bytes item = Bytes(kPrefixBytes), rewards = Bytes(kPrefixBytes);
    Bytes vendor = Bytes(kPrefixBytes), flags = Bytes(kPrefixBytes), pools = Bytes(kPrefixBytes);
    std::size_t rewardAt{}, claimAt{}, poolAt{}, claimProgram{}, parameterAt{}, purchaseField{};

    /**
     * @param memberCount Authored direct reward count.
     * @param requiredCount Independent number of saved prerequisites.
     * @param reversed Exchange the class and pool expressions without changing their meaning.
     */
    explicit Fixture(std::size_t memberCount = kArmourPieces,
                     std::size_t requiredCount = kArmourPieces,
                     bool reversed = false) {
        put(item, kSackPointer, static_cast<std::int64_t>(kSackBlock - kSackPointer));
        put(item, kSackBlock - sizeof(kSackClass), kSackClass);
        const auto parameters =
            array(item, kSackBlock + kTableArray, 1, kParameterStride, kParameterClass);
        put(item, parameters, kGroup);
        put(item, parameters + kQuantityOffset, static_cast<std::uint32_t>(memberCount));
        parameterAt = parameters;
        put(item, parameters + kModeOffset, kDirectMode);
        const auto list = array(rewards, kTableArray, 1, kListStride, kListClass);
        rewardAt = array(rewards, list + kTableArray, memberCount, kRewardStride, kRewardClass);
        for (std::size_t index = 0; index < memberCount; ++index) {
            const auto at = rewardAt + index * kRewardStride;
            put(rewards, at, static_cast<std::uint16_t>(kFirstItem + index));
            put(rewards, at + kQuantityOffset, std::uint32_t{1});
            put(rewards, at + kChildOffset, kNoChildren);
            put(rewards, at + kWeightOffset, 1.0F);
            put(rewards, at + kGroupOffset, kGroup);
        }
        const auto sale = array(vendor, kSalesArray, 1, kSaleStride, kSaleClass);
        purchaseField = sale + kPurchaseArray;
        // Native purchase order is class, unclaimed, then prerequisite pool.
        constexpr std::size_t kGateCount = 3;
        const auto expressions =
            array(vendor, sale + kPurchaseArray, kGateCount, kDescriptorBytes, kExpressionClass);
        const auto classProgram = array(vendor,
                                        expressions + (reversed ? 2 : 0) * kDescriptorBytes,
                                        1,
                                        kInstructionStride,
                                        kInstructionClass);
        put(vendor, classProgram, kFlagOpcode);
        put(vendor, classProgram + sizeof(kFlagOpcode), kHunterFlag);
        claimProgram =
            array(vendor, expressions + kDescriptorBytes, 2, kInstructionStride, kInstructionClass);
        put(vendor, claimProgram, kFlagOpcode);
        put(vendor, claimProgram + sizeof(kFlagOpcode), std::uint32_t{kClaimSlot});
        put(vendor, claimProgram + kInstructionStride, kNotOpcode);
        const auto poolProgram = array(vendor,
                                       expressions + (reversed ? 0 : 2) * kDescriptorBytes,
                                       1,
                                       kInstructionStride,
                                       kInstructionClass);
        put(vendor, poolProgram, kPoolOpcode);
        const auto pool = array(pools, kTableArray, 1, kListStride, kPoolClass);
        poolAt = array(pools,
                       pool + kTableArray,
                       requiredCount * 2 - 1,
                       kInstructionStride,
                       kInstructionClass);
        for (std::size_t index = 0; index < requiredCount; ++index) {
            const auto at = poolAt + index * kInstructionStride;
            put(pools, at, kFlagOpcode);
            put(pools, at + sizeof(kFlagOpcode), static_cast<std::uint32_t>(kFirstFlag + index));
        }
        for (std::size_t index = requiredCount; index < requiredCount * 2 - 1; ++index) {
            put(pools, poolAt + index * kInstructionStride, kAndOpcode);
        }
        claimAt = array(flags, kTableArray, requiredCount + 1, kMapStride, kMapClass);
        put(flags, claimAt, bundles::kClaimEffects.front().flagHash);
        put(flags, claimAt + sizeof(std::uint32_t), kClaimSlot);
        for (std::size_t index = 0; index < requiredCount; ++index) {
            const auto at = claimAt + (index + 1) * kMapStride;
            put(flags, at + sizeof(std::uint32_t), static_cast<std::uint16_t>(kFirstFlag + index));
        }
        // Non-account mapping domains contain unrelated slots.
        constexpr auto kOtherMaps = std::to_array<std::size_t>({24, 40, 56, 72});
        for (auto descriptor : kOtherMaps) {
            (void)array(flags, descriptor, 1, kMapStride, kMapClass);
        }
    }

    /** @return Views of the mutable blobs used by the production parser. */
    tables::VendorBundleSource source() const {
        return {item, rewards, vendor, flags, pools, kItemCount};
    }
};

/** Reject malformed and unsupported inputs without replacing the caller's output. */
void synthetic_checks() {
    Fixture fixture;
    bundles::Definition parsed{};
    const auto read = [&] {
        return tables::read_vendor_bundle(
            fixture.source(), bundles::kClaimEffects.front(), 0, parsed);
    };
    check(read() && parsed.claimRow == 0 && parsed.requiredCount == kArmourPieces
              && parsed.rewards.members.front().itemDefinitionIndex == kFirstItem
              && parsed.requiredRows.front() == 1
              && parsed.characterClass == sunrise::state::CharacterClass::hunter,
          "relocated native members and flag mappings, not installed constants");
    fixture = Fixture{1, 1, true};
    check(read() && parsed.requiredCount == 1 && parsed.rewards.count == 1,
          "reordered gates and independent single prerequisite/member");
    fixture = Fixture{1, bundles::kRequirementCapacity};
    check(read() && parsed.requiredCount == bundles::kRequirementCapacity,
          "prerequisite count follows content rather than armour slot count");
    fixture = Fixture{1, bundles::kRequirementCapacity + 1};
    check(!read(), "excess prerequisites refuse without truncation");
    fixture = Fixture{};
    put(fixture.pools, fixture.poolAt, kPoolOpcode);
    put(fixture.pools, fixture.poolAt + sizeof(kPoolOpcode), std::uint32_t{0});
    check(!read(), "cyclic expression pool is bounded and refused");
    fixture = Fixture{};
    const auto nestedPools = array(fixture.pools, kTableArray, 2, kListStride, kPoolClass);
    const auto outer =
        array(fixture.pools, nestedPools + kTableArray, 1, kInstructionStride, kInstructionClass);
    put(fixture.pools, outer, kPoolOpcode);
    put(fixture.pools, outer + sizeof(kPoolOpcode), std::uint32_t{1});
    const auto inner = array(fixture.pools,
                             nestedPools + kListStride + kTableArray,
                             1,
                             kInstructionStride,
                             kInstructionClass);
    put(fixture.pools, inner, kFlagOpcode);
    put(fixture.pools, inner + sizeof(kFlagOpcode), std::uint32_t{kFirstFlag});
    check(read() && parsed.requiredCount == 1, "nested prerequisite pools resolve normally");
    fixture = Fixture{};
    const auto onlyGate =
        array(fixture.vendor, fixture.purchaseField, 1, kDescriptorBytes, kExpressionClass);
    const auto onlyProgram =
        array(fixture.vendor, onlyGate, 2, kInstructionStride, kInstructionClass);
    put(fixture.vendor, onlyProgram, kFlagOpcode);
    put(fixture.vendor, onlyProgram + sizeof(kFlagOpcode), std::uint32_t{kClaimSlot});
    put(fixture.vendor, onlyProgram + kInstructionStride, kNotOpcode);
    check(read() && parsed.requiredCount == 0 && !parsed.characterClass.has_value(),
          "unclaimed-only contract does not invent class or armour prerequisites");
    fixture = Fixture{};
    auto changed = static_cast<std::uint16_t>(kFirstItem + kArmourPieces);
    put(fixture.rewards, fixture.rewardAt, changed);
    check(read() && parsed.rewards.members.front().itemDefinitionIndex == changed,
          "payout follows content");
    const auto unchanged = parsed.rewards.members;
    put(fixture.rewards, fixture.rewardAt + kChildOffset, std::uint32_t{0});
    check(!read() && parsed.rewards.members == unchanged,
          "child payout refused without partial output");
    fixture = Fixture{};
    put(fixture.rewards, fixture.rewardAt + kWeightOffset, 0.5F);
    check(!read(), "weighted payout refused");
    fixture = Fixture{};
    put(fixture.rewards, fixture.rewardAt + kConditionOffset, std::uint64_t{1});
    check(!read(), "conditional payout refused");
    // Both trailing native arrays remain unsupported, not silently ignored.
    constexpr auto kTrailingArrays = std::to_array<std::size_t>({48, 64});
    for (auto offset : kTrailingArrays) {
        fixture = Fixture{};
        put(fixture.rewards, fixture.rewardAt + offset, std::uint64_t{1});
        check(!read(), "unsupported trailing payout array refused");
    }
    fixture = Fixture{};
    put(fixture.rewards, fixture.rewardAt + kRewardStride, kFirstItem);
    check(!read(), "duplicate member refused");
    fixture = Fixture{};
    put(fixture.rewards, fixture.rewardAt, static_cast<std::uint16_t>(kItemCount));
    check(!read(), "out-of-range member refused");
    fixture = Fixture{};
    put(fixture.flags, fixture.claimAt + kMapStride, bundles::kClaimEffects.front().flagHash);
    check(!read(), "ambiguous claim hash refused");
    fixture = Fixture{};
    put(fixture.flags, fixture.claimAt + kMapStride + sizeof(std::uint32_t), kClaimSlot);
    check(!read(), "ambiguous claim slot refused even with distinct hashes");
    fixture = Fixture{};
    put(fixture.vendor, fixture.claimProgram + sizeof(kFlagOpcode), std::uint32_t{kFirstFlag});
    check(!read(), "unrelated negated flag never becomes a claim effect");
    fixture = Fixture{};
    put(fixture.pools, fixture.poolAt, kNotOpcode);
    check(!read(), "unsupported prerequisite program refused");
    fixture = Fixture{};
    put(fixture.item, kSackPointer, (std::numeric_limits<std::int64_t>::max)());
    check(!read(), "overflowing block pointer refused");
    fixture = Fixture{};
    fixture.rewards.pop_back();
    check(!read(), "truncated array refused");
    fixture = Fixture{sunrise::state::build_data::items::kBundleMemberCapacity};
    for (std::size_t index = 0; index < sunrise::state::build_data::items::kBundleMemberCapacity;
         ++index) {
        // The second native case grants fifty units per material.
        put(fixture.rewards,
            fixture.rewardAt + index * kRewardStride + kQuantityOffset,
            std::uint32_t{50});
    }
    sunrise::state::build_data::items::ItemBundle contents{};
    check(tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents)
              && contents.count == sunrise::state::build_data::items::kBundleMemberCapacity
              && contents.members.back().quantity == 50,
          "nine-member quantified sack needs no wrapper identity");
    put(fixture.item, fixture.parameterAt + kModeOffset, std::uint32_t{0});
    check(!tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents),
          "unknown selection mode refused");
    fixture = Fixture{1};
    check(tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents)
              && contents.count == 1,
          "single-member sack");
    put(fixture.rewards, fixture.rewardAt + kQuantityOffset, std::uint32_t{0});
    check(!tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents),
          "zero quantity refused");
    put(fixture.rewards,
        fixture.rewardAt + kQuantityOffset,
        (std::numeric_limits<std::uint32_t>::max)());
    check(!tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents),
          "quantity overflow refused");
    fixture = Fixture{sunrise::state::build_data::items::kBundleMemberCapacity + 1};
    check(!tables::read_item_bundle(fixture.item, fixture.rewards, kItemCount, contents),
          "oversized sack refused without truncation");
    std::puts("PASS: content-driven members/quantities/gates and unsupported-contract refusal");
}

/** Quantities and opening kind survive the versioned content cache. */
void cache_checks() {
    namespace data = sunrise::state::build_data;
    data::season_pass::Package package{};
    package.definitionHash = kGroup;
    package.itemCount = static_cast<std::uint8_t>(package.items.size());
    package.directSack = true;
    for (std::size_t index = 0; index < package.itemCount; ++index) {
        package.items[index] = static_cast<std::uint32_t>(kFirstItem + index);
        // Match the native resource stack quantity, not the armour quantity.
        package.quantities[index] = 50;
    }
    data::cache::records::SeasonPassPackageRecord record{};
    data::season_pass::Package restored{};
    check(data::cache::records::encode(package, record)
              && data::cache::records::decode(record, restored) && restored.items == package.items
              && restored.quantities == package.quantities && restored.directSack
              && restored.itemCount == package.itemCount,
          "sack content cache round trip");
    const std::array<data::season_pass::Reward, 1> rewards{{{kGroup, 1}}};
    check(data::season_pass::valid(rewards, std::span(&restored, 1)), "valid cached sack");
    restored.quantities.front() = 0;
    check(!data::season_pass::valid(rewards, std::span(&restored, 1)),
          "cached empty payout refused");
    record.directSack = 2;
    check(!data::cache::records::decode(record, restored), "invalid cached opening kind refused");
}

/** @param path Retained native blob path. @return Its complete bytes. */
Bytes load(const char* path) {
    std::FILE* file = nullptr;
    check(fopen_s(&file, path, "rb") == 0 && file != nullptr, "open retained content");
    Bytes result;
    // Retained file reads use a 4-KiB transfer buffer, independent of blob size.
    constexpr std::size_t kReadChunkBytes = 4096;
    std::array<std::byte, kReadChunkBytes> chunk{};
    while (const auto count = std::fread(chunk.data(), 1, chunk.size(), file)) {
        const auto bytes = std::span(chunk).first(count);
        result.insert(result.end(), bytes.begin(), bytes.end());
    }
    check(std::ferror(file) == 0 && !result.empty(), "read nonempty retained content");
    std::fclose(file);
    return result;
}
} // namespace

/**
 * Run synthetic parser checks and optionally inspect retained native blobs.
 * @param argc Optional native file/selector arguments.
 * @param argv Argument values.
 * @return Zero when every requested check passes.
 */
int main(int argc, char** argv) {
    synthetic_checks();
    cache_checks();
    // A content-only invocation takes item, reward table and item-table count.
    if (argc == 4) {
        const auto item = load(argv[1]), rewards = load(argv[2]);
        sunrise::state::build_data::items::ItemBundle contents{};
        check(tables::read_item_bundle(item, rewards, std::stoul(argv[3]), contents),
              "native direct sack");
        std::printf("PASS: native sack count=%zu members=", contents.count);
        for (const auto& member : std::span(contents.members).first(contents.count)) {
            std::printf(" %u:%d", member.itemDefinitionIndex, member.quantity);
        }
        std::puts("");
        return 0;
    }
    if (argc == 1) {
        return 0;
    }
    // Five blobs followed by sale, policy index and item-table count.
    constexpr int kNativeArgumentCount = 9;
    check(argc == kNativeArgumentCount, "native arguments");
    const auto item = load(argv[1]), rewards = load(argv[2]), vendor = load(argv[3]);
    const auto flags = load(argv[4]), pools = load(argv[5]);
    const auto sale = std::stoul(argv[6]), effect = std::stoul(argv[7]),
               count = std::stoul(argv[8]);
    check(effect < bundles::kClaimEffects.size()
              && sale <= (std::numeric_limits<std::uint16_t>::max)(),
          "native selectors");
    bundles::Definition result{};
    check(tables::read_vendor_bundle({item, rewards, vendor, flags, pools, count},
                                     bundles::kClaimEffects[effect],
                                     static_cast<std::uint16_t>(sale),
                                     result),
          "retained native read");
    std::printf("PASS: native source=%u classRestricted=%d claimRow=%u items=",
                result.sourceHash,
                result.characterClass.has_value(),
                result.claimRow);
    for (auto member : std::span(result.rewards.members).first(result.rewards.count)) {
        std::printf(" %u:%d", member.itemDefinitionIndex, member.quantity);
    }
    std::printf(" prerequisiteRows=");
    for (auto row : std::span(result.requiredRows).first(result.requiredCount)) {
        std::printf(" %u", row);
    }
    std::puts("");
}
