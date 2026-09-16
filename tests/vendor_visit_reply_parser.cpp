#include <algorithm>
#include <array>
// NOLINTNEXTLINE(portability-restrict-system-includes) - Standalone test assertions.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
// NOLINTNEXTLINE(portability-restrict-system-includes) - Retained fixture paths.
#include <filesystem>
// NOLINTNEXTLINE(portability-restrict-system-includes) - Retained fixture input.
#include <fstream>
#include <limits>
#include <span>
#include <vector>

#include "client/content/vendors/layout.h"
#include "client/content/vendors/visit_reply_parser.h"
#include "middleware/content/packages/tables/definition_index_table.h"
#include "state/build_data/cache/records/codec.h"
#include "state/build_data/vendors/vendor_catalog.h"
#include "state/unlocks/definition.h"

namespace {

namespace fs = std::filesystem;
namespace parser = sunrise::client::content::vendors;
namespace records = sunrise::state::build_data::cache::records;
namespace tables = sunrise::middleware::content::packages::tables;
namespace vendors = sunrise::state::build_data::vendors;

/** Retained Banshee fixture identity from vendor index row 22. */
constexpr std::uint32_t kFixtureVendorHash = 0x280FB4FDU;
constexpr std::uint32_t kFixtureVendorTag = 0x81319094U;
/** Interaction 21 is mutated by the negative reply and redundant-flag checks. */
constexpr std::size_t kFixtureVisitInteraction = 21;

/**
 * Reads one nonempty retained fixture.
 * @param path Fixture path.
 * @return Complete fixture bytes.
 */
std::vector<std::byte> file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    assert(stream && stream.tellg() > 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    assert(stream.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size())));
    return bytes;
}

template <typename Value>
void write(std::vector<std::byte>& bytes, std::size_t offset, Value value) {
    assert(offset <= bytes.size() && bytes.size() - offset >= sizeof value);
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

/**
 * Builds the minimum vendor definition the pure parser and catalog checks need.
 * @param investment Retained investment vendor bytes.
 * @return Definition bound to the retained interaction array.
 */
vendors::Definition definition(std::span<const std::byte> investment) {
    tables::Array interactions{};
    assert(tables::find_array_at(investment, parser::kThirdArrayDescriptor, interactions));
    assert(interactions.count <= (std::numeric_limits<std::uint16_t>::max)());
    vendors::Definition value{};
    value.definitionHash = kFixtureVendorHash;
    value.definitionTag = kFixtureVendorTag;
    value.definitionClass = vendors::kDefinitionClass;
    value.definitionSize = static_cast<std::uint32_t>(investment.size());
    value.thirdRowBase = static_cast<std::uint32_t>(interactions.dataOffset);
    value.thirdRowClass = interactions.elementClass;
    value.thirdCount = static_cast<std::uint16_t>(interactions.count);
    return value;
}

/**
 * Checks all strict replies retained from the Banshee fixture.
 * @param value Parsed vendor definition.
 */
void expect_retained(const vendors::Definition& value) {
    struct Expected {
        std::uint16_t interaction;
        std::array<std::uint16_t, 2> flags;
        std::array<std::uint16_t, 2> rows;
    };
    // Exact retained interaction, flag-slot, and account-row fixture values.
    constexpr std::array expected{
        Expected{10,
                 {779, 831},
                 {vendors::kUnavailableAccountFlagRow, vendors::kUnavailableAccountFlagRow}},
        Expected{
            kFixtureVisitInteraction, {20724, 11028}, {vendors::kUnavailableAccountFlagRow, 6845}},
        Expected{23, {20592, 10510}, {vendors::kUnavailableAccountFlagRow, 6454}},
        Expected{24, {20746, 11241}, {vendors::kUnavailableAccountFlagRow, 6994}},
        Expected{25, {20818, 11804}, {vendors::kUnavailableAccountFlagRow, 7466}},
    };
    assert(value.visitReplyCount == expected.size());
    for (std::size_t row = 0; row < expected.size(); ++row) {
        assert(value.visitReplies[row].interactionIndex == expected[row].interaction);
        assert(value.visitReplies[row].replyIndex == 0);
        assert(value.visitReplies[row].flags == expected[row].flags);
        assert(value.visitReplies[row].accountFlagRows == expected[row].rows);
    }
}

/**
 * Points one synthetic descriptor at an existing retained array header.
 * @param bytes Mutable fixture bytes owning the descriptor.
 * @param descriptor Descriptor offset.
 * @param source Existing resolved array whose header is reused.
 */
void repoint(std::vector<std::byte>& bytes, std::size_t descriptor, const tables::Array& source) {
    const std::size_t header = source.dataOffset - tables::kHeaderSkip;
    const std::int64_t relative =
        static_cast<std::int64_t>(header)
        - static_cast<std::int64_t>(descriptor + tables::kUnlockExpressionPointerOffset);
    write(bytes, descriptor, source.count);
    write(bytes, descriptor + tables::kUnlockExpressionPointerOffset, relative);
}

} // namespace

/**
 * Runs retained parsing plus cache, canonical, and fail-closed checks.
 * @param argc Must name one retained fixture directory.
 * @param argv Program arguments.
 * @return Zero when every assertion passes.
 */
