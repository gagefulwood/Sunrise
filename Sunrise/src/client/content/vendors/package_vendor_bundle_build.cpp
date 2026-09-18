#include <array>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/definition_index_table.h"
#include "../../../middleware/content/packages/tables/vendor_bundle_reader.h"
#include "../../../state/build_data/items/item_catalog.h"
#include "../../../state/build_data/runtime.h"
#include "../../../state/build_data/vendors/vendor_catalog.h"
#include "vendor_build.h"

namespace sunrise::client::content::vendors {
namespace {
namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;
namespace domain = state::build_data::vendors;
namespace bundles = domain::bundles;

/** Installed investment tables: item identities, sack rewards, flag maps and expression pools. */
constexpr std::uint32_t kItemTableTag = 0x81327CCBU, kRewardTableTag = 0x81319335U;
constexpr std::uint32_t kFlagMapTag = 0x81319322U, kPoolTableTag = 0x81319324U;

/**
 * Find the unique installed sale of one supported wrapper; no vendor or sale ordinal is fixed.
 * @param itemIndex Wrapper's installed item-table index.
 * @param vendors Published vendor definitions.
 * @param vendor Receives the unique owning vendor.
 * @param saleIndex Receives the sale ordinal.
 * @return False for missing, duplicate or priced offers.
 */
bool find_sale(std::uint16_t itemIndex,
               std::span<const domain::Definition> vendors,
               domain::Definition& vendor,
               std::uint16_t& saleIndex) noexcept {
    bool found = false;
    for (const auto& candidate : vendors) {
        for (std::uint16_t index = 0; index < candidate.saleCount; ++index) {
            domain::SaleRow sale{};
            if (!domain::sale_row(candidate, index, sale)) {
                return false;
            }
            if (sale.itemIndex != itemIndex) {
                continue;
            }
            if (found || sale.costQuantity != 0) {
                return false;
            }
            found = true;
            vendor = candidate;
            saleIndex = index;
        }
    }
    return found;
}
/**
 * Rebuild process-local bundle plans even when the base vendor catalog came from disk cache.
 * @param source Installed package directory and borrowed block keys.
 * @param scratch Lock-owned package scratch storage.
 * @return True after publishing the supported subset; individual failures remain unclaimable.
 */
bool read_bundles(const reader::Source& source, reader::Scratch& scratch) noexcept {
    std::vector<std::byte> itemTable, rewards, flagMap, pools, itemBlob, vendorBlob;
    tables::Array items{};
    std::array<domain::Definition, domain::kDefinitionCapacity> vendors{};
    std::size_t vendorCount{};
    if (!reader::read_tag(source, scratch, kItemTableTag, itemTable)
        || !tables::find_array_at(itemTable, tables::kTableArrayDescriptor, items)
        || items.elementClass != tables::kItemIndexTableClass
        || items.count > state::build_data::items::kDefinitionCapacity
        || !reader::read_tag(source, scratch, kRewardTableTag, rewards)
        || !reader::read_tag(source, scratch, kFlagMapTag, flagMap)
        || !reader::read_tag(source, scratch, kPoolTableTag, pools)
        || !domain::snapshot_definitions(vendors, vendorCount)) {
        return false;
    }
    std::array<bundles::Definition, bundles::kClaimEffects.size()> definitions{};
    std::size_t definitionCount = 0;
    for (std::size_t effectIndex = 0; effectIndex < bundles::kClaimEffects.size(); ++effectIndex) {
        const auto& effect = bundles::kClaimEffects[effectIndex];
        tables::IndexRow wrapper{};
        std::uint16_t itemIndex{};
        std::size_t matches = 0;
        for (std::size_t index = 0; index < items.count; ++index) {
            tables::IndexRow row{};
            if (!tables::index_row(itemTable, items, index, row)) {
                return false;
            }
            if (row.definitionHash != effect.itemHash) {
                continue;
            }
            ++matches;
            wrapper = row;
            itemIndex = static_cast<std::uint16_t>(index);
        }
        domain::Definition vendor{};
        std::uint16_t saleIndex{};
        std::uint32_t itemClass{}, vendorClass{};
        bundles::Definition definition{};
        if (matches != 1
            || !find_sale(itemIndex, std::span(vendors).first(vendorCount), vendor, saleIndex)
            || !reader::read_tag(source, scratch, wrapper.targetTag, itemBlob, itemClass)
            || itemClass != tables::kItemDefinitionClass
            || !reader::read_tag(source, scratch, vendor.definitionTag, vendorBlob, vendorClass)
            || vendorClass != domain::kDefinitionClass
            || !tables::read_vendor_bundle({itemBlob,
                                            rewards,
                                            vendorBlob,
                                            flagMap,
                                            pools,
                                            static_cast<std::size_t>(items.count)},
                                           effect,
                                           saleIndex,
                                           definition)) {
            continue;
        }
        definition.vendorIndex = vendor.index;
        definitions[definitionCount++] = definition;
    }
    return bundles::replace(std::span(definitions).first(definitionCount));
}
} // namespace

/**
 * Extract once per base catalog; unsupported offers do not disable their supported siblings.
 * @param source Installed package directory and borrowed block keys.
 * @param scratch Lock-owned package storage.
 * @return True when at least one supported bundle definition is available.
 */
bool build_bundles(const reader::Source& source, reader::Scratch& scratch) noexcept {
    if (bundles::settled()) {
        return bundles::ready();
    }
    if (!state::build_data::vendor_catalog_ready()) {
        return false;
    }
    if (read_bundles(source, scratch)) {
        return bundles::ready();
    }
    bundles::unavailable();
    core::log::write(core::log::Channel::state,
                     core::log::Level::warn,
                     "ev=build_data stage=vendor_bundles result=unsupported");
    return false;
}
} // namespace sunrise::client::content::vendors
