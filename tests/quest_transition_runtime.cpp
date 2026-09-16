#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "state/investment/store_internal.h"
#include "state/runtime/state_account_transaction_helpers.h"
#include "state/runtime/state_quest_transition_runtime.h"

void verify_quest_transition_reader(const char* retainedDirectory);

namespace {

namespace state = sunrise::state;
namespace store = state::investment::store;
namespace items = state::build_data::items;
using Policy = state::QuestTransitionPolicy;

/** Synthetic identities are not installed quest hashes or table indices. */
constexpr std::uint64_t kAccount = 1001, kCharacter = 1002, kOtherCharacter = 1003, kSource = 1004;
/** Two synthetic item rows exercise the same pursuit bucket. */
constexpr std::uint32_t kSourceHash = 2001, kSuccessorHash = 2002;
/** Synthetic step values deliberately decrease; identifiers are not progress counts. */
constexpr std::int32_t kCurrentValue = 300, kNextValue = 100;
/** One saved character bank row and one global override slot belong to this fixture. */
constexpr std::uint16_t kQuestRow = 12, kValueSlot = 17;
/** Test threshold crosses the 16-bit boundary to detect narrowed constants. */
constexpr std::int32_t kMinimumValue = 70000;
/** Fixture capacity is adjustable to test the State boundary's resolver rejection. */
std::size_t g_bucketCapacity = state::account::inventory::kCharacterItemCapacity;

/** @param passed Condition to enforce. @param label Identifies the failed check. */
void check(bool passed, const char* label) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::abort();
    }
}

/**
 * Reads only repository-owned SQL resources for a new in-memory database.
 * @param path Resource filename.
 * @return Complete resource text.
 */
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

/** @return A synthetic decoded contract with an unresolved completion effect. */
items::QuestTransition contract() {
    items::QuestTransition result{};
    result.sourceItemIndex = 0;
    result.successorItemIndex = 1;
    result.currentValue = kCurrentValue;
    result.nextValue = kNextValue;
    result.valueRow = kQuestRow;
    result.objectiveCount = 1;
    result.objectives[0] = {kValueSlot, kMinimumValue};
    result.completionEffect = 7; // Synthetic unresolved table row, not an unlock flag.
    return result;
}

/** Resets only the disposable in-memory fixture between independent failure cases. */
void reset_fixture() {
    state::AccountState account{};
    account.primarySoid = kAccount;
    account.characterCount = 2;
    check(store::read_settings(account.settings), "load settings defaults");
    account.characters[0].soid = kCharacter;
    account.characters[0].selected = true;
    account.characters[0].nextInventorySerial = 2;
    auto& inventory = account.characters[0].inventory;
    inventory.count = 1;
    inventory.values[0].instanceSoid = kSource;
    inventory.values[0].definitionHash = kSourceHash;
    inventory.values[0].quantity = 1;
    inventory.values[0].mutationSerial = 1;
    account.characters[1].soid = kOtherCharacter;
    check(store::write_account(account), "seed account");
    check(store::execute("DELETE FROM unlocks"), "clear fixture unlocks");
    check(store::write_unlock(store::Bank::characterObjectValues, kQuestRow, kCurrentValue),
          "seed current stage");
    state::Family5State family{};
    family.valueCount = 1;
    family.values[0] = {kValueSlot, kMinimumValue};
    check(store::write_family5(family), "seed objective input");
    g_bucketCapacity = state::account::inventory::kCharacterItemCapacity;
}

/** @return A prepared reconstruction without changing the database. */
state::PendingQuestTransition prepare() {
    state::PendingQuestTransition pending{};
    check(state::prepare_quest_transition(kSource, contract(), pending, Policy::reconstructLinear),
          "prepare reconstruction");
    return pending;
}

