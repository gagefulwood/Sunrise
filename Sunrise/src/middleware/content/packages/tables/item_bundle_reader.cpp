#include "item_bundle_reader.h"

#include <algorithm>
#include <limits>

#include "definition_index_table.h"
#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {
/** Native sack block pointer and serialized block class. */
constexpr std::size_t kSackPointer = 0x58;
constexpr std::uint32_t kSackClass = 0x808077CCU;
/** Sack parameters are group hash, draw count and selection mode, in 12 bytes. */
constexpr std::size_t kParameterStride = 12;
constexpr std::uint32_t kParameterClass = 0x808077CFU;
constexpr std::size_t kParameterCountOffset = 4;
/** Sack +8 holds its parameter array; each list row also holds its entry array at +8. */
constexpr std::size_t kNestedArrayOffset = 8;
/** Reward list rows hold a hash and array descriptor; reward entries occupy 80 bytes. */
constexpr std::size_t kListStride = 24, kRewardStride = 80;
constexpr std::uint32_t kListClass = 0x8080748CU, kRewardClass = 0x8080748EU;
/** Direct reward fields: quantity, child selectors, weight, group and nested conditions. */
constexpr std::size_t kQuantityOffset = 4, kChildOffset = 8, kWeightOffset = 12;
constexpr std::size_t kGroupOffset = 20, kConditionsOffset = 32;
/** Nonempty trailing arrays have no supported payout interpretation. */
constexpr auto kTrailingArrays = std::to_array<std::size_t>({48, 64});
/** All-one selectors mean the reward has no child list or alternate payout. */
constexpr std::uint32_t kNoChildren = 0xFFFFFFFFU;
/** Deterministic direct members each have unit weight. */
constexpr float kUnitWeight = 1.0F;
/** Sack header precedes its inline parameter descriptor. */
constexpr std::size_t kSackHeaderBytes = 24;
/** Supported complete direct lists use native selection mode 255. */
constexpr std::uint32_t kDirectSelectionMode = 255;
constexpr std::size_t kParameterModeOffset = 8;

/**
 * Bound every fixed-width row before any nested field is read.
 * @param blob Blob owning the descriptor and rows.
 * @param field Descriptor offset.
 * @param type Required element class.
 * @param stride Bytes per row.
 * @param rows Receives validated bounds.
 * @return False for absent, wrong-class or truncated arrays.
 */
bool array(std::span<const std::byte> blob,
           std::size_t field,
           std::uint32_t type,
           std::size_t stride,
           Array& rows) noexcept {
    return find_array_at(blob, field, rows) && rows.elementClass == type
           && rows.dataOffset <= blob.size()
           && rows.count <= (blob.size() - rows.dataOffset) / stride;
}

} // namespace

/**
 * Read a bounded direct sack; the caller decides when its source opens.
 * @param item Native wrapper definition.
 * @param rewards Native reward-list table.
 * @param itemCount Installed item-table bound.
 * @param output Receives all members on success; unchanged on failure.
 * @return False for nested, weighted, conditional or incomplete payouts.
 */
bool read_item_bundle(std::span<const std::byte> item,
                      std::span<const std::byte> rewards,
                      std::size_t itemCount,
                      state::build_data::items::ItemBundle& output) noexcept {
    state::build_data::items::ItemBundle result{};
    std::int64_t relative{};
    if (!read(item, kSackPointer, relative) || relative == 0
        || relative > (std::numeric_limits<std::int64_t>::max)()
                          - static_cast<std::int64_t>(kSackPointer)) {
        return false;
    }
    const auto target = relative + static_cast<std::int64_t>(kSackPointer);
    if (target < static_cast<std::int64_t>(sizeof(std::uint32_t))
        || static_cast<std::uint64_t>(target) > item.size()
        || item.size() - static_cast<std::size_t>(target) < kSackHeaderBytes) {
        return false;
    }
    const auto block = static_cast<std::size_t>(target);
    std::uint32_t type{}, group{}, draws{}, mode{};
    std::uint16_t listIndex{};
    Array parameters{}, lists{}, members{};
    if (!read(item, block - sizeof(type), type) || type != kSackClass
        || !read(item, block, listIndex)
        || !array(item, block + kNestedArrayOffset, kParameterClass, kParameterStride, parameters)
        || parameters.count != 1 || !read(item, parameters.dataOffset, group)
        || !read(item, parameters.dataOffset + kParameterCountOffset, draws)
        || !read(item, parameters.dataOffset + kParameterModeOffset, mode)
        || mode != kDirectSelectionMode || draws == 0 || draws > result.members.size()
        || !array(rewards, kTableArrayDescriptor, kListClass, kListStride, lists)
        || listIndex >= lists.count
        || !array(rewards,
                  lists.dataOffset + listIndex * kListStride + kNestedArrayOffset,
                  kRewardClass,
                  kRewardStride,
                  members)
        || members.count != draws) {
        return false;
    }
    for (std::size_t index = 0; index < draws; ++index) {
        const auto at = members.dataOffset + index * kRewardStride;
        std::uint32_t quantity{}, children{}, memberGroup{};
        std::uint64_t conditions{};
        float weight{};
        auto& member = result.members[index];
        const auto prior = std::span(result.members).first(index);
        if (!read(rewards, at, member.itemDefinitionIndex)
            || member.itemDefinitionIndex >= itemCount
            || !read(rewards, at + kQuantityOffset, quantity) || quantity == 0
            || quantity > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
            || !read(rewards, at + kChildOffset, children) || children != kNoChildren
            || !read(rewards, at + kWeightOffset, weight) || weight != kUnitWeight
            || !read(rewards, at + kGroupOffset, memberGroup) || memberGroup != group
            || !read(rewards, at + kConditionsOffset, conditions) || conditions != 0
            || std::any_of(prior.begin(), prior.end(), [&](const auto& held) {
                   return held.itemDefinitionIndex == member.itemDefinitionIndex;
               })) {
            return false;
        }
        member.quantity = static_cast<std::int32_t>(quantity);
        for (auto offset : kTrailingArrays) {
            std::uint64_t count{};
            if (!read(rewards, at + offset, count) || count != 0) {
                return false;
            }
        }
    }
    result.count = draws;
    output = result;
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
