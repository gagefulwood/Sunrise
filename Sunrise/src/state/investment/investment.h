#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state {

/** The native family-5 lists hold 100 rows each. The 7-bit wire count is not the limit. */
inline constexpr std::size_t kUnlockOverrideCapacity = 100;
/** Authored value slots fit the nonnegative half of a signed 16-bit mapping. */
inline constexpr std::uint16_t kUnlockValueSlotLimit = 0x8000U;
/** Family-5 value overrides index a native buffer with 15500 entries. */
inline constexpr std::uint16_t kFamily5ValueSlotLimit = 15500;

/** One logical unlock-flag value stored by slot. */
struct UnlockFlagOverride {
    std::uint16_t slot{};
    std::uint8_t value{};
};

/** One logical signed unlock value stored by slot. */
struct UnlockValueOverride {
    std::uint16_t slot{};
    std::int32_t value{};
};

/** Global family-5 object and its bounded account overrides. */
struct Family5State {
    std::uint64_t objectSoid{};
    std::array<UnlockFlagOverride, kUnlockOverrideCapacity> flags{};
    std::size_t flagCount{};
    std::array<UnlockValueOverride, kUnlockOverrideCapacity> values{};
    std::size_t valueCount{};
    bool contentGateArm{};
};

/** Account-wide evaluated content state. */
struct InvestmentState {
    Family5State family5;
};

} // namespace sunrise::state