int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root(argv[1]);
    const std::vector<std::byte> investment = file(root / "81319094.bin");
    const std::vector<std::byte> companion = file(root / "81318F25.bin");
    const std::vector<std::byte> flagMap = file(root / "81319322.bin");

    parser::AccountFlagRows accountFlagRows{};
    assert(parser::read_account_flag_rows(flagMap, accountFlagRows));
    vendors::Definition value = definition(investment);
    assert(parser::parse_visit_replies(investment, companion, accountFlagRows, value));
    expect_retained(value);

    records::VendorDefinitionRecord record{};
    assert(records::encode(value, record));
    vendors::Definition decoded{};
    assert(records::decode(record, decoded));
    expect_retained(decoded);

    const std::array index{vendors::IndexEntry{value.definitionHash, value.definitionTag, 0}};
    value.index = 0;
    assert(vendors::valid(index, std::span(&value, 1), {}, {}));
    vendors::Definition malformed = value;
    malformed.visitReplies[0].accountFlagRows[1] =
        static_cast<std::uint16_t>(sunrise::state::unlocks::kAccountFlagCapacity);
    assert(!vendors::valid(index, std::span(&malformed, 1), {}, {}));
    malformed = value;
    malformed.visitReplies[value.visitReplyCount].flags[0] = 1;
    assert(!vendors::valid(index, std::span(&malformed, 1), {}, {}));

    tables::Array presentations{};
    assert(tables::find_array_at(
        companion, parser::kCompanionInteractionArrayDescriptor, presentations));
    tables::Array replies{};
    const std::size_t row21 =
        presentations.dataOffset + kFixtureVisitInteraction * parser::kCompanionInteractionStride;
    assert(tables::find_array_at(companion, row21, replies));
    std::vector<std::byte> wideType = companion;
    wideType[replies.dataOffset + parser::kCompanionReplyTypeOffset + 1] = std::byte{1};
    value = definition(investment);
    assert(parser::parse_visit_replies(investment, wideType, accountFlagRows, value));
    assert(value.visitReplyCount == 4);
    assert(std::none_of(value.visitReplies.begin(),
                        value.visitReplies.begin() + value.visitReplyCount,
                        [](const vendors::VisitReply& reply) {
                            return reply.interactionIndex == kFixtureVisitInteraction;
                        }));

    tables::Array interactions{};
    tables::Array visibility{};
    tables::Array investmentReplies{};
    assert(tables::find_array_at(investment, parser::kThirdArrayDescriptor, interactions));
    const std::size_t sourceInvestment =
        interactions.dataOffset + kFixtureVisitInteraction * vendors::kThirdRowStride;
    assert(tables::find_array_at(
        investment, sourceInvestment + parser::kInteractionVisibilityDescriptor, visibility));
    assert(tables::find_array_at(
        investment, sourceInvestment + parser::kInteractionReplyDescriptor, investmentReplies));
    std::uint32_t firstFlag = 0;
    std::memcpy(&firstFlag,
                investment.data() + visibility.dataOffset + sizeof(std::uint32_t),
                sizeof firstFlag);
    std::vector<std::byte> redundantFlags = investment;
    write(redundantFlags,
          visibility.dataOffset + parser::kVisibilityInstructionStride + sizeof(std::uint32_t),
          firstFlag);
    value = definition(redundantFlags);
    assert(parser::parse_visit_replies(redundantFlags, companion, accountFlagRows, value));
    assert(value.visitReplyCount == 4);
    assert(std::none_of(value.visitReplies.begin(),
                        value.visitReplies.begin() + value.visitReplyCount,
                        [](const vendors::VisitReply& reply) {
                            return reply.interactionIndex == kFixtureVisitInteraction;
                        }));

    std::vector<std::byte> overflowInvestment = investment;
    std::vector<std::byte> overflowCompanion = companion;
    for (std::size_t row = 0; row <= vendors::kVisitReplyCapacity; ++row) {
        const std::size_t investmentRow = interactions.dataOffset + row * vendors::kThirdRowStride;
        repoint(overflowInvestment,
                investmentRow + parser::kInteractionVisibilityDescriptor,
                visibility);
        write(overflowInvestment,
              investmentRow + parser::kInteractionRetirementDescriptor,
              std::uint64_t{});
        write(overflowInvestment,
              investmentRow + parser::kInteractionRetirementDescriptor + 8,
              std::int64_t{});
        repoint(overflowInvestment,
                investmentRow + parser::kInteractionReplyDescriptor,
                investmentReplies);
        repoint(overflowCompanion,
                presentations.dataOffset + row * parser::kCompanionInteractionStride,
                replies);
    }
    value = definition(overflowInvestment);
    assert(!parser::parse_visit_replies(
        overflowInvestment, overflowCompanion, accountFlagRows, value));
    assert(value.visitReplyCount == 0);
    assert(std::all_of(value.visitReplies.begin(), value.visitReplies.end(), [](const auto& reply) {
        return reply.interactionIndex == 0 && reply.replyIndex == 0
               && reply.flags == std::array<std::uint16_t, 2>{}
               && reply.accountFlagRows
                      == std::array<std::uint16_t, 2>{vendors::kUnavailableAccountFlagRow,
                                                      vendors::kUnavailableAccountFlagRow};
    }));
}
