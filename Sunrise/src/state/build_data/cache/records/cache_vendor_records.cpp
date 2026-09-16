#include "codec.h"

namespace sunrise::state::build_data::cache::records {

/** Encodes one vendor index row. */
bool encode(const vendors::IndexEntry& value, VendorIndexRecord& record) noexcept {
    record = {};
    record.definitionHash = value.definitionHash;
    record.definitionTag = value.definitionTag;
    record.index = value.index;
    return true;
}

/** Decodes one vendor index row. */
bool decode(const VendorIndexRecord& record, vendors::IndexEntry& value) noexcept {
    value = {};
    if (record.reserved != 0) {
        return false;
    }
    value = {record.definitionHash, record.definitionTag, record.index};
    return true;
}

/** Encodes one vendor definition and its flat-bank ranges. */
bool encode(const vendors::Definition& value, VendorDefinitionRecord& record) noexcept {
    record = {};
    record.definitionHash = value.definitionHash;
    record.definitionTag = value.definitionTag;
    record.definitionClass = value.definitionClass;
    record.definitionSize = value.definitionSize;
    record.installedRowBase = value.installedRowBase;
    record.installedRowClass = value.installedRowClass;
    record.saleRowBase = value.saleRowBase;
    record.saleRowClass = value.saleRowClass;
    record.thirdRowBase = value.thirdRowBase;
    record.thirdRowClass = value.thirdRowClass;
    record.saleRowOffset = value.saleRowOffset;
    record.installedRowOffset = value.installedRowOffset;
    record.resetIntervalRaw = value.resetIntervalRaw;
    record.resetPhaseRaw = value.resetPhaseRaw;
    record.index = value.index;
    record.installedCount = value.installedCount;
    record.saleCount = value.saleCount;
    record.thirdCount = value.thirdCount;
    for (std::size_t row = 0; row < value.visitReplies.size(); ++row) {
        const vendors::VisitReply& source = value.visitReplies[row];
        VisitReplyRecord& target = record.visitReplies[row];
        target.interactionIndex = source.interactionIndex;
        target.replyIndex = source.replyIndex;
        target.flags = source.flags;
        target.accountFlagRows = source.accountFlagRows;
    }
    record.visitReplyCount = value.visitReplyCount;
    return true;
}

/** Decodes one vendor definition and its flat-bank ranges. */
bool decode(const VendorDefinitionRecord& record, vendors::Definition& value) noexcept {
    value = {};
    // The catalog checks every range against the whole domain. Reject record-level class and count
    // fields here before copying their payloads.
    if (record.definitionClass != vendors::kDefinitionClass
        || record.visitReplyCount > vendors::kVisitReplyCapacity) {
        return false;
    }
    value.definitionHash = record.definitionHash;
    value.definitionTag = record.definitionTag;
    value.definitionClass = record.definitionClass;
    value.definitionSize = record.definitionSize;
    value.installedRowBase = record.installedRowBase;
    value.installedRowClass = record.installedRowClass;
    value.saleRowBase = record.saleRowBase;
    value.saleRowClass = record.saleRowClass;
    value.thirdRowBase = record.thirdRowBase;
    value.thirdRowClass = record.thirdRowClass;
    value.saleRowOffset = record.saleRowOffset;
    value.installedRowOffset = record.installedRowOffset;
    value.resetIntervalRaw = record.resetIntervalRaw;
    value.resetPhaseRaw = record.resetPhaseRaw;
    value.index = record.index;
    value.installedCount = record.installedCount;
    value.saleCount = record.saleCount;
    value.thirdCount = record.thirdCount;
    for (std::size_t row = 0; row < record.visitReplies.size(); ++row) {
        const VisitReplyRecord& source = record.visitReplies[row];
        vendors::VisitReply& target = value.visitReplies[row];
        target.interactionIndex = source.interactionIndex;
        target.replyIndex = source.replyIndex;
        target.flags = source.flags;
        target.accountFlagRows = source.accountFlagRows;
    }
    value.visitReplyCount = record.visitReplyCount;
    return true;
}

/** Encodes one vendor sale row. */
bool encode(const vendors::SaleRow& value, VendorSaleRowRecord& record) noexcept {
    record = {};
    record.itemIndex = value.itemIndex;
    record.secondaryItemIndex = value.secondaryItemIndex;
    record.categoryIndex = value.categoryIndex;
    record.costQuantity = value.costQuantity;
    record.costItemIndex = value.costItemIndex;
    return true;
}

/** Decodes one vendor sale row. */
bool decode(const VendorSaleRowRecord& record, vendors::SaleRow& value) noexcept {
    value = {};
    if (record.reserved != decltype(record.reserved){}) {
        return false;
    }
    value.itemIndex = record.itemIndex;
    value.secondaryItemIndex = record.secondaryItemIndex;
    value.categoryIndex = record.categoryIndex;
    value.costQuantity = record.costQuantity;
    value.costItemIndex = record.costItemIndex;
    return true;
}

/** Encodes one vendor category row. */
bool encode(const vendors::InstalledRow& value, VendorInstalledRowRecord& record) noexcept {
    record = {value.definitionHash};
    return true;
}

/** Decodes one vendor category row. */
bool decode(const VendorInstalledRowRecord& record, vendors::InstalledRow& value) noexcept {
    value = {record.definitionHash};
    return true;
}

} // namespace sunrise::state::build_data::cache::records
