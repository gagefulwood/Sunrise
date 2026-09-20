#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sunrise::state::build_data::reward_sites {

/** All site-index bits set mean the content names no Reward Site. */
inline constexpr std::uint16_t kUnavailableSiteIndex = (std::numeric_limits<std::uint16_t>::max)();
/** All item-index bits set mean the operation has no valid endpoint. */
inline constexpr std::uint16_t kUnavailableItemIndex = (std::numeric_limits<std::uint16_t>::max)();

/** Definition origin is retained so reconstructed data is never presented as recovered data. */
enum class Provenance : std::uint8_t {
    none,
    recovered,
    reconstructed,
};

/** One supported old-item to new-item operation. */
struct ItemProgression {
    std::uint32_t sourceItemHash{};
    std::uint32_t successorItemHash{};
    std::uint16_t sourceItemIndex{kUnavailableItemIndex};
    std::uint16_t successorItemIndex{kUnavailableItemIndex};
};

/** One selected-character object value compare-and-set operation. */
struct CharacterObjectTransition {
    std::int32_t expectedValue{};
    std::int32_t nextValue{};
    std::uint16_t rowIndex{};
};

/** One build-bound site and its contiguous typed operation ranges. */
struct Definition {
    std::size_t itemProgressionOffset{};
    std::size_t characterObjectTransitionOffset{};
    std::uint16_t siteIndex{kUnavailableSiteIndex};
    std::uint16_t itemProgressionCount{};
    std::uint16_t characterObjectTransitionCount{};
    Provenance provenance{Provenance::none};
};

} // namespace sunrise::state::build_data::reward_sites
