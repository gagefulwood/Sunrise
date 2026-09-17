#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "state/build_data/runtime.h"
#include "state/build_data/vendors/vendor_catalog.h"
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
constexpr std::array kHashes{
    801733632U,
    3921851413U,
    1775707016U,
    1923236933U, // Veteran of the Hunt emblem.
    690228054U,  // Shrouded Stripes shader.
    1909657913U, // Fireteam Medallion.
    2916406440U, // Boon of the Vanguard.
    3196288028U, // Boon of the Crucible.
    2891979647U, // Finest Matterweave.
    2800872395U, // Gratitude Package source, never part of the payout.
};
/** Fixture indices separate equipment from profile stacks and the source wrapper. */
constexpr std::size_t kFirstStack = 4, kGiftSource = kHashes.size() - 1;
/** Synthetic profile bucket and stack limit exercise capacity without a player save. */
constexpr std::uint8_t kProfileBucket = 2;
constexpr std::int32_t kStackLimit = 20;
constexpr std::uint16_t kProfileCapacity = 5;
/** Build-86657 Zavala sale, category and predicate rows. */
constexpr std::uint32_t kZavala = 69482069U;
constexpr std::uint16_t kVendor = 16, kSale = 106, kEligibility = 217, kClaim = 4634;
constexpr std::int32_t kCategory = 17;
/** Deliberately synthetic quantities test the transaction, not the retail reward policy. */
constexpr std::array<state::DirectRecordReward, 8> kGiftRewards{
    {{0, 1}, {1, 1}, {3, 1}, {4, 2}, {5, 1}, {6, 3}, {7, 4}, {8, 5}}};
/** FLAG[7372] and FLAG[7373] map to these account-bank rows, not item slots. */
constexpr std::array<std::uint16_t, kEmotes> kRows{4450, 4451};
std::size_t g_capacity = 1;
std::size_t g_recordRevocations{};
bool g_missingContent{}, g_wrongEquipment{};
std::uint16_t g_profileCapacity = kProfileCapacity;
state::build_data::vendors::SaleRow g_sale{};
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
    g_profileCapacity = kProfileCapacity;
    g_sale = {};
    g_sale.categoryIndex = kCategory;
    g_sale.itemIndex = static_cast<std::uint16_t>(kGiftSource);
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

/** @return The disposable account's gift claim flag. */
std::int32_t claimed() {
    std::int32_t value{};
    check(store::read_unlock(store::Bank::accountFlags, kClaim, value), "read gift claim");
    return value;
}

/** Restores an eligible disposable account without marking the gift claimed. */
void eligible_gift() {
    reset();
    check(store::write_unlock(store::Bank::profileFlags, kEligibility, state::unlocks::kFlagSet),
          "seed fixture eligibility");
}

/** @param pending Receives a gift using synthetic, server-owned payout quantities. */
void prepare_gift(state::PendingRecordRewardGrant& pending) {
    check(state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "prepare gift with explicit fixture payout");
}

