#include "vendor_bundle_reader.h"

#include <algorithm>
#include <limits>

#include "../../../../state/build_data/vendors/definition.h"
#include "../../../../state/unlocks/definition.h"
#include "definition_index_table.h"
#include "internal.h"
#include "item_bundle_reader.h"

namespace sunrise::middleware::content::packages::tables {
namespace {
namespace bundles = state::build_data::vendors::bundles;
/** Pool rows place their nested expression descriptor after the hash and padding. */
constexpr std::size_t kNestedArrayOffset = 8;
/** Vendor sale array and purchase-expression list offsets in the native layout. */
constexpr std::size_t kSalesOffset = 48, kPurchaseOffset = 8;
/** An expression list contains 16-byte descriptors; instructions are opcode/operand pairs. */
constexpr std::size_t kExpressionStride = 16;
constexpr std::uint32_t kExpressionClass = 0x80807D2FU, kInstructionClass = 0x80807D31U;
/** Pool rows hold a hash and an expression descriptor. */
constexpr std::size_t kPoolStride = 24;
constexpr std::uint32_t kPoolClass = 0x80807C4FU;
/** Native opcode 12 evaluates a shared expression-pool row. */
constexpr std::uint32_t kPoolOpcode = 12;
/** At most 128 instruction visits per sale, including pool calls, bounds cyclic content. */
constexpr std::size_t kGateInstructionBudget = 128;
/** Native class predicates address the evaluated flag space, not saved bank rows. */
constexpr std::uint16_t kHunterClassFlag = 239, kTitanClassFlag = 264, kWarlockClassFlag = 271;
/** All five mapping domains must be checked to reject ambiguous flag destinations. */
constexpr auto kMapDescriptors = std::to_array<std::size_t>({8, 24, 40, 56, 72});
constexpr std::uint32_t kMapClass = 0x80807D48U;
/** The saved-map row ends with a reserved 16-bit field. */
constexpr std::size_t kMapReservedOffset = 6;

/**
 * Bound every fixed-width row before any nested field is read.
 * @param blob Blob owning the descriptor and rows.
 * @param field Descriptor offset.
 * @param type Required element class.
 * @param stride Bytes per row.
 * @param rows Receives validated bounds.
 * @return False for absent, wrong-class or truncated arrays.
 */
bool array(std::span<const std::byte> blob,
           std::size_t field,
           std::uint32_t type,
           std::size_t stride,
           Array& rows) noexcept {
    return find_array_at(blob, field, rows) && rows.elementClass == type
           && rows.dataOffset <= blob.size()
           && rows.count <= (blob.size() - rows.dataOffset) / stride;
}

/**
 * Resolve one flag by slot or hash, accepting only a unique account-bank mapping.
 * @param blob All native flag mapping domains.
 * @param byHash Select hash lookup for the explicit claim effect, otherwise slot lookup.
 * @param key Requested identity.
 * @param row Receives the saved account row.
 * @param slot Receives the evaluated flag slot.
 * @return False for ambiguous, reserved or non-account mappings.
 */
bool account_flag(std::span<const std::byte> blob,
                  bool byHash,
                  std::uint32_t key,
                  std::uint16_t& row,
                  std::uint16_t& slot) noexcept {
    bool found = false;
    for (auto descriptor : kMapDescriptors) {
        Array map{};
        if (!array(blob, descriptor, kMapClass, kUnlockMapRowStride, map)) {
            return false;
        }
        for (std::size_t index = 0; index < map.count; ++index) {
            const auto at = map.dataOffset + index * kUnlockMapRowStride;
            std::uint32_t hash{};
            std::uint16_t candidate{}, reserved{};
            if (!read(blob, at, hash)
                || !read(blob, at + kUnlockMapDestinationSlotOffset, candidate)
                || !read(blob, at + kMapReservedOffset, reserved)) {
                return false;
            }
            if ((byHash ? hash : candidate) != key) {
                continue;
            }
            if (found || descriptor != kAccountFlagMapDescriptor || reserved != 0
                || index >= state::unlocks::kAccountFlagCapacity) {
                return false;
            }
            found = true;
            row = static_cast<std::uint16_t>(index);
            slot = candidate;
        }
    }
    return found;
}

/**
 * Resolve one positive predicate without confusing evaluated class flags with saved rows.
 * @param source Native mapping tables.
 * @param operand Evaluated flag slot.
 * @param claimSlot Explicit claim flag, which cannot also be a positive prerequisite.
 * @param output Accumulates the optional class and unique saved prerequisites.
 * @return False for conflicting classes, unknown flags or excessive prerequisites.
 */
bool prerequisite(const VendorBundleSource& source,
                  std::uint32_t operand,
                  std::uint16_t claimSlot,
                  bundles::Definition& output) noexcept {
    std::optional<state::CharacterClass> characterClass;
    switch (operand) {
    case kHunterClassFlag:
        characterClass = state::CharacterClass::hunter;
        break;
    case kTitanClassFlag:
        characterClass = state::CharacterClass::titan;
        break;
    case kWarlockClassFlag:
        characterClass = state::CharacterClass::warlock;
        break;
    default:
        break;
    }
    if (characterClass.has_value()) {
        if (output.characterClass.has_value() && output.characterClass != characterClass) {
            return false;
        }
        output.characterClass = characterClass;
        return true;
    }
    std::uint16_t row{}, slot{};
    if (operand == claimSlot || !account_flag(source.flagMap, false, operand, row, slot)) {
        return false;
    }
    const auto held = std::span(output.requiredRows).first(output.requiredCount);
    if (std::find(held.begin(), held.end(), row) != held.end()) {
        return true;
    }
    if (output.requiredCount == output.requiredRows.size()) {
        return false;
    }
    output.requiredRows[output.requiredCount++] = row;
    return true;
}

/**
 * Flatten only conjunctions of positive flags and shared pools.
 * @param source Native pool and mapping tables.
 * @param blob Blob owning this instruction descriptor.
 * @param field Instruction-array descriptor.
 * @param claimSlot Explicit claim flag excluded from positive prerequisites.
 * @param budget Remaining instruction visits, shared by nested calls.
 * @param output Accumulates prerequisites; the caller discards it on refusal.
 * @return False for malformed stacks, cycles, unknown operators or unresolved flags.
 */
bool conjunction(const VendorBundleSource& source,
                 std::span<const std::byte> blob,
                 std::size_t field,
                 std::uint16_t claimSlot,
                 std::size_t& budget,
                 bundles::Definition& output) noexcept {
    Array program{};
    if (!array(blob, field, kInstructionClass, kUnlockInstructionStride, program)
        || program.count == 0 || program.count > budget) {
        return false;
    }
    std::size_t depth = 0;
    for (std::size_t index = 0; index < program.count; ++index) {
        if (budget == 0) {
            return false;
        }
        --budget;
        const auto at = program.dataOffset + index * kUnlockInstructionStride;
        std::uint32_t opcode{}, operand{};
        if (!read(blob, at, opcode) || !read(blob, at + kUnlockInstructionOperandOffset, operand)) {
            return false;
        }
        if (opcode == kUnlockReadFlagOpcode) {
            if (!prerequisite(source, operand, claimSlot, output)) {
                return false;
            }
            ++depth;
        } else if (opcode == kPoolOpcode) {
            Array pools{};
            if (!array(source.pools, kTableArrayDescriptor, kPoolClass, kPoolStride, pools)
                || operand >= pools.count
                || !conjunction(source,
                                source.pools,
                                pools.dataOffset + operand * kPoolStride + kNestedArrayOffset,
                                claimSlot,
                                budget,
                                output)) {
                return false;
            }
            ++depth;
        } else if (opcode == kUnlockAndOpcode && depth >= 2) {
            --depth;
        } else {
            return false;
        }
    }
    return depth == 1;
}

/**
 * Read independent purchase expressions without requiring their order or prerequisite count.
 * @param source Native blobs.
 * @param claimSlot Resolved slot of the explicit claim effect.
 * @param output Sale selector and extracted purchase gates.
 * @return False unless the explicit unclaimed predicate and all other gates are supported.
 */
bool gates(const VendorBundleSource& source,
           std::uint16_t claimSlot,
           bundles::Definition& output) noexcept {
    Array sales{}, expressions{};
    if (!array(source.vendor,
               kSalesOffset,
               state::build_data::vendors::kSaleRowClass,
               state::build_data::vendors::kSaleRowStride,
               sales)
        || output.saleIndex >= sales.count
        || !array(source.vendor,
                  sales.dataOffset + output.saleIndex * state::build_data::vendors::kSaleRowStride
                      + kPurchaseOffset,
                  kExpressionClass,
                  kExpressionStride,
                  expressions)
        || expressions.count == 0 || expressions.count > kGateInstructionBudget) {
        return false;
    }
    bool unclaimed = false;
    std::size_t budget = kGateInstructionBudget;
    for (std::size_t index = 0; index < expressions.count; ++index) {
        const auto field = expressions.dataOffset + index * kExpressionStride;
        Array program{};
        std::uint32_t opcode{}, operand{}, next{};
        if (!array(source.vendor, field, kInstructionClass, kUnlockInstructionStride, program)) {
            return false;
        }
        // Only the separately supported claim effect admits a NOT gate; it never creates a write.
        if (program.count == 2 && read(source.vendor, program.dataOffset, opcode)
            && opcode == kUnlockReadFlagOpcode
            && read(source.vendor, program.dataOffset + kUnlockInstructionOperandOffset, operand)
            && operand == claimSlot
            && read(source.vendor, program.dataOffset + kUnlockInstructionStride, next)
            && next == kUnlockNotOpcode) {
            if (unclaimed || budget < program.count) {
                return false;
            }
            budget -= static_cast<std::size_t>(program.count);
            unclaimed = true;
        } else if (!conjunction(source, source.vendor, field, claimSlot, budget, output)) {
            return false;
        }
    }
    return unclaimed;
}
} // namespace

/**
 * Resolve the native payout and eligibility without deriving write effects from predicates.
 * @param source Installed blobs and item index bounds.
 * @param effect Supported claim-on-success effect.
 * @param saleIndex Native sale row ordinal.
 * @param output Receives the extracted bundle only on success.
 * @return False for unsupported or malformed content.
 */
bool read_vendor_bundle(const VendorBundleSource& source,
                        const bundles::ClaimEffect& effect,
                        std::uint16_t saleIndex,
                        bundles::Definition& output) noexcept {
    bundles::Definition result{};
    result.sourceHash = effect.itemHash;
    result.saleIndex = saleIndex;
    std::uint16_t claimSlot{}, uniqueRow{}, uniqueSlot{};
    if (!account_flag(source.flagMap, true, effect.flagHash, result.claimRow, claimSlot)
        || !account_flag(source.flagMap, false, claimSlot, uniqueRow, uniqueSlot)
        || uniqueRow != result.claimRow
        || !read_item_bundle(source.item, source.rewards, source.itemCount, result.rewards)
        || !gates(source, claimSlot, result)) {
        return false;
    }
    output = result;
    return true;
}
} // namespace sunrise::middleware::content::packages::tables
