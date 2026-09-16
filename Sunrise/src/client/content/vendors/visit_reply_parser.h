#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/build_data/vendors/definition.h"

namespace sunrise::client::content::vendors {

/** Unlock expression flag slots are signed 16-bit destinations in the installed map. */
inline constexpr std::size_t kVisitFlagSlotCapacity = 32'768;

using AccountFlagRows = std::array<std::uint16_t, kVisitFlagSlotCapacity>;

/**
 * Builds the unique account flag slot-to-bank-row map used by visit replies.
 * @param blob Installed unlock flag map bytes.
 * @param output Receives a row per uniquely mapped slot and the unavailable row otherwise.
 * @return False when the account map is malformed or wider than the saved account bank.
 */
[[nodiscard]] bool read_account_flag_rows(std::span<const std::byte> blob,
                                          AccountFlagRows& output) noexcept;

/**
 * Extracts exact two-flag Complete replies from one investment/client vendor pair.
 * @param investment Investment vendor definition bytes.
 * @param companion Client presentation vendor definition bytes for the same index row.
 * @param accountFlagRows Unique account flag slot mappings.
 * @param definition Vendor definition receiving a complete bounded reply set.
 * @return False when either definition is malformed or the complete set does not fit.
 */
[[nodiscard]] bool
parse_visit_replies(std::span<const std::byte> investment,
                    std::span<const std::byte> companion,
                    std::span<const std::uint16_t, kVisitFlagSlotCapacity> accountFlagRows,
                    state::build_data::vendors::Definition& definition) noexcept;

} // namespace sunrise::client::content::vendors
