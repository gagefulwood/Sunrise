#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "state/build_data/runtime.h"
#include "state/investment/store_internal.h"
#include "state/runtime/runtime.h"
#include "state/unlocks/unlocks_records.h"

namespace {
namespace state = sunrise::state;
namespace store = state::investment::store;
/** Synthetic account and item selectors never refer to a player save. */
constexpr std::uint64_t kAccount = 1001, kCharacter = 1002;
constexpr std::uint8_t kGearBucket = 1;
/** Build-86657 emotes occupy bucket 41 and the emote plug category. */
constexpr std::uint8_t kEmoteBucket = 41;
constexpr std::uint32_t kEmoteCategory = 3054419239U;
/** Two supported permanent emotes precede one ordinary Hunter armour reward. */
constexpr std::size_t kEmotes = 2;
constexpr std::array<std::uint32_t, 3> kHashes{801733632U, 3921851413U, 1775707016U};
/** FLAG[7372] and FLAG[7373] map to these account-bank rows, not item slots. */
constexpr std::array<std::uint16_t, kEmotes> kRows{4450, 4451};
std::size_t g_capacity = 1;
std::size_t g_recordRevocations{};
bool g_missingContent{}, g_wrongEquipment{};
/** @param passed Check result. @param label Failure description. */
void check(bool passed, const char* label) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::abort();
    }
}

/** @param path Repository SQL resource. @return Complete text. */
std::string read_text(const std::string& path) {
    std::FILE* file = nullptr;
    check(fopen_s(&file, path.c_str(), "rb") == 0 && file != nullptr, "open SQL resource");
    std::string result;
    // SQL resources are streamed in 4-KiB chunks.
    std::array<char, 4096> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        result.append(buffer.data(), count);
    }
    check(std::ferror(file) == 0, "read SQL resource");
    std::fclose(file);
    return result;
}

