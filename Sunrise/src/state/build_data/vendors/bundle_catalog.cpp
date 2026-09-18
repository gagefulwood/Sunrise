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

/** @return True once the complete process-local bundle catalog has been published. */
bool ready() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_ready;
}

/**
 * Publish all supported effects together; incomplete extraction leaves the catalog unchanged.
 * @param definitions One installed definition per effect, in effect order.
 * @return False for missing, mismatched or out-of-bank definitions.
 */
bool replace(std::span<const Definition> definitions) noexcept {
    if (definitions.size() != kClaimEffects.size()) {
        return false;
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        if (definition.sourceHash != kClaimEffects[index].itemHash
            || definition.claimRow >= unlocks::kAccountFlagCapacity || definition.rewards.count == 0
            || definition.rewards.count > definition.rewards.members.size()
            || !std::all_of(definition.rewards.members.begin(),
                            definition.rewards.members.begin()
                                + static_cast<std::ptrdiff_t>(definition.rewards.count),
                            [](const auto& member) { return member.quantity > 0; })
            || !std::all_of(definition.requiredRows.begin(),
                            definition.requiredRows.end(),
                            [](auto row) { return row < unlocks::kAccountFlagCapacity; })) {
            return false;
        }
    }
    const std::lock_guard lock(g_mutex);
    std::copy(definitions.begin(), definitions.end(), g_definitions.begin());
    g_ready = true;
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
    if (!g_ready || found == g_definitions.end()) {
        return false;
    }
    definition = *found;
    return true;
}
} // namespace sunrise::state::build_data::vendors::bundles
