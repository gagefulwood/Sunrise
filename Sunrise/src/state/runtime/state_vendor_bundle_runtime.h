#pragma once

#include "../unlocks/definition.h"

namespace sunrise::state {

/** A recognized bundle cannot fall through to a grant of its unopened wrapper. */
enum class VendorBundleDisposition : std::uint8_t { notApplicable, refused, prepared };

/** Exact sale and prior account flag retained until the whole bundle commits. */
struct VendorBundleClaim {
    std::uint32_t sourceHash{};
    std::uint16_t vendorIndex{};
    std::uint16_t saleIndex{};
    std::uint8_t beforeClaim{};
};

struct PendingRecordRewardGrant;
[[nodiscard]] VendorBundleDisposition
prepare_vendor_bundle(std::uint16_t vendorIndex,
                      std::uint16_t saleIndex,
                      PendingRecordRewardGrant& mutation) noexcept;

/**
 * Hold the investment lock while checking a bundle against its saved eligibility.
 * @param mutation Prepared rewards and source sale.
 * @param banks Current saved unlocks, not a client eligibility override.
 * @param claimRow Receives the account flag to set only on success.
 * @return False for changed content, eligibility, class, claim state or reward rows.
 */
[[nodiscard]] bool vendor_bundle_claim_row(const PendingRecordRewardGrant& mutation,
                                           const unlocks::Table& banks,
                                           std::uint16_t& claimRow) noexcept;

} // namespace sunrise::state
