#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "middleware/content/packages/tables/vendor_bundle_reader.h"

namespace {
namespace tables = sunrise::middleware::content::packages::tables;
namespace bundles = sunrise::state::build_data::vendors::bundles;
using Bytes = std::vector<std::byte>;

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
    std::size_t rewardAt{}, claimAt{}, poolAt{}, claimProgram{};

    /** Build independent synthetic native arrays for one supported claim policy. */
    Fixture() {
        put(item, kSackPointer, static_cast<std::int64_t>(kSackBlock - kSackPointer));
        put(item, kSackBlock - sizeof(kSackClass), kSackClass);
        const auto parameters =
            array(item, kSackBlock + kTableArray, 1, kParameterStride, kParameterClass);
        put(item, parameters, kGroup);
        put(item, parameters + kQuantityOffset, static_cast<std::uint32_t>(bundles::kPieceCount));
        const auto list = array(rewards, kTableArray, 1, kListStride, kListClass);
        rewardAt =
            array(rewards, list + kTableArray, bundles::kPieceCount, kRewardStride, kRewardClass);
        for (std::size_t index = 0; index < bundles::kPieceCount; ++index) {
            const auto at = rewardAt + index * kRewardStride;
            put(rewards, at, static_cast<std::uint16_t>(kFirstItem + index));
            put(rewards, at + kQuantityOffset, std::uint32_t{1});
            put(rewards, at + kChildOffset, kNoChildren);
            put(rewards, at + kWeightOffset, 1.0F);
            put(rewards, at + kGroupOffset, kGroup);
        }
        const auto sale = array(vendor, kSalesArray, 1, kSaleStride, kSaleClass);
        // Native purchase order is class, unclaimed, then prerequisite pool.
        constexpr std::size_t kGateCount = 3;
        const auto expressions =
            array(vendor, sale + kPurchaseArray, kGateCount, kDescriptorBytes, kExpressionClass);
        const auto classProgram =
            array(vendor, expressions, 1, kInstructionStride, kInstructionClass);
        put(vendor, classProgram, kFlagOpcode);
        put(vendor, classProgram + sizeof(kFlagOpcode), kHunterFlag);
        claimProgram =
            array(vendor, expressions + kDescriptorBytes, 2, kInstructionStride, kInstructionClass);
        put(vendor, claimProgram, kFlagOpcode);
        put(vendor, claimProgram + sizeof(kFlagOpcode), std::uint32_t{kClaimSlot});
        put(vendor, claimProgram + kInstructionStride, kNotOpcode);
        const auto poolProgram = array(
            vendor, expressions + 2 * kDescriptorBytes, 1, kInstructionStride, kInstructionClass);
        put(vendor, poolProgram, kPoolOpcode);
        const auto pool = array(pools, kTableArray, 1, kListStride, kPoolClass);
        poolAt = array(pools,
                       pool + kTableArray,
                       bundles::kPieceCount * 2 - 1,
                       kInstructionStride,
                       kInstructionClass);
        for (std::size_t index = 0; index < bundles::kPieceCount; ++index) {
            const auto at = poolAt + index * kInstructionStride;
            put(pools, at, kFlagOpcode);
            put(pools, at + sizeof(kFlagOpcode), static_cast<std::uint32_t>(kFirstFlag + index));
        }
        for (std::size_t index = bundles::kPieceCount; index < bundles::kPieceCount * 2 - 1;
             ++index) {
            put(pools, poolAt + index * kInstructionStride, kAndOpcode);
        }
        claimAt = array(flags, kTableArray, bundles::kPieceCount + 1, kMapStride, kMapClass);
        put(flags, claimAt, bundles::kClaimEffects.front().flagHash);
        put(flags, claimAt + sizeof(std::uint32_t), kClaimSlot);
        for (std::size_t index = 0; index < bundles::kPieceCount; ++index) {
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
    check(read() && parsed.claimRow == 0 && parsed.items.front() == kFirstItem
              && parsed.requiredRows.front() == 1
              && parsed.characterClass == sunrise::state::CharacterClass::hunter,
          "relocated native members and flag mappings, not installed constants");
    auto changed = static_cast<std::uint16_t>(kFirstItem + bundles::kPieceCount);
    put(fixture.rewards, fixture.rewardAt, changed);
    check(read() && parsed.items.front() == changed, "payout follows content");
    const auto unchanged = parsed.items;
    put(fixture.rewards, fixture.rewardAt + kChildOffset, std::uint32_t{0});
    check(!read() && parsed.items == unchanged, "child payout refused without partial output");
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
    std::puts("PASS: content-driven members/gates, malformed input and unsupported contracts");
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
    std::printf("PASS: native source=%u class=%u claimRow=%u items=",
                result.sourceHash,
                static_cast<unsigned>(result.characterClass),
                result.claimRow);
    for (auto itemIndex : result.items) {
        std::printf(" %u", itemIndex);
    }
    std::printf(" prerequisiteRows=");
    for (auto row : result.requiredRows) {
        std::printf(" %u", row);
    }
    std::puts("");
}
