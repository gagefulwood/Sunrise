#include "bundle_catalog.h"

#include <algorithm>
#include <mutex>

#include "../../unlocks/definition.h"

namespace sunrise::state::build_data::vendors::bundles {
namespace {
std::mutex g_mutex;
std::array<Definition, kClaimEffects.size()> g_definitions{};
bool g_ready{};
bool g_settled{};
} // namespace

/** Discard derived rows under the same lock used by readers. */
void clear() noexcept {
    const std::lock_guard lock(g_mutex);
    g_definitions = {};
    g_ready = false;
    g_settled = false;
}

/** Keep unavailable content unclaimable without retrying an unsupported layout each frame. */
void unavailable() noexcept {
    const std::lock_guard lock(g_mutex);
    g_definitions = {};
    g_ready = false;
    g_settled = true;
}

/** @return True after extraction succeeded or the installed layout was refused. */
bool settled() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_settled;
}

/** @return True when at least one supported offer has been published. */
bool ready() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_ready;
}

/**
 * Publish independently resolved offers; omitted wrappers remain unclaimable.
 * @param definitions Supported installed offers in any order, possibly empty.
 * @return False for duplicate, unsupported or out-of-bank definitions.
 */
bool replace(std::span<const Definition> definitions) noexcept {
    if (definitions.size() > kClaimEffects.size()) {
        return false;
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        const auto prior = definitions.first(index);
        if (std::none_of(
                kClaimEffects.begin(),
                kClaimEffects.end(),
                [&](const auto& effect) { return effect.itemHash == definition.sourceHash; })
            || std::any_of(
                prior.begin(),
                prior.end(),
                [&](const auto& held) { return held.sourceHash == definition.sourceHash; })
            || definition.claimRow >= unlocks::kAccountFlagCapacity || definition.rewards.count == 0
            || definition.rewards.count > definition.rewards.members.size()
            || !std::all_of(definition.rewards.members.begin(),
                            definition.rewards.members.begin()
                                + static_cast<std::ptrdiff_t>(definition.rewards.count),
                            [](const auto& member) { return member.quantity > 0; })
            || definition.requiredCount > definition.requiredRows.size()
            || !std::all_of(definition.requiredRows.begin(),
                            definition.requiredRows.begin()
                                + static_cast<std::ptrdiff_t>(definition.requiredCount),
                            [&](auto row) {
                                return row < unlocks::kAccountFlagCapacity
                                       && row != definition.claimRow;
                            })) {
            return false;
        }
    }
    const std::lock_guard lock(g_mutex);
    g_definitions = {};
    std::copy(definitions.begin(), definitions.end(), g_definitions.begin());
    g_ready = !definitions.empty();
    g_settled = true;
    return true;
}

/**
 * Copy a resolved bundle without exposing catalog storage to transaction callers.
 * @param sourceHash Wrapper item identity.
 * @param definition Receives the installed bundle only on success.
 * @return False until extraction succeeds or when the wrapper is unsupported.
 */
bool find(std::uint32_t sourceHash, Definition& definition) noexcept {
    const std::lock_guard lock(g_mutex);
    const auto found = std::find_if(g_definitions.begin(),
                                    g_definitions.end(),
                                    [=](const auto& row) { return row.sourceHash == sourceHash; });
    if (sourceHash == 0 || !g_ready || found == g_definitions.end()) {
        return false;
    }
    definition = *found;
    return true;
}
} // namespace sunrise::state::build_data::vendors::bundles
