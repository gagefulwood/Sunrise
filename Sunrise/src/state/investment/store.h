#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "../account/account_state.h"
#include "../entitlements/definition.h"
#include "../unlocks/definition.h"
#include "investment.h"

namespace sunrise::state::investment::store {

/** Bank identifiers are part of schema version 1. */
enum class Bank : int {
    accountFlags,
    profileFlags,
    characterFlags,
    objectiveValues,
    characterObjectFlags,
    characterObjectValues,
    accountProgressions,
    characterProgressions
};

[[nodiscard]] bool initialize(void* module) noexcept;
[[nodiscard]] bool validate() noexcept;
[[nodiscard]] bool open(std::string_view path,
                        std::string_view schema,
                        std::string_view defaults,
                        std::string_view settingsSchema,
                        std::string_view settingsDefaults) noexcept;
void shutdown() noexcept;
[[nodiscard]] bool read_account(AccountState& output) noexcept;
[[nodiscard]] AccountState account() noexcept;
[[nodiscard]] bool write_account(const AccountState& value) noexcept;
[[nodiscard]] bool read_settings(account::settings::AccountSettings& output) noexcept;
[[nodiscard]] bool write_settings(const account::settings::AccountSettings& value) noexcept;
void set_sign_in_time(std::uint64_t seconds) noexcept;
[[nodiscard]] bool read_family5(Family5State& output) noexcept;
[[nodiscard]] bool write_family5(const Family5State& value) noexcept;
/** Projection is for publication only; never save it through write_family5(). */
[[nodiscard]] bool project_character_objectives(Family5State& output) noexcept;
/**
 * Reads one character-owned counter without substituting an account override.
 * @param characterSoid Stable identity of an existing character.
 * @param slot Authored objective value slot.
 * @param value Empty for an absent counter or a failed read; zero is a saved value.
 * @return False for an invalid owner, slot or failed database read.
 */
[[nodiscard]] bool read_character_objective(std::uint64_t characterSoid,
                                            std::uint16_t slot,
                                            std::optional<std::int32_t>& value) noexcept;
/**
 * The caller validates earned credit and commits it with the event's other changes.
 * @param characterSoid Stable identity of an existing character.
 * @param slot Authored objective value slot.
 * @param value Nonnegative absolute counter value, including an explicit zero.
 * @return False for an invalid owner, slot, value or failed write.
 */
[[nodiscard]] bool write_character_objective(std::uint64_t characterSoid,
                                             std::uint16_t slot,
                                             std::int32_t value) noexcept;
[[nodiscard]] bool read_unlocks(unlocks::Table& output, int characterSlot = -1) noexcept;
[[nodiscard]] bool write_unlocks(const unlocks::Table& value, int characterSlot = -1) noexcept;
[[nodiscard]] bool read_unlock(Bank bank, std::uint16_t slot, std::int32_t& value) noexcept;
[[nodiscard]] bool write_unlock(Bank bank, std::uint16_t slot, std::int32_t value) noexcept;
[[nodiscard]] bool read_entitlements(entitlements::Table& output) noexcept;
[[nodiscard]] bool bootstrap_completed(std::string_view name) noexcept;
[[nodiscard]] bool complete_bootstrap(std::string_view name) noexcept;

/** An earned reward stays in the database until its inventory grant commits. */
struct PendingReward {
    std::uint64_t id{};
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
    std::uint8_t kind{};
};
[[nodiscard]] bool
enqueue_reward(std::uint32_t definitionHash, std::int32_t quantity, std::uint8_t kind) noexcept;
[[nodiscard]] bool next_reward(PendingReward& output) noexcept;
[[nodiscard]] bool complete_reward(std::uint64_t id) noexcept;

} // namespace sunrise::state::investment::store
