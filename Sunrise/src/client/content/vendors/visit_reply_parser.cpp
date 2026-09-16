#include "visit_reply_parser.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "../../../middleware/content/packages/tables/definition_index_table.h"
#include "../../../state/unlocks/definition.h"
#include "layout.h"

namespace sunrise::client::content::vendors {
namespace {

namespace tables = middleware::content::packages::tables;
namespace domain = state::build_data::vendors;

/** Duplicate slot mappings stay unavailable after the map pass ends. */
constexpr std::uint16_t kAmbiguousAccountFlagRow = 0xFFFEU;

/** @param blob Source bytes. @param offset Field offset. @param value Receives the field. */
template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

/**
 * Resolves one optional array and checks its class and byte range.
 * @param blob Blob owning the descriptor.
 * @param descriptor Array descriptor offset.
 * @param stride Row width.
 * @param expectedClass Required class for a nonempty array, or zero to accept its class.
 * @param output Receives the resolved array.
 * @return False when the descriptor, class, or range is invalid.
 */
[[nodiscard]] bool read_array(std::span<const std::byte> blob,
                              std::size_t descriptor,
                              std::size_t stride,
                              std::uint32_t expectedClass,
                              tables::Array& output) noexcept {
    if (!tables::find_optional_array_at(blob, descriptor, output)) {
        return false;
    }
    if (output.count == 0) {
        return true;
    }
    return (expectedClass == 0 || output.elementClass == expectedClass)
           && output.dataOffset <= blob.size()
           && output.count <= (blob.size() - output.dataOffset) / stride;
}

/** @return The unique account row for one flag, or the unavailable row. */
[[nodiscard]] std::uint16_t
account_flag_row(std::span<const std::uint16_t, kVisitFlagSlotCapacity> rows,
                 std::uint32_t flag) noexcept {
    return flag < rows.size() ? rows[flag] : domain::kUnavailableAccountFlagRow;
}

} // namespace

/**
 * Builds the unique account flag slot-to-bank-row map used by visit replies.
 * @param blob Installed unlock flag map bytes.
 * @param output Receives a row per uniquely mapped slot and the unavailable row otherwise.
 * @return False when the account map is malformed or wider than the saved account bank.
 */
bool read_account_flag_rows(std::span<const std::byte> blob, AccountFlagRows& output) noexcept {
    output.fill(domain::kUnavailableAccountFlagRow);
    tables::Array rows{};
    if (!read_array(blob,
                    tables::kAccountFlagMapDescriptor,
                    tables::kUnlockMapRowStride,
                    kAccountFlagMapRowClass,
                    rows)
        || rows.count == 0 || rows.count > state::unlocks::kAccountFlagCapacity
        || rows.count >= kAmbiguousAccountFlagRow) {
        return false;
    }
    for (std::uint64_t row = 0; row < rows.count; ++row) {
        const std::size_t at =
            rows.dataOffset + static_cast<std::size_t>(row) * tables::kUnlockMapRowStride;
        std::int16_t slot = -1;
        std::uint16_t reserved = 0;
        if (!read(blob, at + tables::kUnlockMapDestinationSlotOffset, slot)
            || !read(blob, at + tables::kUnlockMapDestinationSlotOffset + sizeof slot, reserved)
            || reserved != 0) {
            output.fill(domain::kUnavailableAccountFlagRow);
            return false;
        }
        if (slot < 0) {
            continue;
        }
        std::uint16_t& mapped = output[static_cast<std::size_t>(slot)];
        mapped = mapped == domain::kUnavailableAccountFlagRow ? static_cast<std::uint16_t>(row)
                                                              : kAmbiguousAccountFlagRow;
    }
    std::replace(
        output.begin(), output.end(), kAmbiguousAccountFlagRow, domain::kUnavailableAccountFlagRow);
    return true;
}

/**
 * Extracts exact two-flag Complete replies from one investment/client vendor pair.
 * @param investment Investment vendor definition bytes.
 * @param companion Client presentation vendor definition bytes for the same index row.
 * @param accountFlagRows Unique account flag slot mappings.
 * @param definition Vendor definition receiving a complete bounded reply set.
 * @return False when either definition is malformed or the complete set does not fit.
 */
bool parse_visit_replies(std::span<const std::byte> investment,
                         std::span<const std::byte> companion,
                         std::span<const std::uint16_t, kVisitFlagSlotCapacity> accountFlagRows,
                         domain::Definition& definition) noexcept {
    definition.visitReplies = {};
    definition.visitReplyCount = 0;

    tables::Array interactions{};
    tables::Array presentations{};
    if (!read_array(investment,
                    kThirdArrayDescriptor,
                    domain::kThirdRowStride,
                    kInvestmentInteractionClass,
                    interactions)
        || !read_array(companion,
                       kCompanionInteractionArrayDescriptor,
                       kCompanionInteractionStride,
                       kCompanionInteractionClass,
                       presentations)
        || interactions.count != definition.thirdCount
        || interactions.dataOffset != definition.thirdRowBase
        || interactions.elementClass != definition.thirdRowClass
        || presentations.count != interactions.count) {
        return false;
    }

    for (std::uint64_t interaction = 0; interaction < interactions.count; ++interaction) {
        const std::size_t investmentRow =
            interactions.dataOffset
            + static_cast<std::size_t>(interaction) * domain::kThirdRowStride;
        const std::size_t companionRow =
            presentations.dataOffset
            + static_cast<std::size_t>(interaction) * kCompanionInteractionStride;
        tables::Array visibility{};
        tables::Array retirement{};
        tables::Array investmentReplies{};
        tables::Array companionReplies{};
        if (!read_array(investment,
                        investmentRow + kInteractionVisibilityDescriptor,
                        kVisibilityInstructionStride,
                        kVisibilityInstructionClass,
                        visibility)
            || !read_array(investment,
                           investmentRow + kInteractionRetirementDescriptor,
                           kVisibilityInstructionStride,
                           0,
                           retirement)
            || !read_array(investment,
                           investmentRow + kInteractionReplyDescriptor,
                           kInvestmentReplyStride,
                           kInvestmentReplyClass,
                           investmentReplies)
            || !read_array(companion,
                           companionRow,
                           kCompanionReplyStride,
                           kCompanionReplyClass,
                           companionReplies)) {
            definition.visitReplies = {};
            definition.visitReplyCount = 0;
            return false;
        }
        if (visibility.count != kVisibilityInstructionCount || retirement.count != 0
            || investmentReplies.count != 1 || companionReplies.count != 1) {
            continue;
        }

        std::array<std::uint32_t, kVisibilityInstructionCount> opcodes{};
        std::array<std::uint32_t, kVisibilityInstructionCount> operands{};
        bool instructionsRead = true;
        for (std::size_t instruction = 0; instruction < opcodes.size(); ++instruction) {
            const std::size_t at =
                visibility.dataOffset + instruction * kVisibilityInstructionStride;
            instructionsRead =
                instructionsRead && read(investment, at, opcodes[instruction])
                && read(investment, at + sizeof(std::uint32_t), operands[instruction]);
        }
        std::uint32_t replyType = 0;
        if (!instructionsRead
            || !read(
                companion, companionReplies.dataOffset + kCompanionReplyTypeOffset, replyType)) {
            definition.visitReplies = {};
            definition.visitReplyCount = 0;
            return false;
        }
        if (opcodes[0] != kReadFlagOpcode || opcodes[1] != kReadFlagOpcode
            || opcodes[2] != kAndOpcode || operands[2] != kUnusedOpcodeOperand
            || operands[0] > (std::numeric_limits<std::uint16_t>::max)()
            || operands[1] > (std::numeric_limits<std::uint16_t>::max)()
            || operands[0] == operands[1] || replyType != kCompleteReplyType) {
            continue;
        }
        if (definition.visitReplyCount == domain::kVisitReplyCapacity) {
            definition.visitReplies = {};
            definition.visitReplyCount = 0;
            return false;
        }
        domain::VisitReply& reply = definition.visitReplies[definition.visitReplyCount++];
        reply.interactionIndex = static_cast<std::uint16_t>(interaction);
        reply.replyIndex = 0;
        for (std::size_t flag = 0; flag < reply.flags.size(); ++flag) {
            reply.flags[flag] = static_cast<std::uint16_t>(operands[flag]);
            reply.accountFlagRows[flag] = account_flag_row(accountFlagRows, operands[flag]);
        }
    }
    return true;
}

} // namespace sunrise::client::content::vendors
