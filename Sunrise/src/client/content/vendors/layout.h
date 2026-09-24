#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::vendors {

/** Tag of the installed vendor index blob, which names every vendor definition. */
inline constexpr std::uint32_t kIndexRootTag = 0x8131931DU;
/** Installed faction table named by the build-86657 investment content. */
inline constexpr std::uint32_t kFactionTableTag = 0x81327CD5U;
/** The faction table's rows begin at this array descriptor. */
inline constexpr std::size_t kFactionArrayDescriptor = 8;
/** One faction row holds its hash and progression index in the first eight bytes. */
inline constexpr std::size_t kFactionRowStride = 24;
/** The installed faction table's row class. */
inline constexpr std::uint32_t kFactionRowClass = 0x808074CEU;
/** Faction definition hash at the start of a faction row. */
inline constexpr std::size_t kFactionHashOffset = 0;
/** Progression definition index following the faction hash. */
inline constexpr std::size_t kFactionProgressionIndexOffset = 4;
/** Vendor definition +18 selects a faction row; out-of-range values name no faction. */
inline constexpr std::size_t kVendorFactionIndexOffset = 18;

/** A vendor definition holds its installed array descriptor here. */
inline constexpr std::size_t kInstalledArrayDescriptor = 32;
/** A vendor definition holds its sale array descriptor here. */
inline constexpr std::size_t kSaleArrayDescriptor = 48;
/** A vendor definition holds its unnamed third array descriptor here. */
inline constexpr std::size_t kThirdArrayDescriptor = 80;
/** Element class of an interaction row in the third vendor array. */
inline constexpr std::uint32_t kInteractionRowClass = 0x80807857U;
/** Interaction row +8 holds its direct availability expression. */
inline constexpr std::size_t kInteractionConditionField = 8;
/** Interaction row +56 names the vendor category it presents. */
inline constexpr std::size_t kInteractionCategoryOffset = 56;
/** Raw reset interval. Its unit, epoch and scope are open, so it is stored unconverted. */
inline constexpr std::size_t kResetIntervalOffset = 20;
/** Raw reset phase, paired with the interval. */
inline constexpr std::size_t kResetPhaseOffset = 24;

/** Sale row price-override array descriptor, which is what the row charges. */
inline constexpr std::size_t kSaleCostArrayDescriptor = 32;
/** Sale row +8 carries admission programs; +120 carries selection programs. */
inline constexpr std::size_t kSaleAdmissionField = 8, kSaleSelectionField = 120;
/** Cost item-definition index inside one price-override row. */
inline constexpr std::size_t kSaleCostItemIndexOffset = 0;
/** Units the price-override row charges. */
inline constexpr std::size_t kSaleCostQuantityOffset = 4;
/** Sale row main item-definition index. */
inline constexpr std::size_t kSaleItemIndexOffset = 70;
/** Sale row vendor category index. */
inline constexpr std::size_t kSaleCategoryIndexOffset = 100;
/** Sale row secondary item-definition index. */
inline constexpr std::size_t kSaleSecondaryItemOffset = 176;
/** A category row names its item by definition hash at this offset. */
inline constexpr std::size_t kInstalledRowHashOffset = 0;

} // namespace sunrise::client::content::vendors
