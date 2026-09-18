#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::items {

/** Shared grants hold at most nine members, including the Season resource package. */
inline constexpr std::size_t kBundleMemberCapacity = 9;

/** One direct reward; quantity is a count, not a random draw weight. */
struct BundleMember {
    std::uint16_t itemDefinitionIndex{};
    std::int32_t quantity{};
    bool operator==(const BundleMember&) const = default;
};

/** A complete deterministic payout; it does not prescribe when its source opens. */
struct ItemBundle {
    std::array<BundleMember, kBundleMemberCapacity> members{};
    std::size_t count{};
};

} // namespace sunrise::state::build_data::items
