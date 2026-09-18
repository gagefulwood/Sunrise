#pragma once

#include <cstddef>

#include "../../../account/account_state.h"
#include "../../../build_data/vendors/vendor_expression.h"
#include "../definition.h"

namespace sunrise::state::equipment::light::resolution {

/**
 * Evaluates cached equip predicates for an owned item and its actual selected plugs.
 * @param item Owned item with either native-default or authored socket selections.
 * @param inputs Fully resolved server flags and values for the intended recipient.
 * @param satisfied Receives the AND result; cleared on any refusal.
 * @return False for missing metadata, unknown state or invalid socket selections.
 * @note This checks expression predicates, not progression-level requirements or reward policy.
 */
[[nodiscard]] bool equip_predicates(const account::inventory::Item& item,
                                    const build_data::vendors::Inputs& inputs,
                                    bool& satisfied) noexcept;

/**
 * Finds authored equipment in the installed item and detail maps, then computes light from the
 * authored item levels.
 * @param account Already-checked account configuration, read under the lock.
 * @param selectedCharacterIndex Selected used character row.
 * @param output Receives a complete selected-character evaluation only on success.
 * @return True when every configured item has one consistent native slot and exact score.
 */
[[nodiscard]] bool resolve(const AccountState& account,
                           std::size_t selectedCharacterIndex,
                           Evaluation& output) noexcept;

/**
 * Computes the equipment light one character displays, selected or not.
 * @param account Already-checked account configuration, read under the lock.
 * @param characterIndex Used character row.
 * @param light Receives the weighted integer average plus the seasonal artifact Power bonus.
 * @return True when every item on that character is found.
 */
[[nodiscard]] bool character_light(const AccountState& account,
                                   std::size_t characterIndex,
                                   std::int32_t& light) noexcept;

} // namespace sunrise::state::equipment::light::resolution
