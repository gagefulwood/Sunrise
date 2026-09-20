#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "core/logging/log.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/web_service/messages/family5_codec.h"
#include "state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "state/investment/store_internal.h"
#include "state/runtime/state_account_transaction_helpers.h"
#include "state/runtime/state_quest_transition_runtime.h"

void verify_quest_transition_reader(const char* retainedDirectory);
void verify_quest_transition_catalog();

namespace {

namespace state = sunrise::state;
namespace store = state::investment::store;
namespace items = state::build_data::items;
namespace runtime_detail = state::runtime::detail;

/** Synthetic identities are not installed quest hashes or table indices. */
constexpr std::uint64_t kAccount = 1001, kCharacter = 1002, kOtherCharacter = 1003, kSource = 1004;
/** Two synthetic item rows exercise the same pursuit bucket. */
constexpr std::uint32_t kSourceHash = 2001, kSuccessorHash = 2002;
/** Synthetic step values deliberately decrease; identifiers are not progress counts. */
constexpr std::int32_t kCurrentValue = 300, kNextValue = 100;
/** One saved character bank row and one global override slot belong to this fixture. */
constexpr std::uint16_t kQuestRow = 12, kValueSlot = 17;
/** Synthetic completion row proves unresolved effects are refused. */
constexpr std::uint16_t kUnresolvedCompletionEffect = 7;
/** The fixture Reward Site owns the supported item and character-object operations. */
constexpr std::uint16_t kRewardSiteIndex = 11481;
/** Test threshold crosses the 16-bit boundary to detect narrowed constants. */
constexpr std::int32_t kMinimumValue = 70000;
/** Unlimited Power's first retained predicate completes at equipment Power 899. */
constexpr std::int32_t kEquipmentPowerThreshold = 899;
/** Fixture capacity is adjustable to test the State boundary's resolver rejection. */
std::size_t g_bucketCapacity = state::account::inventory::kCharacterItemCapacity;
/** Controlled equipment result isolates the quest runtime's input selection. */
std::int32_t g_equipmentPower = kEquipmentPowerThreshold;
/** Tests can refuse equipment resolution without malformed account state. */
bool g_equipmentPowerAvailable = true;
/** Tests can withdraw the build-bound Reward Site without changing quest metadata. */
bool g_rewardSiteAvailable = true;
/** Tests can reject an installed-item identity mismatch at Reward Site resolution. */
bool g_rewardItemIdentitiesValid = true;

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

/** @return A synthetic decoded contract backed by the fixture Reward Site. */
items::QuestTransition contract() {
    items::QuestTransition result{};
    result.sourceItemIndex = 0;
    result.successorItemIndex = 1;
    result.currentValue = kCurrentValue;
    result.nextValue = kNextValue;
    result.valueRow = kQuestRow;
    result.objectiveCount = 1;
    result.objectives[0] = {kValueSlot, kMinimumValue};
    result.completionEffect = kRewardSiteIndex;
    return result;
}

/** @return The fixture contract with one unresolved completion effect. */
items::QuestTransition unresolved_contract() {
    auto result = contract();
    result.completionEffect = kUnresolvedCompletionEffect;
    return result;
}

/** @return The fixture contract bound to the selected character's equipment Power. */
items::QuestTransition equipment_power_contract() {
    auto result = contract();
    result.objectives[0] = {items::kEquipmentPowerValueSlot,
                            kEquipmentPowerThreshold,
                            items::QuestPredicate::Input::equipmentPower};
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
    check(store::execute("DELETE FROM character_objective_values"), "clear fixture counters");
    check(store::execute("DELETE FROM unlocks"), "clear fixture unlocks");
    check(store::write_unlock(store::Bank::characterObjectValues, kQuestRow, kCurrentValue),
          "seed current stage");
    state::Family5State family{};
    family.valueCount = 1;
    family.values[0] = {kValueSlot, kMinimumValue};
    check(store::write_family5(family), "seed objective input");
    g_bucketCapacity = state::account::inventory::kCharacterItemCapacity;
    g_equipmentPower = kEquipmentPowerThreshold;
    g_equipmentPowerAvailable = true;
    g_rewardSiteAvailable = true;
    g_rewardItemIdentitiesValid = true;
}

/**
 * Checks the real Family-5 encoder against its descriptor widths and publication values.
 * @param family Expected complete replacement, including zero rows used after a switch.
 */
void check_publication_wire(const state::Family5State& family) {
    namespace codec = sunrise::middleware::web_service::messages::family5;
    namespace bits = sunrise::middleware::encoding::bits;
    // Native descriptors interleave presence bits with each field and list member.
    constexpr std::uint8_t kPresenceBits = 1, kIdentityBits = 64, kCountBits = 7, kSlotBits = 16,
                           kValueBits = 32, kFlagBits = 2;
    // Signed descriptors add these biases before writing unsigned wire fields.
    constexpr std::uint64_t kSlotBias = 0x8000, kValueBias = 0x80000000, kFlagBias = 1;
    // Family 5 has ten fields, with its lists at indices four and five and gate at seven.
    constexpr std::size_t kFieldCount = 10, kFlagsField = 4, kValuesField = 5, kGateField = 7;
    std::array<std::byte, codec::kObjectCapacity> bytes{};
    std::size_t written = 0;
    check(codec::encode_object(family, 0, bytes, written), "encode publication snapshot");
    bits::Reader reader({bytes.data(), written});
    const auto read = [&](std::uint8_t width) {
        std::uint64_t value = 0;
        check(reader.read(width, value), "read complete wire field");
        return value;
    };
    for (std::size_t field = 0; field < kFieldCount; ++field) {
        const auto present = read(kPresenceBits);
        if (field == 0 || field == 1) {
            check(present == 1, "identity and clock present");
            check(read(kIdentityBits) == (field == 0 ? family.objectSoid : 0),
                  "identity and clock values");
        } else if (field == kFlagsField || field == kValuesField) {
            const bool flags = field == kFlagsField;
            const auto count = flags ? family.flagCount : family.valueCount;
            check(present == (count == 0 ? 0U : 1U), "list presence");
            if (count == 0) {
                continue;
            }
            check(read(kCountBits) == count, "no projected rows silently dropped");
            for (std::size_t index = 0; index < count; ++index) {
                const auto slot = flags ? family.flags[index].slot : family.values[index].slot;
                const auto value = flags ? family.flags[index].value + kFlagBias
                                         : static_cast<std::uint64_t>(
                                               static_cast<std::int64_t>(family.values[index].value)
                                               + static_cast<std::int64_t>(kValueBias));
                check(read(kPresenceBits) == 1 && read(kSlotBits) == slot + kSlotBias
                          && read(kPresenceBits) == 1
                          && read(flags ? kFlagBits : kValueBits) == value,
                      "wire override matches selected owner");
            }
        } else if (field == kGateField) {
            check(present == (family.contentGateArm ? 1U : 0U), "gate preserved");
            if (present != 0) {
                check(read(kValueBits) == 1, "content gate bit");
            }
        } else {
            check(present == 0, "unused descriptor absent");
        }
    }
    check(reader.remaining_bits() < 8, "only byte padding remains");
}

/** Checks selection replacement, unchanged persisted inputs and native publication limits. */
void verify_objective_publication() {
    reset_fixture();
    // Synthetic counter values and unrelated override rows distinguish the two owners.
    constexpr std::int32_t kFirstProgress = 3, kSecondProgress = 2, kUnrelatedValue = 9;
    constexpr std::uint16_t kUnrelatedSlot = kValueSlot + 1, kNewSlot = kValueSlot + 2;
    state::Family5State global{};
    check(store::read_family5(global), "read raw global snapshot");
    global.values[global.valueCount++] = {kUnrelatedSlot, kUnrelatedValue};
    global.flags[global.flagCount++] = {kUnrelatedSlot, 1};
    global.contentGateArm = true;
    check(store::write_family5(global), "seed unrelated global rows");
    check(store::write_character_objective(kCharacter, kValueSlot, kFirstProgress),
          "seed first owner progress");
    check(store::write_character_objective(kOtherCharacter, kNewSlot, kSecondProgress),
          "seed other owner distinct slot");
    auto publication = global;
    check(store::project_character_objectives(publication) && publication.valueCount == 3
              && publication.values[0].value == kFirstProgress
              && publication.values[1].value == kUnrelatedValue
              && publication.values[2].slot == kNewSlot && publication.values[2].value == 0
              && publication.flagCount == global.flagCount && publication.contentGateArm,
          "selected owner overlays globals and clears another owner's slot");
    check_publication_wire(publication);
    store::g_session.selected = {false, true, false};
    publication = global;
    check(store::project_character_objectives(publication) && publication.values[0].value == 0
              && publication.values[2].value == kSecondProgress,
          "switch cannot borrow the previous character or global progress");
    check_publication_wire(publication);
    std::optional<std::int32_t> missing;
    check(store::read_character_objective(kOtherCharacter, kValueSlot, missing) && !missing,
          "publication zero does not create earned or saved credit");
    store::g_session.selected = {};
    publication = global;
    check(store::project_character_objectives(publication) && publication.values[0].value == 0
              && publication.values[2].value == 0,
          "no selection clears counters without borrowing roster slot zero");
    check_publication_wire(publication);
    state::Family5State saved{};
    check(store::read_family5(saved) && saved.valueCount == global.valueCount
              && saved.values[0].value == kMinimumValue,
          "publication never changes saved global overrides");
    check(store::write_character_objective(kCharacter, kValueSlot, 0), "save explicit zero");
    store::g_session.selected = {true, false, false};
    publication = global;
    check(store::project_character_objectives(publication) && publication.values[0].value == 0,
          "saved zero is published");
    store::g_session.selected = {true, true, false};
    publication = global;
    check(!store::project_character_objectives(publication)
              && publication.values[0].value == kMinimumValue,
          "ambiguous selection leaves output unchanged");
    store::g_session.selected = {false, false, true};
    check(!store::project_character_objectives(publication), "missing selected owner rejected");
    store::g_session.selected = {true, false, false};
    check(store::write_character_objective(kCharacter, state::kFamily5ValueSlotLimit, 1),
          "storage mapping can exceed native projection range");
    check(!store::project_character_objectives(publication)
              && publication.values[0].value == kMinimumValue,
          "unpublishable slot fails without partial output");
    reset_fixture();
    check(store::write_character_objective(kCharacter, state::kFamily5ValueSlotLimit - 1, 1),
          "seed last native slot");
    publication = {};
    publication.valueCount = publication.values.size();
    for (std::size_t index = 0; index < publication.valueCount; ++index) {
        publication.values[index] = {static_cast<std::uint16_t>(index), kUnrelatedValue};
    }
    check(!store::project_character_objectives(publication)
              && publication.values[0].value == kUnrelatedValue,
          "full combined list refuses an extra slot");
    --publication.valueCount;
    check(store::project_character_objectives(publication)
              && publication.valueCount == publication.values.size(),
          "exact native row capacity and last native slot accepted");
    check_publication_wire(publication);
    std::puts("PASS: selected-character objective projection and Family-5 encoding");
}

/** @return A prepared transition without changing the database. */
state::PendingQuestTransition prepare() {
    state::PendingQuestTransition pending{};
    check(state::prepare_quest_transition(kSource, contract(), pending), "prepare transition");
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
    check(!state::prepare_quest_transition(kSource, unresolved_contract(), pending)
              && !pending.prepared,
          "unresolved effects rejected by default");
    check_unchanged();

    reset_fixture();
    g_rewardSiteAvailable = false;
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "unavailable build-bound Reward Site accepted");
    check_unchanged();

