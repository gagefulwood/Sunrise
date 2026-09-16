#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "middleware/datagen/definitions.h"
#include "middleware/web_service/messages/opcode2002.h"
#include "server/bap/encrypted/queuez/queuez_state_validation.h"
#include "state/investment/store_internal.h"
#include "state/progression/season_pass_reward_catalog.h"
#include "state/runtime/state_account_transaction_helpers.h"

namespace {
namespace state = sunrise::state;
namespace store = state::investment::store;
namespace pass = state::progression::season_pass;
namespace queuez = sunrise::server::bap::encrypted::queuez;
namespace codec = sunrise::middleware::web_service::messages::opcode2002;
/** Synthetic account and character identities belong only to the disposable test database. */
constexpr std::uint64_t kAccount = 1001, kCharacter = 1002, kOtherCharacter = 1003;
/** Generated-item-shaped source identity exercises all eight request bytes. */
constexpr std::uint64_t kSource = 0x4000000000000113ULL;
/** Synthetic level differs from the inventory maximum to catch Collections-level reuse. */
constexpr std::int32_t kLevel = 106;
/** Fixture catalogue rows select a weapon or a class-specific armour reward. */
constexpr std::uint16_t kWeaponIndex = 1, kTitanIndex = 2, kHunterIndex = 3;
bool g_resolverAccepts = true;

/** @param passed Required condition. @param label Failed check description. */
void check(bool passed, const char* label) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::abort();
    }
}

/** @param path Repository SQL resource. @return Complete text or aborts on an I/O error. */
std::string read_text(const std::string& path) {
    std::FILE* file = nullptr;
    check(fopen_s(&file, path.c_str(), "rb") == 0 && file != nullptr, "open SQL resource");
    std::string result;
    std::array<char, 4096> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        result.append(buffer.data(), count);
    }
    check(std::ferror(file) == 0, "read SQL resource");
    std::fclose(file);
    return result;
}

/** Resets only the in-memory save; no installed game data is opened. */
void reset_fixture() {
    state::AccountState account{};
    account.primarySoid = kAccount;
    account.characterCount = 2;
    check(store::read_settings(account.settings), "settings defaults");
    auto& character = account.characters[0];
    character.soid = kCharacter;
    character.characterClass = state::CharacterClass::titan;
    character.selected = true;
    character.nextInventorySerial = 2;
    character.inventory.count = 1;
    auto& item = character.inventory.values[0];
    item.instanceSoid = kSource;
    item.definitionHash = pass::kOwnedLegendaryEngramHash;
    item.quantity = 1;
    item.level = kLevel;
    item.mutationSerial = 1;
    account.characters[1].soid = kOtherCharacter;
    check(store::write_account(account), "seed fixture");
    g_resolverAccepts = true;
}

/** Exercises strict request bounds without assigning a meaning to the zero trailer. */
void verify_request() {
    std::array<std::byte, codec::kRequestBytes + 1> bytes{};
    sunrise::middleware::encoding::write_u64_be(std::span(bytes).first<sizeof(kSource)>(), kSource);
    sunrise::middleware::web_service::Message message{};
    message.opcode = codec::kOpcode;
    std::uint64_t soid = 0;
    for (std::size_t length = 0; length <= bytes.size(); ++length) {
        message.payload = std::span(bytes).first(length);
        check(codec::parse_request(message, soid) == (length == codec::kRequestBytes),
              "exact supported payload length");
    }
    message.payload = std::span(bytes).first(codec::kRequestBytes);
    check(codec::parse_request(message, soid) && soid == kSource, "big endian identity");
    bytes[codec::kRequestBytes - 1] = std::byte{1};
    check(!codec::parse_request(message, soid) && soid == 0, "unknown trailer refused");
    bytes.fill(std::byte{});
    check(!codec::parse_request(message, soid), "zero identity refused");
    message.opcode = 0;
    check(!codec::parse_request(message, soid), "wrong opcode refused");
}