/** Restores only the disposable fixture's inventory and flags. */
void reset() {
    g_capacity = 1;
    g_missingContent = false;
    g_wrongEquipment = false;
    state::AccountState account{};
    account.primarySoid = kAccount;
    account.characterCount = 1;
    account.characters[0].soid = kCharacter;
    account.characters[0].selected = true;
    account.characters[0].characterClass = state::CharacterClass::hunter;
    check(store::read_settings(account.settings) && store::write_account(account), "seed account");
    check(store::execute("DELETE FROM unlocks"), "clear disposable flags");
}
/** @param index Fixture emote selector. @return Its saved account ownership flag. */
std::int32_t owned(std::size_t index) {
    std::int32_t value{};
    check(store::read_unlock(store::Bank::accountFlags, kRows[index], value), "read ownership");
    return value;
}
/** @param pending Receives two permanent emotes and one ordinary item. */
void prepare(state::PendingRecordRewardGrant& pending) {
    // Fixture indices are local to the content doubles.
    const std::array<state::DirectRecordReward, 3> rewards{{{0, 1}, {1, 1}, {2, 1}}};
    check(state::prepare_record_reward_grant(rewards, state::kUnclaimedRecordIndex, pending),
          "prepare mixed reward");
}
/** Checks ownership, inventory, rollback, staleness and malformed reward refusal. */
void verify() {
    state::PendingRecordRewardGrant pending{};
    state::AccountState after{};
    state::unlocks::Table banks{};
    reset();
    prepare(pending);
    check(owned(0) == 0 && owned(1) == 0 && store::account().characters[0].inventory.count == 0,
          "prepare writes nothing");
    check(state::preview_record_reward_grant(pending, after, banks)
              && banks.accountFlags[kRows[0]] == state::unlocks::kFlagSet
              && banks.accountFlags[kRows[1]] == state::unlocks::kFlagSet
              && after.characters[0].inventory.count == 1 && owned(0) == 0,
          "preview publishes ownership without fake emote inventory");
    auto duplicate = pending;
    check(state::commit_record_reward(pending) && !pending.prepared
              && owned(0) == state::unlocks::kFlagSet && owned(1) == state::unlocks::kFlagSet
              && store::account().characters[0].inventory.count == 1,
          "mixed grant commits both ownership flags and gear");
    check(!state::commit_record_reward(duplicate), "stale duplicate refused");
    reset();
    prepare(pending);
    check(store::write_unlock(store::Bank::accountFlags, kRows[0], state::unlocks::kFlagSet),
          "change ownership after preparation");
    check(!state::commit_record_reward(pending) && owned(1) == 0
              && store::account().characters[0].inventory.count == 0,
          "stale ownership refuses whole batch");
    reset();
    prepare(pending);
    ++pending.rewards[0].stateIndex;
    check(!state::commit_record_reward(pending) && owned(0) == 0, "arbitrary unlock row refused");
    reset();
    prepare(pending);
    check(store::execute("CREATE TEMP TRIGGER refuse_reward BEFORE INSERT ON items "
                         "BEGIN SELECT RAISE(ABORT, 'test failure'); END"),
          "inject item failure");
    check(!state::commit_record_reward(pending) && owned(0) == 0 && owned(1) == 0
              && store::account().characters[0].inventory.count == 0,
          "item write failure rolls ownership back");
    check(store::execute("DROP TRIGGER refuse_reward"), "remove item failure");
    prepare(pending);
    const auto refuseSecond = "CREATE TEMP TRIGGER refuse_second BEFORE INSERT ON unlocks "
                              "WHEN NEW.slot = "
                              + std::to_string(kRows[1])
                              + " BEGIN SELECT RAISE(ABORT, 'test failure'); END";
    check(store::execute(refuseSecond.c_str()), "inject second ownership failure");
    check(!state::commit_record_reward(pending) && owned(0) == 0 && owned(1) == 0
              && store::account().characters[0].inventory.count == 0,
          "second flag failure rolls first flag back");
    check(store::execute("DROP TRIGGER refuse_second"), "remove flag failure");

    const std::array<state::DirectRecordReward, 1> emote{{{0, 1}}};
    reset();
    g_capacity = 0;
    check(state::prepare_record_reward_grant(emote, state::kUnclaimedRecordIndex, pending)
              && state::commit_record_reward(pending)
              && store::account().characters[0].inventory.count == 0,
          "permanent emote consumes no inventory capacity");
    check(state::prepare_record_reward_grant(emote, state::kUnclaimedRecordIndex, pending)
              && state::commit_record_reward(pending) && owned(0) == state::unlocks::kFlagSet,
          "already owned emote stays owned");
    reset();
    const std::array<state::DirectRecordReward, 2> repeated{{{0, 1}, {0, 1}}};
    const std::array<state::DirectRecordReward, 1> multiple{{{0, 2}}};
    check(!state::prepare_record_reward_grant(repeated, state::kUnclaimedRecordIndex, pending)
              && !pending.prepared,
          "duplicate emote rows refused");
    check(!state::prepare_record_reward_grant(multiple, state::kUnclaimedRecordIndex, pending)
              && !pending.prepared,
          "emote quantity must be one");
    g_wrongEquipment = true;
    check(!state::prepare_record_reward_grant(emote, state::kUnclaimedRecordIndex, pending),
          "unexpected equipped emote definition refused");
    g_wrongEquipment = false;
    g_missingContent = true;
    check(!state::prepare_record_reward_grant(emote, state::kUnclaimedRecordIndex, pending),
          "missing installed content refused");
    reset();
    check(store::write_unlock(store::Bank::accountFlags, kRows[0], 1), "seed invalid flag");
    check(!state::prepare_record_reward_grant(emote, state::kUnclaimedRecordIndex, pending),
          "unsupported flag encoding refused");
    check(g_recordRevocations == 0, "unclaimed batch never revokes a Triumph");
}
} // namespace