    reset_fixture();
    g_rewardItemIdentitiesValid = false;
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "Reward Site item identity mismatch accepted");
    check_unchanged();

    // A replacement may use the row its source occupied in a full bucket.
    reset_fixture();
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
    check(!state::prepare_quest_transition(kSource, contract(), pending),
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
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "owned successor rejected");

    reset_fixture();
    check(store::read_account(after), "read before missing source");
    after.characters[0].inventory = {};
    check(store::write_account(after), "remove source stage");
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "missing source stage accepted");

    reset_fixture();
    check(store::read_account(after), "read before equipping source");
    auto& equippedCharacter = after.characters[0];
    equippedCharacter.equipment.slots.front() = equippedCharacter.inventory.values[0];
    equippedCharacter.inventory = {};
    check(store::write_account(after), "equip source stage");
    check(store::read_account(after), "reread equipped source");
    runtime_detail::CharacterItemLocation sourceLocation{};
    check(after.characters[0].inventory.count == 0
              && runtime_detail::find_character_item_location(
                  after.characters[0], kSource, sourceLocation)
              && sourceLocation.equipped,
          "equipped source fixture");
    state::PendingQuestTransition equippedPending{};
    check(!state::prepare_quest_transition(kSource, contract(), equippedPending)
              && !equippedPending.prepared,
          "equipped source accepted");
    check(store::read_account(after), "read after equipped-source refusal");
    const auto& equippedSource = after.characters[0].equipment.slots.front();
    check(after.characters[0].inventory.count == 0 && equippedSource.has_value()
              && equippedSource->instanceSoid == kSource
              && equippedSource->definitionHash == kSourceHash,
          "equipped source changed inventory");
    check(store::read_unlock(store::Bank::characterObjectValues, kQuestRow, saved)
              && saved == kCurrentValue,
          "equipped source changed quest state");

    reset_fixture();
    check(store::read_account(after), "read before allocator exhaustion");
    auto& allocatorInventory = after.characters[0].inventory;
    allocatorInventory.values[1] = allocatorInventory.values[0];
    allocatorInventory.values[1].instanceSoid = (std::numeric_limits<std::uint64_t>::max)();
    allocatorInventory.values[1].definitionHash = kSuccessorHash + 1;
    allocatorInventory.count = 2;
    check(store::write_account(after), "exhaust item identity allocator");
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "item identity allocator exhaustion accepted");

    reset_fixture();
    pending = prepare();
    g_bucketCapacity = 0;
    check(!state::commit_quest_transition(contract(), pending), "resolver refusal prevents commit");
    check_unchanged();

    reset_fixture();
    check(store::read_account(after), "read before stacked source");
    after.characters[0].inventory.values[0].quantity = 2;
    check(store::write_account(after), "seed multi-unit source");
    check(!state::prepare_quest_transition(kSource, contract(), pending),
          "multi-unit source rejected");

    reset_fixture();
    pending = prepare();
    check(store::write_unlock(store::Bank::characterObjectValues, kQuestRow, kCurrentValue + 1),
          "change quest stage value");
    check(!state::commit_quest_transition(contract(), pending), "stale quest state accepted");
    check(store::read_account(after)
              && after.characters[0].inventory.values[0].definitionHash == kSourceHash,
          "stale quest state changed inventory");

    reset_fixture();
    pending = prepare();
    check(store::read_account(after), "read before serial change");
    ++after.characters[0].nextInventorySerial;
    check(store::write_account(after), "change inventory generation");
    check(!state::commit_quest_transition(contract(), pending), "stale inventory rejected");
    check_unchanged();
    std::puts("PASS: State transition, stale guards, replay and SQLite rollback");
}