/** Exercises the real State prepare/preview/commit and SQLite rollback boundaries. */
void verify_transaction() {
    state::PendingItemAcquisition pending{};
    state::AccountState after{};
    state::unlocks::Table unlocks{};
    reset_fixture();
    check(state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "prepare supported source");
    check(store::account().characters[0].inventory.values[0].instanceSoid == kSource,
          "prepare leaves save unchanged");
    check(state::preview_item_acquisition(pending, after, unlocks), "preview replacement");
    check(after.characters[0].inventory.count == 1
              && after.characters[0].inventory.values[0].instanceSoid == kSource + 1
              && after.characters[0].inventory.values[0].definitionHash
                     == pass::kLegendaryEngramWeapons[0]
              && after.characters[0].inventory.values[0].level == kLevel,
          "one source exchanged for one same-level reward");
    auto stale = pending;
    check(state::commit_item_acquisition(pending) && !pending.prepared,
          "atomic commit consumes pending");
    check(!state::commit_item_acquisition(pending), "pending replay refused");
    check(!state::commit_item_acquisition(stale),
          "second prepared claim refused after first commits");
    check(!state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "source replay refused");
    state::AccountState saved{};
    check(store::read_account(saved)
              && saved.characters[0].inventory.values[0].instanceSoid == kSource + 1,
          "replacement persisted in SQLite");

    reset_fixture();
    check(!state::prepare_engram_decryption(kSource, kHunterIndex, pending),
          "wrong class reward refused");
    check(state::prepare_engram_decryption(kSource, kTitanIndex, pending),
          "matching class reward accepted");
    pending.afterCharacter.inventory.values[0].level++;
    check(!state::commit_item_acquisition(pending), "tampered reward refused");
    check(store::account().characters[0].inventory.values[0].instanceSoid == kSource,
          "source retained on refusal");
    g_resolverAccepts = false;
    check(!state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "destination capacity refused");
    g_resolverAccepts = true;
    check(state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "prepare before character switch");
    saved = store::account();
    saved.characters[0].selected = false;
    saved.characters[1].selected = true;
    check(store::write_account(saved), "switch selected character");
    check(!state::commit_item_acquisition(pending), "stale selected character refused");
    check(!state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "other character source refused");

    reset_fixture();
    check(state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "prepare before database failure");
    const auto trigger = "CREATE TRIGGER refuse_reward BEFORE INSERT ON items "
                         "WHEN NEW.definition_hash != "
                         + std::to_string(pass::kOwnedLegendaryEngramHash)
                         + " BEGIN SELECT RAISE(ABORT,'test'); END";
    check(store::execute(trigger.c_str()), "install test-only failed reward write");
    check(!state::commit_item_acquisition(pending), "database failure refused");
    check(store::read_account(saved)
              && saved.characters[0].inventory.values[0].instanceSoid == kSource,
          "failed write rolls back source removal");
    check(store::execute("DROP TRIGGER refuse_reward"), "remove test trigger");
    saved.characters[0].inventory.values[0].definitionHash = pass::kExoticEngramHash;
    check(store::write_account(saved), "unsupported source fixture");
    check(!state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "unsupported source refused");
}

/** Ensures an exchange needs no spare inventory slot and ordinary grants still append. */
void verify_inventory_capacity() {
    reset_fixture();
    auto saved = store::account();
    auto& character = saved.characters[0];
    for (std::size_t index = 1; index < character.inventory.values.size(); ++index) {
        auto& item = character.inventory.values[index];
        item = character.inventory.values[0];
        item.instanceSoid += index;
        item.definitionHash = pass::kLegendaryEngramWeapons[0];
        item.level = kLevel + 1;
        item.mutationSerial = static_cast<std::int32_t>(index + 1);
    }
    character.inventory.count = character.inventory.values.size();
    character.nextInventorySerial = static_cast<std::uint32_t>(character.inventory.count + 1);
    check(store::write_account(saved), "full inventory fixture");
    state::PendingItemAcquisition pending{};
    check(state::prepare_engram_decryption(kSource, kWeaponIndex, pending)
              && pending.afterCharacter.inventory.count == character.inventory.count
              && pending.afterCharacter.inventory.values[0].level == kLevel,
          "exchange needs no extra slot and preserves source level below inventory maximum");
    check(!state::prepare_item_acquisition_for_item(kWeaponIndex, pending),
          "ordinary grant refuses full inventory");
    check(state::prepare_engram_decryption(kSource, kWeaponIndex, pending),
          "prepare before malformed count");
    pending.afterCharacter.inventory.count = pending.afterCharacter.inventory.values.size() + 1;
    check(!state::commit_item_acquisition(pending), "out-of-bounds after-image refused");
    reset_fixture();
    check(state::prepare_item_acquisition_for_item(kWeaponIndex, pending)
              && pending.consumedInstanceSoid == 0 && state::commit_item_acquisition(pending)
              && store::account().characters[0].inventory.count == 2,
          "ordinary acquisition still appends and commits");
}

/** Checks real resident staging for append, exchange, replay and full-manifest cases. */
void verify_manifest() {
    namespace data = sunrise::middleware::datagen;
    queuez::SessionState before{};
    before.family4Active = true;
    before.family4RootSoid = kAccount;
    before.family4ResidentCount = 3;
    before.family4Residents[0] = {kAccount, data::kAccountObjectId};
    before.family4Residents[1] = {kCharacter, data::kCharacterObjectId};
    before.family4Residents[2] = {kSource, data::kItemInstanceObjectId};
    queuez::ItemAcquisition staged{};
    check(queuez::stage_item_acquisition(
              before, kAccount, kCharacter, kSource + 1, false, staged, kSource)
              && staged.after.family4ResidentCount == before.family4ResidentCount
              && staged.after.family4Version == before.family4Version + 1
              && staged.after.family4Residents[2].objectSoid == kSource + 1,
          "single revision replaces source resident");
    const auto after = staged.after;
    check(!queuez::stage_item_acquisition(
              after, kAccount, kCharacter, kSource + 2, false, staged, kSource),
          "resident source replay refused");
    check(!queuez::stage_item_acquisition(
              before, kAccount, kCharacter, kSource + 1, false, staged, kAccount),
          "account cannot be consumed as an item");
    check(queuez::stage_item_acquisition(before, kAccount, kCharacter, kSource + 1, false, staged)
              && staged.after.family4ResidentCount == 4,
          "ordinary acquisition still appends");
    for (std::size_t index = before.family4ResidentCount; index < before.family4Residents.size();
         ++index) {
        before.family4Residents[index] = {kSource + index, data::kItemInstanceObjectId};
    }
    before.family4ResidentCount = static_cast<std::uint16_t>(before.family4Residents.size());
    check(queuez::stage_item_acquisition(
              before, kAccount, kCharacter, kSource + 1, false, staged, kSource),
          "replacement fits full resident manifest");
    check(!queuez::stage_item_acquisition(before, kAccount, kCharacter, kSource + 1, false, staged),
          "ordinary append refuses full manifest");
}
} // namespace

