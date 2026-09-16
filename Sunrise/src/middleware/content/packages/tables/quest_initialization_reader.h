#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../state/build_data/items/quest_initialization.h"
#include "definition_index_table.h"

namespace sunrise::middleware::content::packages::tables::items {

/** The all-one item index cannot name a quest-set owner. */
inline constexpr std::uint16_t kUnavailableQuestParent = 0xFFFFU;

/**
 * Only an objective-bearing pursuit can name a quest-set owner.
 * @param definition Item definition bytes, including its nested blocks.
 * @return The owner's item-table index, or kUnavailableQuestParent on rejection.
 */
[[nodiscard]] std::uint16_t quest_parent(std::span<const std::byte> definition) noexcept;

/**
 * Only a unique first member with one supported save-bank mapping may start a quest.
 * @param definition Pursuit item being acquired.
 * @param itemIndex Pursuit's item-table index.
 * @param parent Set-owner bytes selected by quest_parent; may be definition itself.
 * @param itemCount Exclusive bound for item-table indices.
 * @param valueMap Blob containing all four unlock value maps.
 * @return The first-step value and bank row, or an empty plan for unsupported content.
 */
[[nodiscard]] state::build_data::items::QuestInitialization
read_quest_initialization(std::span<const std::byte> definition,
                          std::uint16_t itemIndex,
                          std::span<const std::byte> parent,
                          std::size_t itemCount,
                          std::span<const std::byte> valueMap) noexcept;

namespace detail {

/** Parsed bounds and selector for one supported ordered quest set. */
struct QuestSet {
    Array members{};
    std::uint16_t valueSlot{};
};

/**
 * Reads the objective block and its bounded 16-bit reference array.
 * @param definition Item definition bytes.
 * @param blockOffset Receives the objective block payload offset; use only on success.
 * @param objectives Receives the objective-reference array; use only on success.
 * @return False for an absent, short, or wrong-class block or array.
 */
[[nodiscard]] bool read_quest_objectives(std::span<const std::byte> definition,
                                         std::size_t& blockOffset,
                                         Array& objectives) noexcept;

/**
 * Reads one mode-1 quest set whose members fit the item table.
 * @param parent Quest-set owner bytes.
 * @param itemCount Exclusive bound for member item indices and the member count.
 * @param set Receives the set bounds and value slot; use only on success.
 * @return False for malformed bounds, an unsupported mode, or an invalid slot.
 */
[[nodiscard]] bool
read_quest_set(std::span<const std::byte> parent, std::size_t itemCount, QuestSet& set) noexcept;

/**
 * Reads one quest-set member and validates its item index and reserved field.
 * @param parent Quest-set owner bytes.
 * @param set Parsed quest-set bounds.
 * @param index Member ordinal.
 * @param itemCount Exclusive bound for member item indices.
 * @param value Receives the signed step identifier; use only on success.
 * @param item Receives the item-table index; use only on success.
 * @return False when the member is absent, truncated, reserved, or out of range.
 */
[[nodiscard]] bool read_quest_member(std::span<const std::byte> parent,
                                     const QuestSet& set,
                                     std::size_t index,
                                     std::size_t itemCount,
                                     std::int32_t& value,
                                     std::uint16_t& item) noexcept;

/**
 * Resolves one unique quest-set slot across every value map.
 * @param valueMap Blob containing all four unlock value maps.
 * @param slot Authored quest-set value slot.
 * @param scope Receives the supported save bank; use only on success.
 * @param row Receives the row in that bank; use only on success.
 * @return False for malformed maps, no match, duplicate matches, or an unsupported bank.
 */
[[nodiscard]] bool map_quest_value_slot(std::span<const std::byte> valueMap,
                                        std::uint16_t slot,
                                        state::build_data::items::QuestInitialization::Scope& scope,
                                        std::uint16_t& row) noexcept;

} // namespace detail

} // namespace sunrise::middleware::content::packages::tables::items