/** Proves one explicit progression event can discover an owned supported stage. */
void verify_event_preparation() {
    reset_fixture();
    state::PendingQuestTransition pending{};
    check(state::prepare_completed_quest_transition(pending) && pending.prepared
              && pending.sourceInstanceSoid == kSource
              && pending.transition.completionEffect == kRewardSiteIndex,
          "owned completed stage was not prepared from the event adapter");

    state::Family5State family{};
    check(store::write_family5(family), "clear event predicate");
    check(!state::prepare_completed_quest_transition(pending) && !pending.prepared,
          "incomplete stage was prepared by the event adapter");
    std::puts("PASS: event-driven owned-stage discovery without login polling");
}

/** Proves equipment Power, not a same-slot Family-5 override, controls the predicate. */
void verify_equipment_power_input() {
    reset_fixture();
    state::Family5State misleading{};
    misleading.valueCount = 1;
    misleading.values[0] = {items::kEquipmentPowerValueSlot, kEquipmentPowerThreshold + 100};
    check(store::write_family5(misleading), "seed misleading Power override");

    const auto transition = equipment_power_contract();
    state::PendingQuestTransition pending{};
    g_equipmentPower = kEquipmentPowerThreshold - 1;
    check(!state::prepare_quest_transition(kSource, transition, pending),
          "Family-5 override completed low equipment Power");

    misleading.values[0].value = 0;
    check(store::write_family5(misleading), "lower misleading Power override");
    g_equipmentPower = kEquipmentPowerThreshold;
    check(state::prepare_quest_transition(kSource, transition, pending),
          "equipment threshold did not complete");
    g_equipmentPower = kEquipmentPowerThreshold + 1;
    check(!state::commit_quest_transition(transition, pending),
          "changed equipment Power did not stale the prepared transition");
    check_unchanged();

    g_equipmentPowerAvailable = false;
    check(!state::prepare_quest_transition(kSource, transition, pending),
          "missing equipment evaluation was accepted");
    std::puts("PASS: equipment Power input selection, threshold and stale guard");
}

