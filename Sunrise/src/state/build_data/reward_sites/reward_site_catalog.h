#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../definition.h"
#include "definition.h"

namespace sunrise::state::build_data::reward_sites {

/**
 * Loads one build's embedded definitions into an immutable typed catalog.
 * @param build Active executable identity; configured equipment does not identify static rows.
 * @param schema Complete source-controlled schema script.
 * @param definitions Complete source-controlled definition script.
 * @return True after validation, database close, and atomic catalog replacement.
 */
[[nodiscard]] bool
load(const BuildIdentity& build, std::string_view schema, std::string_view definitions) noexcept;

/** Clears every published site and operation row. */
void clear() noexcept;

/** @return True when at least one fully validated definition is published. */
[[nodiscard]] bool ready() noexcept;

/**
 * Finds one site by the native 16-bit reference.
 * @param siteIndex Native Reward Site index.
 * @param definition Receives the site and its operation ranges.
 * @return True when the active catalog contains the site.
 */
[[nodiscard]] bool find(std::uint16_t siteIndex, Definition& definition) noexcept;

/**
 * Resolves and checks every item progression owned by one site.
 * @param definition Site returned by find().
 * @param output Caller-owned operation storage.
 * @param count Receives the operation count, or zero when resolution fails.
 * @return True when the range fits and every installed item index matches its authored hash.
 */
[[nodiscard]] bool item_progressions(const Definition& definition,
                                     std::span<ItemProgression> output,
                                     std::size_t& count) noexcept;

/**
 * Copies every selected-character object transition owned by one site.
 * @param definition Site returned by find().
 * @param output Caller-owned operation storage.
 * @param count Receives the operation count, or zero when the range does not fit.
 * @return True when the current catalog still contains the complete range.
 */
[[nodiscard]] bool character_object_transitions(const Definition& definition,
                                                std::span<CharacterObjectTransition> output,
                                                std::size_t& count) noexcept;

/** @return Published site count, read under the catalog lock. */
[[nodiscard]] std::size_t count() noexcept;

} // namespace sunrise::state::build_data::reward_sites
