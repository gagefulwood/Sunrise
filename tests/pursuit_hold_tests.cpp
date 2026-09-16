#include <cstdio>
#include <cstdlib>
#include <limits>

#include "core/logging/log.h"
#include "state/account/pursuit_hold.h"
#include "state/build_data/runtime.h"
#include "state/runtime/runtime.h"

namespace {
namespace state = sunrise::state;
/** Synthetic catalogue identity shared by the held item and its detail row. */
constexpr std::uint32_t kItemHash = 2001;
constexpr std::uint16_t kItemIndex = 0;
state::AccountState g_account;
state::build_data::items::details::Definition g_detail;
bool g_hasDetail = true;
bool g_hasDefinition = true;
/** @param passed Required condition; remains checked in release test builds. */
void check(bool passed) {
    if (!passed) {
        std::fputs("FAIL: pursuit hold regression\n", stderr);
        std::abort();
    }
}
} // namespace

namespace sunrise::state {
/** Supplies only the account view used by the snapshot overload. */
AccountState account_snapshot() noexcept {
    return g_account;
}
} // namespace sunrise::state

namespace sunrise::state::build_data {
/** Controlled catalogue substitute; no installed content or game state is read. */
bool find_configured_item_detail(std::uint16_t definitionIndex,
                                 items::details::Definition& definition) noexcept {
    definition = g_detail;
    return definitionIndex == kItemIndex && g_hasDetail;
}
/** Controlled identity lookup for the held synthetic item. */
bool find_item_definition_index(std::uint16_t definitionIndex,
                                items::Definition& definition) noexcept {
    definition = {};
    definition.definitionHash = kItemHash;
    return definitionIndex == kItemIndex && g_hasDefinition;
}
} // namespace sunrise::state::build_data

namespace sunrise::core::log {
/** The offline check does not create a log sink. */
void writef(Channel, Level, const char*, ...) noexcept {}
} // namespace sunrise::core::log

/** Checks the real hold helper across bucket classification and selected-character ownership. */
int main() {
    g_account.characterCount = 2;
    auto& first = g_account.characters[0];
    first.selected = true;
    first.inventory.count = 1;
    first.inventory.values[0].definitionHash = kItemHash;
    g_detail.maxStackSize = 1;
    for (unsigned bucket = 0; bucket <= (std::numeric_limits<std::uint8_t>::max)(); ++bucket) {
        g_detail.bucketId = static_cast<std::uint8_t>(bucket);
        const bool held = state::account::holds_pursuit(g_account, kItemIndex);
        check(held == (bucket == state::build_data::items::kPursuitBucketId));
    }
    g_detail.bucketId = state::build_data::items::kPursuitBucketId;
    const bool snapshotHeld = state::account::holds_pursuit(kItemIndex);
    check(snapshotHeld);
    first.selected = false;
    g_account.characters[1].selected = true;
    const bool otherHeld = state::account::holds_pursuit(g_account, kItemIndex);
    check(!otherHeld);
    g_account.characters[1].selected = false;
    const bool noSelection = state::account::holds_pursuit(g_account, kItemIndex);
    check(!noSelection);
    first.selected = true;
    first.inventory.count = 0;
    const bool absent = state::account::holds_pursuit(g_account, kItemIndex);
    check(!absent);
    first.inventory.count = 1;
    g_detail.equipmentSlot = std::int8_t{0};
    const bool gear = state::account::holds_pursuit(g_account, kItemIndex);
    check(!gear);
    g_detail.equipmentSlot.reset();
    g_detail.maxStackSize = 2;
    const bool stack = state::account::holds_pursuit(g_account, kItemIndex);
    check(!stack);
    g_detail.maxStackSize = 1;
    g_hasDetail = false;
    const bool missingDetail = state::account::holds_pursuit(g_account, kItemIndex);
    check(!missingDetail);
    g_hasDetail = true;
    g_hasDefinition = false;
    const bool missingDefinition = state::account::holds_pursuit(g_account, kItemIndex);
    check(!missingDefinition);
    std::puts("PASS: pursuit buckets, quest/bounty uniqueness, ownership and missing metadata");
}