/** Checks ownership, missing values, stale counters and joined transaction rollback. */
void verify_character_objectives() {
    reset_fixture();
    std::optional<std::int32_t> value;
    check(store::read_character_objective(kCharacter, kValueSlot, value) && !value,
          "new character has no implicit counter");
    check(!store::write_character_objective(kSource, kValueSlot, 1),
          "non-character owner rejected");
    check(!store::write_character_objective(kCharacter, state::kUnlockValueSlotLimit, 1),
          "out-of-range counter slot rejected");
    check(!store::write_character_objective(kCharacter, kValueSlot, -1),
          "negative credit rejected");

    auto counted = contract();
    counted.objectives[0].input = items::QuestPredicate::Input::characterCounter;
    state::PendingQuestTransition pending{};
    check(!state::prepare_quest_transition(kSource, counted, pending),
          "global override cannot supply character-earned credit");
    check(store::write_character_objective(kOtherCharacter, kValueSlot, kMinimumValue),
          "seed other character counter");
    check(!state::prepare_quest_transition(kSource, counted, pending),
          "other character cannot supply credit");
    check(store::write_character_objective(kCharacter, kValueSlot, 0)
              && store::read_character_objective(kCharacter, kValueSlot, value) && value.has_value()
              && *value == 0,
          "explicit zero is not absent");
    check(!state::prepare_quest_transition(kSource, counted, pending), "zero credit is incomplete");
    check(store::write_character_objective(kCharacter, kValueSlot, kMinimumValue),
          "seed completed first counter");
    check(state::prepare_quest_transition(kSource, counted, pending),
          "own earned counter satisfies objective");
    counted.objectiveCount = 2;
    counted.objectives[1] = {kValueSlot + 1, 2, items::QuestPredicate::Input::characterCounter};
    check(!state::prepare_quest_transition(kSource, counted, pending),
          "missing second earned counter refuses transition");
    check(store::write_character_objective(kCharacter, kValueSlot + 1, 2), "seed second counter");
    check(state::prepare_quest_transition(kSource, counted, pending),
          "both earned counters permit transition");
    check(store::write_character_objective(kCharacter, kValueSlot + 1, 3), "change second input");
    check(!state::commit_quest_transition(counted, pending), "stale second counter rejected");
    check_unchanged();

    {
        store::Transaction outer;
        check(outer.ready() && store::write_character_objective(kCharacter, kValueSlot + 1, 4),
              "stage counter write in outer transaction");
        check(state::prepare_quest_transition(kSource, counted, pending)
                  && state::commit_quest_transition(counted, pending),
              "stage replacement joins outer transaction");
        // The caller's uncommitted transaction models a failed publication.
    }
    check_unchanged();
    check(store::read_character_objective(kCharacter, kValueSlot + 1, value) && value == 3,
          "outer rollback includes earned credit");
    check(state::prepare_quest_transition(kSource, counted, pending)
              && state::commit_quest_transition(counted, pending),
          "commit with character counters");
    check(store::read_character_objective(kCharacter, kValueSlot, value) && value == kMinimumValue,
          "inventory rewrite preserves earned progress");

    state::AccountState account{};
    check(store::read_account(account), "read before roster reorder");
    std::swap(account.characters[0], account.characters[1]);
    check(store::write_account(account), "reorder roster");
    check(store::read_character_objective(kCharacter, kValueSlot + 1, value) && value == 3,
          "progress follows stable identity rather than roster slot");
    account.characters[1].soid = kCharacter + 100;
    check(store::write_account(account), "replace character identity");
    check(!store::read_character_objective(kCharacter, kValueSlot, value),
          "removed owner rejected");
    check(store::read_character_objective(kCharacter + 100, kValueSlot, value) && !value,
          "replacement character does not inherit progress");
    check(store::read_character_objective(kOtherCharacter, kValueSlot, value)
              && value == kMinimumValue,
          "unrelated character progress survives removal");
    std::puts("PASS: character counter isolation, explicit zero, stale inputs and joined rollback");
}

