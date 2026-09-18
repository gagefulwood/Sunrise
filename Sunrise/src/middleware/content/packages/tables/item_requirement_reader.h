#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../state/build_data/vendors/vendor_expression.h"

namespace sunrise::middleware::content::packages::tables::items {

/** The base item and each installed plug carry separate equip requirement groups. */
enum class EquipRequirementSource : std::uint8_t {
    item,
    installedPlug,
};

/**
 * Decodes a complete requirement group for the installed item-detail catalog.
 * @param definition Whole serialized item or plug definition.
 * @param source Selects the base-item group or the plug's equip group.
 * @param output Receives the parsed group; reset to unavailable on refusal.
 * @return False for unsupported or malformed content, including capacity overflow.
 */
[[nodiscard]] bool
read_equip_requirements(std::span<const std::byte> definition,
                        EquipRequirementSource source,
                        state::build_data::vendors::ExpressionGroup& output) noexcept;

/**
 * Evaluates every expression in one base-item or installed-plug equip group.
 * @param definition Whole serialized definition of the item or selected plug, not a plug pool.
 * @param source Selects the base-item group or the plug's equip group.
 * @param inputs Reads fully resolved server flags and values; unknown inputs must return false.
 * @param satisfied Receives the AND of the expressions, true for no group; cleared on failure.
 * @return False for malformed content, unsupported instructions or unreadable state.
 */
[[nodiscard]] bool evaluate_equip_requirements(std::span<const std::byte> definition,
                                               EquipRequirementSource source,
                                               const state::build_data::vendors::Inputs& inputs,
                                               bool& satisfied) noexcept;

/**
 * Reads one flag from the content-selected identity layer, without applying other flag sources.
 * @param table Whole identity-default table from investment-root slot 15.
 * @param characterClass Character identity class byte.
 * @param race Character identity race byte.
 * @param flag Flag slot to inspect.
 * @param logical Receives active or fallthrough, never explicit false; unchanged on failure.
 * @return False for missing or duplicate identity rows, or malformed content.
 */
[[nodiscard]] bool read_identity_flag(std::span<const std::byte> table,
                                      std::uint8_t characterClass,
                                      std::uint8_t race,
                                      std::uint16_t flag,
                                      std::uint8_t& logical) noexcept;

} // namespace sunrise::middleware::content::packages::tables::items
