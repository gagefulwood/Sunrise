#pragma once

#include <cstdint>

#include "../items/quest_transition.h"

namespace sunrise::state::build_data::reward_sites {

/** The all-one row cannot name a Reward Site definition. */
inline constexpr std::uint16_t kUnavailableDefinitionIndex = 0xFFFFU;

/** One reconstructed Reward Site whose exact installed item identities guard its effects. */
struct Definition {
    std::uint16_t definitionIndex{kUnavailableDefinitionIndex};
    std::uint32_t sourceItemHash{};
    std::uint32_t successorItemHash{};
    items::QuestTransition transition{};

    bool operator==(const Definition&) const = default;
};

/**
 * Checks one reconstructed row without treating its native index as proof of its contents.
 * @param definition Candidate row and the installed item identities that constrain it.
 * @return True when the row identifies and completely describes one supported replacement.
 */
[[nodiscard]] constexpr bool valid(const Definition& definition) noexcept {
    return definition.definitionIndex != kUnavailableDefinitionIndex
           && definition.sourceItemHash != 0 && definition.successorItemHash != 0
           && definition.sourceItemHash != definition.successorItemHash
           && items::valid(definition.transition)
           && definition.transition.completionEffect == definition.definitionIndex;
}

} // namespace sunrise::state::build_data::reward_sites
