#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "state/build_data/runtime.h"
#include "state/build_data/vendors/bundle_catalog.h"
#include "state/build_data/vendors/vendor_catalog.h"
#include "state/investment/store_internal.h"
#include "state/runtime/runtime.h"
#include "state/unlocks/unlocks_records.h"

namespace {
namespace state = sunrise::state;
namespace store = state::investment::store;
using Disposition = state::VendorBundleDisposition;
namespace bundles = state::build_data::vendors::bundles;

/** Synthetic identities and selectors keep the fixture independent of player state. */
constexpr std::uint64_t kAccount = 1001, kCharacter = 1002;
constexpr std::uint16_t kVendor = 1, kWrapperIndex = 2, kFirstGearIndex = 10;
constexpr std::uint8_t kBucket = 1;
/** Build-86657 Banshee's free Upgrade category. */
constexpr std::uint32_t kBansheeHash = 672118013U;
constexpr std::int32_t kCategory = 20;
/** The native upgrade grants exactly five armour pieces. */
constexpr std::size_t kPieces = 5;
struct Fixture {
    std::uint32_t wrapper;
    std::uint16_t sale;
    state::CharacterClass characterClass;
    std::uint16_t claimRow;
    std::uint16_t firstRequiredRow;
    std::array<std::uint32_t, kPieces> items;
};
/** Expected sack hashes, sale rows, saved flags and reward-list order from build 86657. */
constexpr auto kFixtures = std::to_array<Fixture>({
    {1493877378U,
     165,
     state::CharacterClass::hunter,
     6398,
     5261,
     {1775707016U, 2805101184U, 2156817213U, 3159052337U, 2877046370U}},
    {4036562374U,
     166,
     state::CharacterClass::titan,
     6399,
     5317,
     {2291082292U, 1288683596U, 3987442049U, 1510405477U, 2578820926U}},
    {2370303981U,
     167,
     state::CharacterClass::warlock,
     6400,
     5373,
     {2127474099U, 450844637U, 2337290000U, 2546370410U, 1862324869U}},
});
const Fixture* g_fixture = &kFixtures.front();
std::size_t g_capacity = kPieces;
std::uint32_t g_cost{};
bool g_missingGear{};
std::size_t g_recordRevocations{};
bool g_resourceMode{};
std::size_t g_seasonRevocations{};
/** Native Season resource package, in reward-list order. */
constexpr std::uint32_t kResourcePackage = 3104539653U;
constexpr auto kResourceHashes = std::to_array<std::uint32_t>({
    1305274547U,
    950899352U,
    2014411539U,
    3487922223U,
    49145143U,
    31293053U,
    1177810185U,
    592227263U,
    3592324052U,
});
/** Retained resource rows grant fifty units; fixture stack capacity permits two grants. */
constexpr std::int32_t kResourceQuantity = 50, kResourceStackCapacity = 100;

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
    // SQL fixture reads use a fixed 4-KiB buffer.
    constexpr std::size_t kSqlReadChunkBytes = 4096;
    std::array<char, kSqlReadChunkBytes> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        result.append(buffer.data(), count);
    }
    check(std::ferror(file) == 0, "read SQL resource");
    std::fclose(file);
    return result;
}

/** @param fixture Class binding to seed in the disposable database. */
void reset(const Fixture& fixture) {
    g_fixture = &fixture;
    g_resourceMode = false;
    g_capacity = kPieces;
    g_cost = 0;
    g_missingGear = false;
    std::array<bundles::Definition, kFixtures.size()> definitions{};
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& expected = kFixtures[index];
        auto& row = definitions[index];
        row.sourceHash = expected.wrapper;
        row.vendorIndex = kVendor;
        row.saleIndex = expected.sale;
        row.characterClass = expected.characterClass;
        row.claimRow = expected.claimRow;
        row.rewards.count = kPieces;
        for (std::size_t piece = 0; piece < kPieces; ++piece) {
            row.requiredRows[piece] = static_cast<std::uint16_t>(expected.firstRequiredRow + piece);
            row.rewards.members[piece] = {static_cast<std::uint16_t>(kFirstGearIndex + piece), 1};
        }
    }
    check(bundles::replace(definitions), "publish synthetic installed bundles");
    state::AccountState account{};
    account.primarySoid = kAccount;
    account.characterCount = 1;
    account.characters[0].soid = kCharacter;
    account.characters[0].selected = true;
    account.characters[0].characterClass = fixture.characterClass;
    check(store::read_settings(account.settings) && store::write_account(account), "seed account");
    check(store::execute("DELETE FROM unlocks"), "clear disposable flags");
    for (std::uint16_t index = 0; index < kPieces; ++index) {
        check(store::write_unlock(store::Bank::accountFlags,
                                  static_cast<std::uint16_t>(fixture.firstRequiredRow + index),
                                  state::unlocks::kFlagSet),
              "seed prerequisite");
    }
}