/** Checks the coupled inventory and quest row after a refused write. */
void check_unchanged() {
    state::AccountState account{};
    std::int32_t value = 0;
    check(store::read_account(account), "read unchanged account");
    const auto& inventory = account.characters[0].inventory;
    check(inventory.count == 1 && inventory.values[0].instanceSoid == kSource
              && inventory.values[0].definitionHash == kSourceHash,
          "old item retained");
    check(store::read_unlock(store::Bank::characterObjectValues, kQuestRow, value)
              && value == kCurrentValue,
          "old stage retained");
}

/** Checks the real prepare/preview/commit functions against the real SQLite store. */
void verify_runtime() {
    reset_fixture();
    state::PendingQuestTransition pending{};
    check(!state::prepare_quest_transition(kSource, contract(), pending) && !pending.prepared,
          "unresolved effects rejected by default");
    check_unchanged();

    // A replacement may use the row its source occupied in a full bucket.
    g_bucketCapacity = 1;
    pending = prepare();
    const auto replay = pending;
    state::AccountState after{};
    state::unlocks::Table unlocks{};
    check(state::preview_quest_transition(contract(), pending, after, unlocks), "preview");
    check(after.characters[0].inventory.count == 1
              && after.characters[0].inventory.values[0].definitionHash == kSuccessorHash
              && after.characters[0].inventory.values[0].instanceSoid != kSource
              && unlocks.characterObjectValues[kQuestRow] == kNextValue,
          "one combined after-image");
    check(pending.sourceInstanceSoid == kSource && pending.successorInstanceSoid != kSource,
          "old release and new instance identities differ");
    check_unchanged();
    check(state::commit_quest_transition(contract(), pending) && !pending.prepared, "commit");
    auto duplicate = replay;
    check(!state::commit_quest_transition(contract(), duplicate) && !duplicate.prepared,
          "replay cannot grant twice");
    check(store::read_account(after)
              && after.characters[0].inventory.values[0].definitionHash == kSuccessorHash,
          "committed inventory reread");
    std::int32_t saved = 0;
    check(store::read_unlock(store::Bank::characterObjectValues, kQuestRow, saved)
              && saved == kNextValue,
          "committed stage reread");

    reset_fixture();
    pending = prepare();
    check(store::execute("CREATE TEMP TRIGGER reject_stage BEFORE INSERT ON unlocks "
                         "BEGIN SELECT RAISE(ABORT, 'forced stage write failure'); END"),
          "install failure trigger");
    check(!state::commit_quest_transition(contract(), pending) && !pending.prepared,
          "second write fails");
    check(store::execute("DROP TRIGGER reject_stage"), "drop failure trigger");
    check_unchanged();

    reset_fixture();
    pending = prepare();
    state::Family5State family{};
    check(store::read_family5(family), "read input");
    ++family.values[0].value;
    check(store::write_family5(family), "change still-complete input");
    check(!state::commit_quest_transition(contract(), pending), "stale input rejected");
    check_unchanged();
    family.valueCount = 0;
    check(store::write_family5(family), "remove unknown input");
    check(!state::prepare_quest_transition(kSource, contract(), pending, Policy::reconstructLinear),
          "missing input is not complete");

    reset_fixture();
    pending = prepare();
    auto changed = contract();
    --changed.nextValue;
    check(!state::commit_quest_transition(changed, pending), "changed metadata rejected");
    check_unchanged();

    reset_fixture();
    pending = prepare();
    pending.afterCharacter.inventory.values[0].quantity = 2;
    check(!state::commit_quest_transition(contract(), pending), "altered grant rejected");
    check_unchanged();

    reset_fixture();
    pending = prepare();
    check(store::read_account(after), "read before selection");
    after.characters[0].selected = false;
    after.characters[1].selected = true;
    check(store::write_account(after), "switch character");
    check(!state::commit_quest_transition(contract(), pending), "changed character rejected");

    reset_fixture();
    check(store::read_account(after), "read before duplicate");
    auto& inventory = after.characters[0].inventory;
    inventory.values[1] = inventory.values[0];
    ++inventory.values[1].instanceSoid;
    inventory.values[1].definitionHash = kSuccessorHash;
    inventory.count = 2;
    check(store::write_account(after), "seed duplicate successor");
    check(!state::prepare_quest_transition(kSource, contract(), pending, Policy::reconstructLinear),
          "owned successor rejected");

    reset_fixture();
    pending = prepare();
    g_bucketCapacity = 0;
    check(!state::commit_quest_transition(contract(), pending), "resolver refusal prevents commit");
    check_unchanged();

    reset_fixture();
    check(store::read_account(after), "read before stacked source");
    after.characters[0].inventory.values[0].quantity = 2;
    check(store::write_account(after), "seed multi-unit source");
    check(!state::prepare_quest_transition(kSource, contract(), pending, Policy::reconstructLinear),
          "multi-unit source rejected");

    reset_fixture();
    pending = prepare();
    check(store::read_account(after), "read before serial change");
    ++after.characters[0].nextInventorySerial;
    check(store::write_account(after), "change inventory generation");
    check(!state::commit_quest_transition(contract(), pending), "stale inventory rejected");
    check_unchanged();
    std::puts("PASS: State transition, stale guards, replay and SQLite rollback");
}

} // namespace