/**
 * Exercises restart and migration against a new disposable database from the runner.
 * @param path Unique scratch database supplied by the runner.
 * @param schema Current schema resource.
 * @param settingsSchema Account settings schema resource.
 * @param settingsDefaults Account settings defaults resource.
 */
void verify_progress_restart(const char* path,
                             const std::string& schema,
                             const std::string& settingsSchema,
                             const std::string& settingsDefaults) {
    std::FILE* existing = nullptr;
    const int opened = fopen_s(&existing, path, "rb");
    if (existing != nullptr) {
        std::fclose(existing);
    }
    check(opened != 0, "scratch database must not already exist");
    store::shutdown();
    const auto open = [&] {
        return store::open(path,
                           schema,
                           "INSERT INTO account VALUES (1,1001,0);",
                           settingsSchema,
                           settingsDefaults);
    };
    check(open(), "open new scratch database");
    reset_fixture();
    check(store::write_character_objective(kCharacter, kValueSlot, 0), "persist explicit zero");
    check(store::write_character_objective(kOtherCharacter, kValueSlot, kMinimumValue),
          "persist other character progress");
    store::shutdown();
    check(open(), "reopen current schema");
    std::optional<std::int32_t> value;
    check(store::read_character_objective(kCharacter, kValueSlot, value) && value == 0,
          "zero survives restart without character selection");
    check(store::read_character_objective(kOtherCharacter, kValueSlot, value)
              && value == kMinimumValue,
          "earned progress survives restart");
    check(store::execute("DROP TABLE character_objective_values; PRAGMA user_version=2;"),
          "construct version-two fixture");
    store::shutdown();
    check(open(), "migrate version-two fixture");
    check(store::read_character_objective(kCharacter, kValueSlot, value) && !value,
          "migration does not invent earned progress");
    state::AccountState account{};
    check(store::read_account(account) && account.characterCount == 2
              && account.characters[0].inventory.values[0].instanceSoid == kSource,
          "migration preserves existing inventory and characters");
    {
        store::Statement version("PRAGMA user_version");
        int current = 0;
        check(version.step() == SQLITE_ROW && version.column(0, current) && current == 3,
              "migration records schema three");
    }
    check(store::execute("DROP TABLE character_objective_values;"
                         "DROP TABLE account_preferences; DROP TABLE account_controls;"
                         "DROP TABLE account_audio; DROP TABLE account_display;"
                         "DROP TABLE account_interface; DROP TABLE account_social;"
                         "DROP TABLE account_key_bindings;"
                         "ALTER TABLE items DROP COLUMN seen;"
                         "ALTER TABLE profile_items DROP COLUMN seen; PRAGMA user_version=1;"),
          "construct version-one fixture");
    store::shutdown();
    check(open() && store::read_account(account) && account.characterCount == 2
              && account.characters[0].inventory.values[0].instanceSoid == kSource
              && store::read_character_objective(kCharacter, kValueSlot, value) && !value,
          "version-one migration reaches counter schema without losing inventory");
    // A conflicting table must fail migration without advancing the version.
    check(store::execute("PRAGMA user_version=2"), "construct conflicting migration fixture");
    store::shutdown();
    check(!open(), "conflicting migration refused");
    sqlite3* inspect = nullptr;
    check(sqlite3_open_v2(path, &inspect, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
          "inspect refused migration read-only");
    sqlite3_stmt* version = nullptr;
    check(sqlite3_prepare_v2(inspect, "PRAGMA user_version", -1, &version, nullptr) == SQLITE_OK
              && sqlite3_step(version) == SQLITE_ROW && sqlite3_column_int(version, 0) == 2,
          "failed migration leaves version unchanged");
    sqlite3_finalize(version);
    sqlite3_close(inspect);
    std::puts("PASS: disk reopen, version-one/two migrations, preservation and failed migration");
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

/** Test transition lookup mirrors the one retained source row. */
bool find_quest_transition(std::uint16_t sourceItemIndex,
                           items::QuestTransition& transition) noexcept {
    transition = {};
    if (sourceItemIndex != 0) {
        return false;
    }
    transition = contract();
    return true;
}

/** Native detail resolution is deliberately outside this State-only executable. */
bool find_configured_item_detail(std::uint16_t, items::details::Definition&) noexcept {
    return false;
}

} // namespace sunrise::state::build_data

namespace sunrise::state::build_data::reward_sites {

/** Test catalog contains one build-bound Reward Site while the availability gate is set. */
bool find(std::uint16_t siteIndex, Definition& definition) noexcept {
    definition = {};
    if (!g_rewardSiteAvailable || siteIndex != kRewardSiteIndex) {
        return false;
    }
    definition.siteIndex = siteIndex;
    definition.itemProgressionCount = 1;
    definition.characterObjectTransitionCount = 1;
    definition.provenance = Provenance::reconstructed;
    return true;
}

/** Supplies the site's one checked item replacement. */
bool item_progressions(const Definition& definition,
                       std::span<ItemProgression> output,
                       std::size_t& count) noexcept {
    count = 0;
    if (!g_rewardItemIdentitiesValid || definition.siteIndex != kRewardSiteIndex
        || output.empty()) {
        return false;
    }
    output.front() = {kSourceHash, kSuccessorHash, 0, 1};
    count = 1;
    return true;
}

/** Supplies the site's one selected-character compare-and-set. */
bool character_object_transitions(const Definition& definition,
                                  std::span<CharacterObjectTransition> output,
                                  std::size_t& count) noexcept {
    count = 0;
    if (definition.siteIndex != kRewardSiteIndex || output.empty()) {
        return false;
    }
    output.front() = {kCurrentValue, kNextValue, kQuestRow};
    count = 1;
    return true;
}

} // namespace sunrise::state::build_data::reward_sites

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

namespace sunrise::state::equipment::light::resolution {

/** Controlled substitute proves which input the quest runtime requests. */
bool resolve(const AccountState& account,
             std::size_t selectedCharacterIndex,
             Evaluation& output) noexcept {
    output = {};
    if (!g_equipmentPowerAvailable || selectedCharacterIndex >= account.characterCount
        || !account.characters[selectedCharacterIndex].selected) {
        return false;
    }
    output.average = g_equipmentPower;
    return true;
}

} // namespace sunrise::state::equipment::light::resolution

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
    verify_event_preparation();
    verify_equipment_power_input();
    verify_character_objectives();
    verify_objective_publication();
    verify_quest_transition_catalog();
    verify_quest_transition_reader(argc > 2 && std::string_view(argv[2]) != "-" ? argv[2]
                                                                                : nullptr);
    if (argc > 3) {
        verify_progress_restart(argv[3], schema, settingsSchema, settingsDefaults);
    }
    store::shutdown();
    return 0;
}