/** Checks that a gift claim cannot outlive a refused or rolled-back reward grant. */
void verify_gift() {
    state::PendingRecordRewardGrant pending{};
    state::AccountState after{};
    state::unlocks::Table banks{};
    reset();
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending)
              && !pending.prepared && claimed() == 0,
          "ineligible gift refused");
    eligible_gift();
    check(!state::prepare_gratitude_package(kVendor + 1, kSale, kGiftRewards, pending)
              && !state::prepare_gratitude_package(kVendor, kSale + 1, kGiftRewards, pending),
          "wrong vendor or sale refused");
    g_sale.costQuantity = 1;
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "unexpected cost refused");
    eligible_gift();
    g_sale.itemIndex = 0;
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "wrong source item refused");
    eligible_gift();
    auto wrong = kGiftRewards;
    wrong.back() = wrong.front();
    check(!state::prepare_gratitude_package(kVendor, kSale, wrong, pending) && !pending.prepared,
          "repeated reward cannot replace a missing one");
    wrong = kGiftRewards;
    wrong.back().itemDefinitionIndex = static_cast<std::uint16_t>(kGiftSource);
    check(!state::prepare_gratitude_package(kVendor, kSale, wrong, pending) && claimed() == 0,
          "empty wrapper cannot replace a reward");
    wrong = kGiftRewards;
    wrong.back().quantity = 0;
    check(!state::prepare_gratitude_package(kVendor, kSale, wrong, pending) && claimed() == 0,
          "unspecified quantity cannot consume claim");
    check(!state::prepare_gratitude_package(kVendor, kSale, {}, pending),
          "no implicit production payout");
    check(!state::prepare_gratitude_package(
              kVendor, kSale, std::span{kGiftRewards}.first(kGiftRewards.size() - 1), pending),
          "incomplete payout refused");
    wrong = kGiftRewards;
    wrong.back().itemDefinitionIndex = 2;
    wrong.back().quantity = 1;
    check(!state::prepare_gratitude_package(kVendor, kSale, wrong, pending) && !pending.prepared,
          "unrelated equipment cannot replace a gift reward");
    g_capacity = 0;
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending) && claimed() == 0
              && owned(0) == 0,
          "full equipment bucket refuses claim");
    eligible_gift();
    g_profileCapacity = kProfileCapacity - 1;
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending) && claimed() == 0
              && store::account().profileItemCount == 0,
          "full profile bucket refuses whole gift");
    eligible_gift();
    prepare_gift(pending);
    check(state::preview_record_reward_grant(pending, after, banks)
              && banks.accountFlags[kClaim] == state::unlocks::kFlagSet
              && banks.accountFlags[kRows[0]] == state::unlocks::kFlagSet
              && after.profileItemCount == kProfileCapacity && claimed() == 0
              && store::account().profileItemCount == 0,
          "preview includes claim and payout but writes neither");
    check(store::write_unlock(store::Bank::profileFlags, kEligibility, state::unlocks::kFlagClear),
          "revoke fixture eligibility after preparation");
    check(!state::commit_record_reward(pending) && claimed() == 0 && owned(0) == 0,
          "stale eligibility refuses gift");
    eligible_gift();
    prepare_gift(pending);
    g_sale.categoryIndex = kCategory + 1;
    check(!state::commit_record_reward(pending) && claimed() == 0,
          "changed sale refuses prepared gift");
    eligible_gift();
    prepare_gift(pending);
    check(store::write_unlock(store::Bank::accountFlags, kClaim, state::unlocks::kFlagSet),
          "another transaction claims gift");
    check(!state::commit_record_reward(pending) && owned(0) == 0
              && store::account().profileItemCount == 0,
          "stale claimed flag refuses payout without reverting the other claim");
    check(claimed() == state::unlocks::kFlagSet, "existing claim preserved");
    eligible_gift();
    prepare_gift(pending);
    const auto refuseClaim = "CREATE TEMP TRIGGER refuse_claim BEFORE INSERT ON unlocks "
                             "WHEN NEW.slot = "
                             + std::to_string(kClaim)
                             + " BEGIN SELECT RAISE(ABORT, 'test failure'); END";
    check(store::execute(refuseClaim.c_str()), "inject claim write failure");
    check(!state::commit_record_reward(pending) && claimed() == 0 && owned(0) == 0 && owned(1) == 0
              && store::account().profileItemCount == 0,
          "claim failure rolls permanent rewards back");
    check(store::execute("DROP TRIGGER refuse_claim"), "remove claim failure");
    prepare_gift(pending);
    check(store::execute("CREATE TEMP TRIGGER refuse_gift BEFORE INSERT ON items "
                         "BEGIN SELECT RAISE(ABORT, 'test failure'); END"),
          "inject inventory failure after claim write");
    check(!state::commit_record_reward(pending) && claimed() == 0 && owned(0) == 0
              && store::account().profileItemCount == 0,
          "inventory failure rolls claim and ownership back");
    check(store::execute("DROP TRIGGER refuse_gift"), "remove inventory failure");
    prepare_gift(pending);
    auto duplicate = pending;
    check(state::commit_record_reward(pending) && claimed() == state::unlocks::kFlagSet
              && store::account().characters[0].inventory.count == 1
              && store::account().profileItemCount == kProfileCapacity,
          "gift atomically commits emblem, stacks, emotes and account claim");
    check(!state::commit_record_reward(duplicate)
              && !state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "duplicate commit and second claim refused");
    auto anotherCharacter = store::account();
    ++anotherCharacter.characters[0].soid;
    check(store::write_account(anotherCharacter), "change disposable character identity");
    check(!state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "different character cannot reclaim account gift");
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
    definition.bucketId = index < kEmotes       ? kEmoteBucket
                          : index < kFirstStack ? kGearBucket
                                                : kProfileBucket;
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
    if ((index >= kEmotes && index < kFirstStack) || g_wrongEquipment) {
        definition.equipmentSlot = 0;
    }
    definition.instancedDefinitionState = index < kFirstStack
                                              ? items::details::InstancedDefinitionState::instanced
                                              : items::details::InstancedDefinitionState::stackable;
    definition.maxStackSize = kStackLimit;
    return true;
}
bool find_inventory_bucket_descriptor(std::uint8_t bucketId,
                                      inventory::buckets::Descriptor& descriptor) noexcept {
    descriptor = {};
    descriptor.arraySelector = bucketId == kProfileBucket
                                   ? inventory::buckets::ArraySelector::profile
                                   : inventory::buckets::ArraySelector::character;
    descriptor.slotCount = g_profileCapacity;
    return bucketId == kGearBucket || bucketId == kEmoteBucket || bucketId == kProfileBucket;
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
namespace sunrise::state::build_data::vendors {
bool find_index(std::uint16_t index, IndexEntry& entry) noexcept {
    entry = {};
    entry.definitionHash = kZavala;
    entry.index = index;
    return index == kVendor;
}
bool find(std::uint32_t hash, Definition& definition) noexcept {
    definition = {};
    definition.definitionHash = hash;
    definition.index = kVendor;
    return hash == kZavala;
}
bool sale_row(const Definition& definition, std::size_t row, SaleRow& output) noexcept {
    output = g_sale;
    return definition.definitionHash == kZavala && row == kSale;
}
} // namespace sunrise::state::build_data::vendors
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
    verify_gift();
    eligible_gift();
    state::PendingRecordRewardGrant pending{};
    prepare_gift(pending);
    check(state::commit_record_reward(pending), "commit before reopen");
    store::shutdown();
    check(store::open(argv[2], {}, {}, {}, {}), "reopen without defaults");
    check(owned(0) == state::unlocks::kFlagSet && owned(1) == state::unlocks::kFlagSet
              && store::account().characters[0].inventory.count == 1
              && store::account().profileItemCount == kProfileCapacity
              && claimed() == state::unlocks::kFlagSet
              && !state::prepare_gratitude_package(kVendor, kSale, kGiftRewards, pending),
          "gift ownership, inventory and claim persist together");
    for (std::size_t index = 0; index < kProfileCapacity; ++index) {
        const auto& expected = kGiftRewards[index + kGiftRewards.size() - kProfileCapacity];
        const auto& saved = store::account().profileItems[index];
        check(saved.definitionHash == kHashes[expected.itemDefinitionIndex]
                  && saved.quantity == expected.quantity,
              "every fixture payout quantity persists exactly");
    }
    store::shutdown();
    std::puts("PASS: mixed rewards, permanent ownership, preview, atomic rollback, stale refusal, "
              "gift eligibility, claim rollback, duplicate refusal, persistence");
}