/** @return This fixture's saved claim value. */
std::int32_t claimed() {
    std::int32_t value{};
    check(store::read_unlock(store::Bank::accountFlags, g_fixture->claimRow, value), "read claim");
    return value;
}

/** @param pending Receives a supported prepared fixture grant. */
void prepare(state::PendingRecordRewardGrant& pending) {
    check(state::prepare_vendor_bundle(kVendor, g_fixture->sale, pending) == Disposition::prepared,
          "prepare bundle");
}

/** Tests the three class bindings and the shared transaction's refusal paths. */
void verify() {
    state::PendingRecordRewardGrant pending{};
    state::AccountState preview{};
    state::unlocks::Table banks{};
    for (const auto& fixture : kFixtures) {
        reset(fixture);
        prepare(pending);
        check(store::account().characters[0].inventory.count == 0 && claimed() == 0,
              "prepare changes neither inventory nor claim");
        check(state::preview_record_reward_grant(pending, preview, banks)
                  && preview.characters[0].inventory.count == kPieces
                  && banks.accountFlags[fixture.claimRow] == state::unlocks::kFlagSet
                  && claimed() == 0,
              "preview includes pending claim without saving it");
        for (std::size_t index = 0; index < kPieces; ++index) {
            check(preview.characters[0].inventory.values[index].definitionHash
                      == fixture.items[index],
                  "exact five pieces in native order");
        }
        auto duplicate = pending;
        check(state::commit_record_reward(pending) && !pending.prepared
                  && store::account().characters[0].inventory.count == kPieces
                  && claimed() == state::unlocks::kFlagSet,
              "atomic grant and claim");
        check(!state::commit_record_reward(duplicate), "stale duplicate refused");
        check(state::prepare_vendor_bundle(kVendor, fixture.sale, pending) == Disposition::refused,
              "claimed bundle refused");
        for (std::uint16_t index = 0; index < kPieces; ++index) {
            reset(fixture);
            check(store::write_unlock(store::Bank::accountFlags,
                                      static_cast<std::uint16_t>(fixture.firstRequiredRow + index),
                                      0),
                  "clear one prerequisite");
            check(state::prepare_vendor_bundle(kVendor, fixture.sale, pending)
                      == Disposition::refused,
                  "each prerequisite required");
        }
        reset(fixture);
        auto account = store::account();
        account.characters[0].characterClass =
            fixture.characterClass == state::CharacterClass::titan ? state::CharacterClass::hunter
                                                                   : state::CharacterClass::titan;
        check(store::write_account(account), "change selected class");
        check(state::prepare_vendor_bundle(kVendor, fixture.sale, pending) == Disposition::refused,
              "wrong class refused");
    }
    const auto& fixture = kFixtures.front();
    reset(fixture);
    bundles::clear();
    check(!bundles::settled() && !bundles::ready()
              && state::prepare_vendor_bundle(kVendor, fixture.sale, pending)
                     == Disposition::refused,
          "unextracted wrapper never falls through to acquisition");
    bundles::unavailable();
    check(bundles::settled() && !bundles::ready()
              && state::prepare_vendor_bundle(kVendor, fixture.sale, pending)
                     == Disposition::refused,
          "unsupported content settles startup without admitting the wrapper");
    reset(fixture);
    prepare(pending);
    bundles::clear();
    check(!state::commit_record_reward(pending) && claimed() == 0,
          "catalog invalidation refuses an earlier prepared grant");
    reset(fixture);
    g_capacity = kPieces - 1;
    check(state::prepare_vendor_bundle(kVendor, fixture.sale, pending) == Disposition::refused
              && !pending.prepared && store::account().characters[0].inventory.count == 0
              && claimed() == 0,
          "full last slot leaves no partial grant or claim");
    reset(fixture);
    g_missingGear = true;
    check(state::prepare_vendor_bundle(kVendor, fixture.sale, pending) == Disposition::refused,
          "missing installed gear refused");
    reset(fixture);
    prepare(pending);
    g_cost = 1;
    check(!state::commit_record_reward(pending) && claimed() == 0, "changed price refused");
    reset(fixture);
    prepare(pending);
    check(store::write_unlock(store::Bank::accountFlags, fixture.firstRequiredRow, 0),
          "change gate");
    check(!state::commit_record_reward(pending) && claimed() == 0, "stale eligibility refused");
    reset(fixture);
    prepare(pending);
    auto changed = store::account();
    changed.characters[0].selected = false;
    check(store::write_account(changed), "change selection before commit");
    check(!state::commit_record_reward(pending) && claimed() == 0, "stale selection refused");
    reset(fixture);
    prepare(pending);
    pending.rewards[0].definitionHash = fixture.items[1];
    check(!state::commit_record_reward(pending) && claimed() == 0, "altered payout refused");
    reset(fixture);
    prepare(pending);
    check(store::execute("CREATE TEMP TRIGGER refuse_bundle BEFORE INSERT ON items "
                         "BEGIN SELECT RAISE(ABORT, 'test failure'); END"),
          "inject item write failure");
    check(!state::commit_record_reward(pending) && claimed() == 0
              && store::account().characters[0].inventory.count == 0,
          "item failure rolls claim back");
    check(store::execute("DROP TRIGGER refuse_bundle"), "remove failure injection");
    prepare(pending);
    check(store::execute("CREATE TEMP TRIGGER refuse_claim BEFORE INSERT ON unlocks "
                         "BEGIN SELECT RAISE(ABORT, 'test failure'); END"),
          "inject claim failure");
    check(!state::commit_record_reward(pending) && claimed() == 0
              && store::account().characters[0].inventory.count == 0,
          "claim failure grants nothing");
    check(store::execute("DROP TRIGGER refuse_claim"), "remove claim failure");
    check(state::prepare_vendor_bundle(kVendor, 0, pending) == Disposition::notApplicable,
          "unrelated sales unchanged");
    reset(fixture);
    const std::array<state::DirectRecordReward, 1> reward{{{kFirstGearIndex, 1}}};
    check(state::prepare_record_reward_grant(reward, state::kUnclaimedRecordIndex, pending)
              && state::preview_record_reward_grant(pending, preview, banks)
              && state::commit_record_reward(pending) && claimed() == 0,
          "ordinary batch grants do not acquire a vendor claim");
    check(g_recordRevocations == 0, "vendor refusals never revoke Triumphs");
}
/** Exercise the second consumer through the real Season commit and SQLite item store. */
void verify_resource_package() {
    reset(kFixtures.front());
    g_resourceMode = true;
    std::array<state::DirectRecordReward, kResourceHashes.size()> rewards{};
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        rewards[index] = {static_cast<std::uint16_t>(kFirstGearIndex + index), kResourceQuantity};
    }
    state::PendingSeasonPassReward season{};
    auto& batch = season.grant.emplace<state::PendingRecordRewardGrant>();
    check(state::prepare_record_reward_grant(rewards, state::kUnclaimedRecordIndex, batch),
          "prepare nine native material quantities");
    season.sourceDefinitionHash = kResourcePackage;
    season.prepared = true;
    auto incorrect = season;
    std::get<state::PendingRecordRewardGrant>(incorrect.grant).rewards.front().quantity += 1;
    check(!state::commit_season_pass_reward(incorrect) && store::account().profileItemCount == 0,
          "Season rejects a payout that differs from extracted quantities");
    check(state::commit_season_pass_reward(season)
              && store::account().profileItemCount == rewards.size(),
          "Season commits all resource stacks");
    const auto account = store::account();
    for (std::size_t index = 0; index < rewards.size(); ++index) {
        check(account.profileItems[index].definitionHash == kResourceHashes[index]
                  && account.profileItems[index].quantity == kResourceQuantity,
              "exact native resource payout");
    }
    check(g_seasonRevocations == 1 && g_recordRevocations == 0,
          "failed Season grant revokes its Season claim only");
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
namespace sunrise::state::build_data::vendors {
bool find_index(std::uint16_t index, IndexEntry& entry) noexcept {
    entry = {};
    entry.definitionHash = kBansheeHash;
    return index == kVendor;
}
bool find(std::uint32_t hash, Definition& definition) noexcept {
    definition = {};
    return hash == kBansheeHash;
}
bool sale_row(const Definition&, std::size_t row, SaleRow& output) noexcept {
    output = {};
    output.itemIndex = kWrapperIndex;
    output.categoryIndex = kCategory;
    output.costQuantity = g_cost;
    return row == g_fixture->sale;
}
} // namespace sunrise::state::build_data::vendors
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
bool find_season_pass_reward(std::uint16_t, season_pass::Reward& reward) noexcept {
    reward = {kResourcePackage, 1};
    return g_resourceMode;
}
/**
 * Only the active fixture's wrapper and available gear resolve.
 * @param index Wrapper or gear index in the active fixture.
 * @param definition Reset, then receives index and bucket, plus the fixture hash on success.
 * @return True for the wrapper or available gear; false for invalid indices or missing gear.
 */