namespace sunrise::state::build_data {
/** Test catalogue exposes real pool hashes at synthetic indices. */
bool find_item_definition_index(std::uint16_t index, items::Definition& definition) noexcept {
    if (index < kWeaponIndex || index > kHunterIndex) {
        return false;
    }
    definition = {};
    definition.definitionIndex = index;
    // Synthetic equipment bucket; the resolver substitute supplies physical placement.
    definition.bucketId = 1;
    definition.definitionHash = index == kWeaponIndex  ? pass::kLegendaryEngramWeapons[0]
                                : index == kTitanIndex ? pass::kLegendaryTitanArmour[0]
                                                       : pass::kLegendaryHunterArmour[0];
    return true;
}
/** Only fixture reward hashes resolve. */
bool find_item_definition_hash(std::uint32_t hash, items::Definition& definition) noexcept {
    for (std::uint16_t index = kWeaponIndex; index <= kHunterIndex; ++index) {
        if (find_item_definition_index(index, definition) && definition.definitionHash == hash) {
            return true;
        }
    }
    return false;
}
/** The fixture provides installed, instanced equipment definitions. */
bool find_configured_item_detail(std::uint16_t definitionIndex,
                                 items::details::Definition& definition) noexcept {
    items::Definition item{};
    if (!find_item_definition_index(definitionIndex, item)) {
        return false;
    }
    definition = {};
    definition.definitionIndex = definitionIndex;
    definition.definitionHash = item.definitionHash;
    definition.bucketId = item.bucketId;
    definition.equipmentSlot = 1;
    definition.instancedDefinitionState = items::details::InstancedDefinitionState::instanced;
    return true;
}
/** Engram grants never resolve a Collections cost. */
bool find_collectible_definition(std::uint16_t, collectibles::Definition&) noexcept {
    return false;
}
/** No profile inventory is seeded by this fixture. */
bool find_inventory_bucket_descriptor(std::uint8_t, inventory::buckets::Descriptor&) noexcept {
    return false;
}
/** Profile action sources are outside this character-only fixture. */
bool is_profile_action_source(std::uint16_t, std::uint8_t) noexcept {
    return false;
}
/** Bundles are outside this owned-engram fixture. */
bool find_season_pass_package(std::uint32_t, season_pass::Package&) noexcept {
    return false;
}
} // namespace sunrise::state::build_data

namespace sunrise::state {
/** The test snapshot reads only its disposable database under the normal save lock. */
AccountState account_snapshot() noexcept {
    const std::lock_guard lock(store::g_mutex);
    return store::account();
}
} // namespace sunrise::state

namespace sunrise::core::log {
/** Offline checks do not create a game log. */
void write(Channel, Level, std::string_view) noexcept {}
} // namespace sunrise::core::log

namespace sunrise::middleware::datagen::family4::loadout {
/** Controlled native resolver substitute tests State rejection, not physical bucket layout. */
bool resolve(const state::AccountState& account,
             std::size_t characterIndex,
             ResolvedLoadout& output) noexcept {
    output = {};
    if (!g_resolverAccepts) {
        return false;
    }
    const auto& inventory = account.characters[characterIndex].inventory;
    output.itemCount = inventory.count;
    for (std::size_t index = 0; index < inventory.count; ++index) {
        output.items[index].instance.instanceSoid = inventory.values[index].instanceSoid;
        output.items[index].inventoryRow = static_cast<std::uint16_t>(index);
    }
    return true;
}
} // namespace sunrise::middleware::datagen::family4::loadout

/** Runs exclusively against a new in-memory database and synthetic content adapters. */
int main(int argc, char** argv) {
    check(argc == 2, "repository SQL directory argument");
    const std::string directory = argv[1];
    check(store::open(":memory:",
                      read_text(directory + "/investment_schema.sql"),
                      "INSERT INTO account VALUES (1,1001,0);",
                      read_text(directory + "/account_settings_schema.sql"),
                      read_text(directory + "/account_settings_defaults.sql")),
          "open disposable store");
    verify_request();
    verify_transaction();
    verify_inventory_capacity();
    verify_manifest();
    store::shutdown();
    std::puts("PASS: owned-engram request, atomic exchange, ownership, replay, rollback and "
              "resident staging");
}
