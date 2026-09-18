#pragma once

#include "../../../../state/build_data/vendors/bundle_catalog.h"

namespace sunrise::middleware::content::packages::tables {

/** Native blobs remain caller-owned for the duration of one bundle read. */
struct VendorBundleSource {
    std::span<const std::byte> item;
    std::span<const std::byte> rewards;
    std::span<const std::byte> vendor;
    std::span<const std::byte> flagMap;
    std::span<const std::byte> pools;
    std::size_t itemCount;
};

/**
 * Resolve a supported fixed armour sack and its purchase gates from installed content.
 * @param source Native item, reward, vendor, flag-map and expression-pool blobs.
 * @param effect Explicit claim-on-success policy; never inferred from an arbitrary NOT flag.
 * @param saleIndex Sale ordinal within the vendor blob.
 * @param output Receives extracted fields only on success; caller supplies vendorIndex.
 * @return False for unsupported payouts, expressions, mappings or malformed content.
 */
[[nodiscard]] bool
read_vendor_bundle(const VendorBundleSource& source,
                   const state::build_data::vendors::bundles::ClaimEffect& effect,
                   std::uint16_t saleIndex,
                   state::build_data::vendors::bundles::Definition& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
