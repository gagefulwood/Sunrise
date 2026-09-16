#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::vendors {

/** Tag of the installed vendor index blob, which names every vendor definition. */
inline constexpr std::uint32_t kIndexRootTag = 0x8131931DU;
/** Client vendor index paired by row and definition hash with the installed vendor index. */
inline constexpr std::uint32_t kCompanionIndexRootTag = 0x81A2926DU;
/** Installed account flag map named by investment-root slot 111 in this build. */
inline constexpr std::uint32_t kAccountFlagMapTag = 0x81319322U;
/** Package class of the client vendor index blob. */
inline constexpr std::uint32_t kCompanionIndexClass = 0x808058E8U;
/** Element class of one client vendor index row. */
inline constexpr std::uint32_t kCompanionIndexRowClass = 0x808058ECU;
/** Package class of one client vendor companion definition. */
inline constexpr std::uint32_t kCompanionDefinitionClass = 0x808058EEU;
/** Package class of the installed unlock flag map. */
inline constexpr std::uint32_t kAccountFlagMapClass = 0x80807D36U;
/** Element class of the account slot-to-saved-row flag map. */
inline constexpr std::uint32_t kAccountFlagMapRowClass = 0x80807D48U;

/** A vendor definition holds its installed array descriptor here. */
inline constexpr std::size_t kInstalledArrayDescriptor = 32;
/** A vendor definition holds its sale array descriptor here. */
inline constexpr std::size_t kSaleArrayDescriptor = 48;
/** A vendor definition holds its unnamed third array descriptor here. */
inline constexpr std::size_t kThirdArrayDescriptor = 80;
/** Element class of the investment vendor interaction array. */
inline constexpr std::uint32_t kInvestmentInteractionClass = 0x80807857U;
/** A client vendor companion holds one presentation row per interaction here. */
inline constexpr std::size_t kCompanionInteractionArrayDescriptor = 144;
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

/** One client companion interaction row is 104 bytes. */
inline constexpr std::size_t kCompanionInteractionStride = 104;
/** One client reply row is 20 bytes. */
inline constexpr std::size_t kCompanionReplyStride = 20;
/** The 32-bit reply type starts at +8 in a client reply row. */
inline constexpr std::size_t kCompanionReplyTypeOffset = 8;
/** The client reply enum assigns 2 to Complete. */
inline constexpr std::uint32_t kCompleteReplyType = 2;
/** Element class of one client companion interaction row. */
inline constexpr std::uint32_t kCompanionInteractionClass = 0x80805922U;
/** Element class of one client companion reply row. */
inline constexpr std::uint32_t kCompanionReplyClass = 0x80805924U;
/** An investment interaction's visibility expression starts at +8. */
inline constexpr std::size_t kInteractionVisibilityDescriptor = 8;
/** An investment interaction's retirement expression starts at +24. */
inline constexpr std::size_t kInteractionRetirementDescriptor = 24;
/** An investment interaction's reply array starts at +40. */
inline constexpr std::size_t kInteractionReplyDescriptor = 40;
/** One investment reply row is 24 bytes. */
inline constexpr std::size_t kInvestmentReplyStride = 24;
/** Element class of one investment reply row. */
inline constexpr std::uint32_t kInvestmentReplyClass = 0x8080785BU;
/** Visibility instructions contain a 32-bit opcode and a 32-bit operand. */
inline constexpr std::size_t kVisibilityInstructionStride = 8;
/** Element class of a visibility-expression instruction. */
inline constexpr std::uint32_t kVisibilityInstructionClass = 0x80807D31U;
/** Exact FLAG, FLAG, AND program accepted for visit reply metadata. */
inline constexpr std::size_t kVisibilityInstructionCount = 3;
/** Native FLAG instructions read one logical unlock flag. */
inline constexpr std::uint32_t kReadFlagOpcode = 1;
/** Native AND instructions require both preceding flags to be active. */
inline constexpr std::uint32_t kAndOpcode = 4;
/** Operand-free expression instructions carry all bits set. */
inline constexpr std::uint32_t kUnusedOpcodeOperand = 0xFFFFFFFFU;

} // namespace sunrise::client::content::vendors
