#pragma once

#include <cstdint>

#include "definition.h"

namespace sunrise::state::build_data::reward_sites {

/**
 * Finds one reconstructed row by the native Reward Site reference carried by installed content.
 * @param definitionIndex Native 16-bit Reward Site row.
 * @param definition Receives the complete row only on success.
 * @return True when that exact row has a supported reconstruction.
 */
[[nodiscard]] bool find(std::uint16_t definitionIndex, Definition& definition) noexcept;

/**
 * Finds the one automatic completion row bound to an owned source item.
 * @param sourceItemHash Installed item identity currently owned by the selected character.
 * @param definition Receives the complete row only on success.
 * @return True when exactly one reconstructed row is bound to that source.
 */
[[nodiscard]] bool find_source(std::uint32_t sourceItemHash, Definition& definition) noexcept;

} // namespace sunrise::state::build_data::reward_sites
