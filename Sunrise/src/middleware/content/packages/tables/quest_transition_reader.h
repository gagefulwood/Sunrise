#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../state/build_data/items/quest_counter_binding.h"
#include "../../../../state/build_data/items/quest_transition.h"

namespace sunrise::middleware::content::packages::tables::items {

/**
 * Reads one bounded non-final quest transition and its supported objective predicates.
 * @param definition Current pursuit item definition.
 * @param itemIndex Current item's item-table index.
 * @param parent Quest-set owner selected by quest_parent; may be definition itself.
 * @param itemCount Exclusive bound for item-table indices.
 * @param valueMap Blob containing all four unlock value maps.
 * @param objectiveTable Dense objective definition table.
 * @param output Cleared on failure; receives mechanics metadata on success.
 * @return False for final, malformed, ambiguous, or unsupported metadata.
 */
[[nodiscard]] bool
read_quest_transition(std::span<const std::byte> definition,
                      std::uint16_t itemIndex,
                      std::span<const std::byte> parent,
                      std::size_t itemCount,
                      std::span<const std::byte> valueMap,
                      std::span<const std::byte> objectiveTable,
                      state::build_data::items::QuestTransition& output) noexcept;

/**
 * Binds the supported Prime-decryption objective to its installed stage and counter.
 * @param definition Current pursuit item definition.
 * @param itemIndex Current item's item-table index.
 * @param parent Quest-set owner selected by quest_parent.
 * @param itemCount Exclusive item-table bound.
 * @param valueMap Installed unlock value maps.
 * @param objectiveTable Dense objective definition table.
 * @return Empty for absent, ambiguous, or unsupported metadata.
 */
[[nodiscard]] state::build_data::items::QuestCounterBinding
read_prime_decryption_binding(std::span<const std::byte> definition,
                              std::uint16_t itemIndex,
                              std::span<const std::byte> parent,
                              std::size_t itemCount,
                              std::span<const std::byte> valueMap,
                              std::span<const std::byte> objectiveTable) noexcept;

/**
 * Reads a first-stage gate only when its sole automatic objective compares character Power.
 * @param definition Current pursuit item definition.
 * @param itemIndex Current item's item-table index.
 * @param parent Quest-set owner selected by quest_parent.
 * @param itemCount Exclusive item-table bound.
 * @param valueMap Installed unlock value maps.
 * @param objectiveTable Dense objective definition table.
 * @return Empty for later stages, counted objectives or unsupported metadata.
 */
[[nodiscard]] state::build_data::items::QuestPowerGate
read_power_quest_gate(std::span<const std::byte> definition,
                      std::uint16_t itemIndex,
                      std::span<const std::byte> parent,
                      std::size_t itemCount,
                      std::span<const std::byte> valueMap,
                      std::span<const std::byte> objectiveTable) noexcept;

/**
 * Reads a supported first-stage visit counter and its incomplete-objective flag.
 * @param definition Current pursuit item definition.
 * @param itemIndex Current item's item-table index.
 * @param parent Quest-set owner selected by quest_parent.
 * @param itemCount Exclusive item-table bound.
 * @param valueMap Installed unlock value maps.
 * @param objectiveTable Dense objective definition table.
 * @return Empty for unknown event semantics, later stages or unsupported metadata.
 */
[[nodiscard]] state::build_data::items::QuestVisitGate
read_vendor_visit_gate(std::span<const std::byte> definition,
                       std::uint16_t itemIndex,
                       std::span<const std::byte> parent,
                       std::size_t itemCount,
                       std::span<const std::byte> valueMap,
                       std::span<const std::byte> objectiveTable) noexcept;

} // namespace sunrise::middleware::content::packages::tables::items