bool find_item_definition_index(std::uint16_t index, items::Definition& definition) noexcept {
    definition = {};
    definition.definitionIndex = index;
    definition.bucketId = kBucket;
    if (g_resourceMode) {
        if (index < kFirstGearIndex || index - kFirstGearIndex >= kResourceHashes.size()) {
            return false;
        }
        definition.definitionHash = kResourceHashes[index - kFirstGearIndex];
        return true;
    }
    if (index == kWrapperIndex) {
        definition.definitionHash = g_fixture->wrapper;
        return true;
    }
    if (index < kFirstGearIndex || index - kFirstGearIndex >= kPieces || g_missingGear) {
        return false;
    }
    definition.definitionHash = g_fixture->items[index - kFirstGearIndex];
    return true;
}
bool find_item_definition_hash(std::uint32_t hash, items::Definition& definition) noexcept {
    if (g_resourceMode) {
        for (std::size_t index = 0; index < kResourceHashes.size(); ++index) {
            if (hash == kResourceHashes[index]) {
                return find_item_definition_index(
                    static_cast<std::uint16_t>(kFirstGearIndex + index), definition);
            }
        }
        return false;
    }
    for (std::uint16_t index = 0; index < kPieces; ++index) {
        if (hash == g_fixture->items[index]) {
            return find_item_definition_index(static_cast<std::uint16_t>(kFirstGearIndex + index),
                                              definition);
        }
    }
    return false;
}
/**
 * Only available fixture gear has configured details; the wrapper has none.
 * @param definitionIndex Gear definition index in the active fixture.
 * @param definition Reset on entry; receives gear details on success and stays reset on failure.
 * @return False for the wrapper, invalid indices or missing gear; true otherwise.
 */
