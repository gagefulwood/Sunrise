#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::vendors {

/** Tag of the installed vendor index blob, which names every vendor definition. */
inline constexpr std::uint32_t kIndexRootTag = 0x8131931DU;

/** Build-86657 investment-root slot holding nested faction reward lists. */
inline constexpr std::size_t kRewardListTableSlot = 88;
/** Native reward-list table row class. */
inline constexpr std::uint32_t kRewardListRowClass = 0x8080748CU;
/** Native reward-list table row size. */
inline constexpr std::size_t kRewardListRowStride = 24;
/** Native reward-list entry class. */
inline constexpr std::uint32_t kRewardEntryClass = 0x8080748EU;
/** Native reward-list entry size. */
inline constexpr std::size_t kRewardEntryStride = 80;
/** Item-definition index in one reward entry. */
inline constexpr std::size_t kRewardEntryItemOffset = 0;
/** Direct child-list selector in one reward entry. */
inline constexpr std::size_t kRewardEntryChildOffset = 8;
/** Separate selector with an unresolved target space; do not treat it as a child list. */
inline constexpr std::size_t kRewardEntryOtherSelectorOffset = 10;
/** Expression array descriptor in one reward entry. */
inline constexpr std::size_t kRewardEntryConditionOffset = 32;
/** All-one item/list selectors do not name an installed row. */
inline constexpr std::uint16_t kNoRewardSelector = 0xFFFFU;
/** Self-relative reward-sack pointer in an item definition. */
inline constexpr std::size_t kItemRewardSackPointer = 0x58;
/** Native marker immediately before a resolved sack block. */
inline constexpr std::uint32_t kRewardSackClass = 0x808077CCU;
/** Native reward-list selector at the start of a sack block. */
inline constexpr std::size_t kRewardSackListOffset = 0;
/** Sack-entry array descriptor in the resolved sack block. */
inline constexpr std::size_t kRewardSackEntriesOffset = 8;
/** Native sack-entry class. */
inline constexpr std::uint32_t kRewardSackEntryClass = 0x808077CFU;
/** Native sack-entry row size. */
inline constexpr std::size_t kRewardSackEntryStride = 12;
/** Hunter armour leaves use FLAG[239] in the installed reward list. */
inline constexpr std::uint32_t kHunterArmourFlag = 239;
/** Titan armour leaves use FLAG[264] in the installed reward list. */
inline constexpr std::uint32_t kTitanArmourFlag = 264;
/** Warlock armour leaves use FLAG[271] in the installed reward list. */
inline constexpr std::uint32_t kWarlockArmourFlag = 271;

/** A vendor definition holds its installed array descriptor here. */
inline constexpr std::size_t kInstalledArrayDescriptor = 32;
/** A vendor definition holds its sale array descriptor here. */
inline constexpr std::size_t kSaleArrayDescriptor = 48;
/** A vendor definition holds its unnamed third array descriptor here. */
inline constexpr std::size_t kThirdArrayDescriptor = 80;
/** Raw reset interval. Its unit, epoch and scope are open, so it is stored unconverted. */
inline constexpr std::size_t kResetIntervalOffset = 20;
/** Raw reset phase, paired with the interval. */
inline constexpr std::size_t kResetPhaseOffset = 24;

/** Sale row price-override array descriptor, which is what the row charges. */
inline constexpr std::size_t kSaleCostArrayDescriptor = 32;
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