namespace sunrise::core::log {
void write(Channel, Level, std::string_view) noexcept {}
} // namespace sunrise::core::log
namespace sunrise::state::unlocks::records {
void revoke(std::uint16_t) noexcept {
    ++g_recordRevocations;
}
} // namespace sunrise::state::unlocks::records
namespace sunrise::state::build_data {
// Unrelated Season and emote paths share the reward translation unit.
bool item_definitions_ready() noexcept {
    return false;
}
bool configured_item_details_ready() noexcept {
    return false;
}
bool socket_plug_rules_ready() noexcept {
    return false;
}
bool is_socket_plug_allowed(std::uint16_t, std::uint8_t, std::uint16_t) noexcept {
    return false;
}
bool find_season_pass_reward(std::uint16_t, season_pass::Reward&) noexcept {
    return false;
}
bool find_item_definition_index(std::uint16_t index, items::Definition& definition) noexcept {
    definition = {};
    if (index >= kHashes.size() || g_missingContent) {
        return false;
    }
    definition.definitionIndex = index;
    definition.definitionHash = kHashes[index];
    definition.bucketId = index < kEmotes ? kEmoteBucket : kGearBucket;
    definition.plugCategoryHash = index < kEmotes ? kEmoteCategory : 0;
    return true;
}
bool find_item_definition_hash(std::uint32_t hash, items::Definition& definition) noexcept {
    for (std::size_t index = 0; index < kHashes.size(); ++index) {
        if (hash == kHashes[index]) {
            return find_item_definition_index(static_cast<std::uint16_t>(index), definition);
        }
    }
    return false;
}
bool find_configured_item_detail(std::uint16_t index,
                                 items::details::Definition& definition) noexcept {
    items::Definition item{};
    definition = {};
    if (!find_item_definition_index(index, item)) {
        return false;
    }
    definition.definitionIndex = index;
    definition.definitionHash = item.definitionHash;
    definition.bucketId = item.bucketId;
    if (index >= kEmotes || g_wrongEquipment) {
        definition.equipmentSlot = 0;
    }
    definition.instancedDefinitionState = items::details::InstancedDefinitionState::instanced;
    return true;
}
bool find_inventory_bucket_descriptor(std::uint8_t bucketId,
                                      inventory::buckets::Descriptor& descriptor) noexcept {
    descriptor = {};
    descriptor.arraySelector = inventory::buckets::ArraySelector::character;
    return bucketId == kGearBucket || bucketId == kEmoteBucket;
}
bool is_profile_action_source(std::uint16_t, std::uint8_t) noexcept {
    return false;
}
bool find_season_pass_package(std::uint32_t, season_pass::Package&) noexcept {
    return false;
}
bool find_collectible_definition(std::uint16_t, collectibles::Definition&) noexcept {
    return false;
}
} // namespace sunrise::state::build_data
namespace sunrise::state {
void revoke_season_pass_reward(std::uint16_t) noexcept {
    check(false, "unexpected Season revoke");
}
AccountState account_snapshot() noexcept {
    return investment::store::account();
}
} // namespace sunrise::state
namespace sunrise::middleware::datagen::family4::loadout {
/**
 * Only content resolution is doubled; inventory construction and SQLite are production code.
 * @param account Candidate account.
 * @param selectedCharacterIndex Selected character index.
 * @param output Receives fixture inventory rows.
 * @return False when the simulated bucket is full.
 */
bool resolve(const state::AccountState& account,
             std::size_t selectedCharacterIndex,
             ResolvedLoadout& output) noexcept {
    output = {};
    const auto& inventory = account.characters[selectedCharacterIndex].inventory;
    if (inventory.count > g_capacity) {
        return false;
    }
    for (std::size_t index = 0; index < inventory.count; ++index) {
        auto& row = output.items[output.itemCount++];
        row.inventoryRow = static_cast<std::uint16_t>(index);
        row.instance.instanceSoid = inventory.values[index].instanceSoid;
    }
    return true;
}
} // namespace sunrise::middleware::datagen::family4::loadout

/**
 * Uses repository schema and a newly named scratch database, never a player save.
 * @param argc Argument count.
 * @param argv Executable, SQL resources and scratch database path.
 * @return Zero after transaction and reopen checks pass.
 */
int main(int argc, char** argv) {
    check(argc == 3, "resource and scratch arguments");
    const std::string root = argv[1];
    check(store::open(argv[2],
                      read_text(root + "/investment_schema.sql"),
                      "INSERT INTO account VALUES (1,1001,0);",
                      read_text(root + "/account_settings_schema.sql"),
                      read_text(root + "/account_settings_defaults.sql")),
          "open disposable database");
    verify();
    reset();
    state::PendingRecordRewardGrant pending{};
    prepare(pending);
    check(state::commit_record_reward(pending), "commit before reopen");
    store::shutdown();
    check(store::open(argv[2], {}, {}, {}, {}), "reopen without defaults");
    check(owned(0) == state::unlocks::kFlagSet && owned(1) == state::unlocks::kFlagSet
              && store::account().characters[0].inventory.count == 1,
          "ownership and item persist");
    store::shutdown();
    std::puts("PASS: mixed rewards, permanent ownership, preview, atomic rollback, stale refusal, "
              "persistence");
}
