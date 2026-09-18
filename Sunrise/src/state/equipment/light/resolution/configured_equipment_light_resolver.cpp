#include "configured_equipment_light_resolver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "../../../build_data/runtime.h"
#include "../../../runtime/runtime.h"
#include "../calculation/equipment_light_calculation.h"

namespace sunrise::state::equipment::light::resolution {
namespace {

namespace authored = account::inventory;
namespace build_details = build_data::items::details;
namespace build_items = build_data::items;

/** At most 2 non-selected rows remain in a 3-character account. */
constexpr std::size_t kOtherCharacterCapacity = kCharacterCapacity - 1U;

/** Native equipment-slot identity learned for every used authored semantic slot. */
using SemanticSlotMap = std::array<std::optional<std::uint8_t>, authored::kEquipmentSlotCount>;
/** Authored semantic-slot owner learned for every used native equipment slot. */
using NativeSlotMap = std::array<std::optional<std::size_t>, build_details::kEquipmentSlotCount>;

/**
 * Limits the used character count to the fixed row storage.
 * @param account Account configuration, read under the lock.
 * @return Rows that can be indexed without leaving the fixed array.
 */
[[nodiscard]] std::size_t occupied_rows(const AccountState& account) noexcept {
    return (std::min)(account.characterCount, account.characters.size());
}

/**
 * Finds one authored item's native slot and computes its score.
 * @param item Already-checked authored equipment item.
 * @param nativeSlot Receives the installed native equipment slot.
 * @param itemScore Receives the installed definition index and computed light score.
 * @return True when the dense item and its configured detail agree on one native slot.
 */
[[nodiscard]] bool
resolve_item(const authored::Item& item, std::size_t& nativeSlot, ItemScore& itemScore) noexcept {
    build_items::Definition definition{};
    build_details::Definition detail{};
    std::uint8_t resolvedSlot = 0;
    if (!build_data::find_item_definition_hash(item.definitionHash, definition)
        || !build_data::find_configured_item_detail(definition.definitionIndex, detail)
        || detail.definitionIndex != definition.definitionIndex
        || !authored::resolve_native_equipment_slot(
            item.definitionHash, detail.equipmentSlot, resolvedSlot)
        || static_cast<std::size_t>(resolvedSlot) >= build_details::kEquipmentSlotCount) {
        return false;
    }
    nativeSlot = static_cast<std::size_t>(resolvedSlot);
    // The "Emotes" collection item's real content contributes no light either way.
    std::int32_t power = 0;
    if (detail.equipmentSlot.has_value()
        && !item_power(item.level, power, item.levelFraction, detail.levelCap)) {
        return false;
    }
    itemScore = ItemScore{definition.definitionIndex, power};
    return true;
}

/**
 * Builds one character's native slot maxima while learning semantic-slot consistency.
 * @param character Already-checked authored character.
 * @param semanticSlots Cross-character semantic-to-native map, learned in place.
 * @param nativeSlots Cross-character native-to-semantic owner, learned in place.
 * @param output Receives one complete native slot array only on success.
 * @return True when every item is found and no semantic or native slot collides.
 */
[[nodiscard]] bool resolve_character(const CharacterState& character,
                                     SemanticSlotMap& semanticSlots,
                                     NativeSlotMap& nativeSlots,
                                     SlotScores& output) noexcept {
    SlotScores staged{};
    for (std::size_t semanticSlot = 0; semanticSlot < character.equipment.slots.size();
         ++semanticSlot) {
        const std::optional<authored::Item>& item = character.equipment.slots[semanticSlot];
        if (!item.has_value()) {
            continue;
        }

        std::size_t nativeSlot = 0;
        ItemScore score{};
        if (!resolve_item(*item, nativeSlot, score) || staged[nativeSlot].has_value()) {
            return false;
        }
        if (semanticSlots[semanticSlot].has_value() && *semanticSlots[semanticSlot] != nativeSlot) {
            return false;
        }
        if (nativeSlots[nativeSlot].has_value() && *nativeSlots[nativeSlot] != semanticSlot) {
            return false;
        }
        // Both directions must commit together so later characters cannot alias a native slot.
        semanticSlots[semanticSlot] = static_cast<std::uint8_t>(nativeSlot);
        nativeSlots[nativeSlot] = semanticSlot;
        staged[nativeSlot] = score;
    }
    output = staged;
    return true;
}

} // namespace

/**
 * Checks the base and selected plugs without substituting a plug pool or native defaults.
 * @param item Owned item whose sockets are read without mutation.
 * @param inputs Resolved server inputs; unreadable values must fail.
 * @param satisfied Receives the complete predicate result, or false on refusal.
 * @return True only when all required metadata and state could be evaluated.
 */
bool equip_predicates(const authored::Item& item,
                      const build_data::vendors::Inputs& inputs,
                      bool& satisfied) noexcept {
    satisfied = false;
    build_items::Definition definition{};
    build_details::Definition detail{};
    if (!authored::valid(item.sockets)
        || !build_data::find_item_definition_hash(item.definitionHash, definition)
        || definition.definitionHash != item.definitionHash
        || !build_data::find_configured_item_detail(definition.definitionIndex, detail)
        || detail.definitionIndex != definition.definitionIndex
        || detail.definitionHash != item.definitionHash
        || detail.ordinarySocketCount > detail.initialPlugIndices.size()) {
        return false;
    }
    const bool selected = item.sockets.policy == authored::SocketPolicy::authored;
    if ((selected && item.sockets.plugCount != detail.ordinarySocketCount)
        || (detail.ordinarySocketState != build_details::OrdinarySocketState::present
            && (detail.ordinarySocketState != build_details::OrdinarySocketState::absent
                || detail.ordinarySocketCount != 0))) {
        return false;
    }
    bool complete = false;
    if (!build_data::vendors::evaluate(detail.equipRequirements, inputs, complete)) {
        return false;
    }
    for (std::size_t lane = 0; lane < detail.ordinarySocketCount; ++lane) {
        build_items::Definition plug{};
        if (selected) {
            const auto& hash = item.sockets.plugs[lane];
            if (!hash.has_value()) {
                continue;
            }
            if (!build_data::find_item_definition_hash(*hash, plug)
                || plug.definitionHash != *hash) {
                return false;
            }
        } else {
            const auto index = detail.initialPlugIndices[lane];
            if (index == build_details::kUnavailableItemIndex) {
                continue;
            }
            if (!build_data::find_item_definition_index(index, plug)
                || plug.definitionIndex != index) {
                return false;
            }
        }
        build_details::Definition plugDetail{};
        bool allowed = false;
        if (!build_data::find_configured_item_detail(plug.definitionIndex, plugDetail)
            || plugDetail.definitionIndex != plug.definitionIndex
            || plugDetail.definitionHash != plug.definitionHash
            || !build_data::vendors::evaluate(plugDetail.plugEquipRequirements, inputs, allowed)) {
            return false;
        }
        complete = complete && allowed;
    }
    satisfied = complete;
    return true;
}

/**
 * Finds authored equipment in the installed item and detail maps, then computes light from the
 * authored item levels.
 */
bool resolve(const AccountState& account,
             std::size_t selectedCharacterIndex,
             Evaluation& output) noexcept {
    const std::size_t count = occupied_rows(account);
    if (selectedCharacterIndex >= count || !account.characters[selectedCharacterIndex].selected) {
        return false;
    }

    std::array<SlotScores, kCharacterCapacity> characterScores{};
    SemanticSlotMap semanticSlots{};
    NativeSlotMap nativeSlots{};
    for (std::size_t characterIndex = 0; characterIndex < count; ++characterIndex) {
        if (!resolve_character(account.characters[characterIndex],
                               semanticSlots,
                               nativeSlots,
                               characterScores[characterIndex])) {
            return false;
        }
    }

    std::array<SlotScores, kOtherCharacterCapacity> otherCharacters{};
    std::size_t otherCharacterCount = 0;
    for (std::size_t characterIndex = 0; characterIndex < count; ++characterIndex) {
        if (characterIndex != selectedCharacterIndex
            && otherCharacterCount < otherCharacters.size()) {
            otherCharacters[otherCharacterCount++] = characterScores[characterIndex];
        }
    }

    // The profile summary array reports the same equipped items as the character array. The
    // generator fills both from one loadout.
    const SlotScores& profileSlotMaxima = characterScores[selectedCharacterIndex];
    Evaluation staged{};
    if (!calculation::evaluate(characterScores[selectedCharacterIndex],
                               profileSlotMaxima,
                               std::span(otherCharacters).first(otherCharacterCount),
                               staged)) {
        return false;
    }
    output = staged;
    return true;
}

/** Computes the equipment light one character displays, artifact bonus included. */
bool character_light(const AccountState& account,
                     std::size_t characterIndex,
                     std::int32_t& light) noexcept {
    light = 0;
    if (characterIndex >= occupied_rows(account)) {
        return false;
    }
    SlotScores scores{};
    SemanticSlotMap semanticSlots{};
    NativeSlotMap nativeSlots{};
    if (!resolve_character(
            account.characters[characterIndex], semanticSlots, nativeSlots, scores)) {
        return false;
    }
    Evaluation evaluation{};
    if (!calculation::evaluate(scores, SlotScores{}, std::span<const SlotScores>{}, evaluation)) {
        return false;
    }
    light = evaluation.average + state::artifact_power_bonus();
    return true;
}

} // namespace sunrise::state::equipment::light::resolution