namespace sunrise::state::build_data {

/** Test catalogue substitute; production uses installed build data. */
bool find_item_definition_index(std::uint16_t index, items::Definition& definition) noexcept {
    if (index > 1) {
        return false;
    }
    definition = {};
    definition.definitionIndex = index;
    definition.definitionHash = index == 0 ? kSourceHash : kSuccessorHash;
    definition.bucketId = items::kPursuitBucketId;
    return true;
}

/** Test catalogue lookup for the shared inventory helpers linked into this executable. */
bool find_item_definition_hash(std::uint32_t hash, items::Definition& definition) noexcept {
    if (hash != kSourceHash && hash != kSuccessorHash) {
        return false;
    }
    return find_item_definition_index(hash == kSourceHash ? 0 : 1, definition);
}

/** Native detail resolution is deliberately outside this State-only executable. */
bool find_configured_item_detail(std::uint16_t, items::details::Definition&) noexcept {
    return false;
}

} // namespace sunrise::state::build_data

namespace sunrise::core::log {

/** The offline fixture never creates a game log or debugger sink. */
void write(Channel, Level, std::string_view) noexcept {}

} // namespace sunrise::core::log

namespace sunrise::state::runtime::detail {

/** Isolates the selector from the unrelated acquisition implementation in this executable. */
std::size_t selected_character_index(const AccountState& account) noexcept {
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            return index;
        }
    }
    return account.characters.size();
}

} // namespace sunrise::state::runtime::detail

namespace sunrise::middleware::datagen::family4::loadout {

/** Controlled resolver substitute tests rejection, not installed native row placement. */
bool resolve(const state::AccountState& account,
             std::size_t characterIndex,
             ResolvedLoadout& output) noexcept {
    output = {};
    const auto& inventory = account.characters[characterIndex].inventory;
    if (inventory.count > g_bucketCapacity) {
        return false;
    }
    output.itemCount = inventory.count;
    for (std::size_t index = 0; index < inventory.count; ++index) {
        auto& row = output.items[index];
        row.instance.instanceSoid = inventory.values[index].instanceSoid;
        row.inventoryRow = static_cast<std::uint16_t>(index);
    }
    return true;
}

} // namespace sunrise::middleware::datagen::family4::loadout

/** Runs against a new in-memory database; the optional final argument is read-only metadata. */
int main(int argc, char** argv) {
    check(argc >= 2, "repository resource directory argument");
    const std::string directory = argv[1];
    const auto schema = read_text(directory + "/investment_schema.sql");
    const auto settingsSchema = read_text(directory + "/account_settings_schema.sql");
    const auto settingsDefaults = read_text(directory + "/account_settings_defaults.sql");
    check(store::open(":memory:",
                      schema,
                      "INSERT INTO account VALUES (1,1001,0);",
                      settingsSchema,
                      settingsDefaults),
          "new in-memory store");
    verify_runtime();
    verify_quest_transition_reader(argc > 2 ? argv[2] : nullptr);
    store::shutdown();
    return 0;
}
