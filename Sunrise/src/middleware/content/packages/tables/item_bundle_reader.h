#pragma once

#include <span>

#include "../../../../state/build_data/items/item_bundle.h"

namespace sunrise::middleware::content::packages::tables {

/**
 * Read a complete direct sack without assigning ownership or acquisition effects.
 * @param item Native item definition.
 * @param rewards Native reward-list table.
 * @param itemCount Installed item-table bound.
 * @param output Receives the whole payout on success; unchanged on failure.
 * @return False for absent, malformed, weighted, nested or conditional sacks.
 */
[[nodiscard]] bool read_item_bundle(std::span<const std::byte> item,
                                    std::span<const std::byte> rewards,
                                    std::size_t itemCount,
                                    state::build_data::items::ItemBundle& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