bool find_configured_item_detail(std::uint16_t definitionIndex,
                                 items::details::Definition& definition) noexcept {
    items::Definition item{};
    definition = {};
    if (!find_item_definition_index(definitionIndex, item) || definitionIndex == kWrapperIndex) {
        return false;
    }
    definition.definitionIndex = definitionIndex;
    definition.definitionHash = item.definitionHash;
    definition.bucketId = kBucket;
    if (g_resourceMode) {
        definition.instancedDefinitionState = items::details::InstancedDefinitionState::stackable;
        definition.maxStackSize = kResourceStackCapacity;
        return true;
    }
    definition.equipmentSlot = 0;
    definition.instancedDefinitionState = items::details::InstancedDefinitionState::instanced;
    return true;
}
bool find_inventory_bucket_descriptor(std::uint8_t bucketId,
                                      inventory::buckets::Descriptor& descriptor) noexcept {
    descriptor = {};
    descriptor.arraySelector = g_resourceMode ? inventory::buckets::ArraySelector::profile
                                              : inventory::buckets::ArraySelector::character;
    descriptor.slotCount = static_cast<std::uint16_t>(kResourceHashes.size());
    return bucketId == kBucket;
}
bool is_profile_action_source(std::uint16_t, std::uint8_t) noexcept {
    return false;
}
bool find_season_pass_package(std::uint32_t hash, season_pass::Package& package) noexcept {
    package = {};
    if (!g_resourceMode || hash != kResourcePackage) {
        return false;
    }
    package.definitionHash = kResourcePackage;
    package.directSack = true;
    package.itemCount = static_cast<std::uint8_t>(kResourceHashes.size());
    for (std::size_t index = 0; index < kResourceHashes.size(); ++index) {
        package.items[index] = kResourceHashes[index];
        package.quantities[index] = kResourceQuantity;
    }
    return true;
}
bool find_collectible_definition(std::uint16_t, collectibles::Definition&) noexcept {
    return false;
}
} // namespace sunrise::state::build_data
namespace sunrise::state {
void revoke_season_pass_reward(std::uint16_t) noexcept {
    ++g_seasonRevocations;
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
    verify_resource_package();
    reset(kFixtures.front());
    state::PendingRecordRewardGrant pending{};
    prepare(pending);
    check(state::commit_record_reward(pending), "commit before reopen");
    store::shutdown();
    check(store::open(argv[2], {}, {}, {}, {}), "reopen without defaults");
    check(claimed() == state::unlocks::kFlagSet
              && store::account().characters[0].inventory.count == kPieces,
          "items and claim persist");
    store::shutdown();
    std::puts("PASS: all classes, eligibility, atomicity, stale/duplicate refusal, batch "
              "regression, persistence");
}
