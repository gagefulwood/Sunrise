#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "../Sunrise/src/state/build_data/cache/internal.h"
#include "../Sunrise/src/state/build_data/cache/read/cache_payload_reader.h"
#include "../Sunrise/src/state/build_data/cache/records/format.h"
#include "../Sunrise/src/state/build_data/cache/records/validation.h"
#include "../Sunrise/src/state/build_data/cache/write/cache_payload_writer.h"

namespace test_data {

using namespace sunrise::state::build_data;
namespace content = sunrise::state::content;
namespace gameplay = sunrise::state::gameplay;

constexpr BuildIdentity kBuild{1598231435U, 145091072U, 0x12345678ULL};
constexpr std::uint16_t kSourceItemIndex = 7;
constexpr std::uint16_t kSuccessorItemIndex = 8;
constexpr int kExpectedArgumentCount = 2;
constexpr int kScratchDirectoryArgument = 1;

struct TransitionRecord {
    std::uint16_t sourceItemIndex{};
    std::uint16_t successorItemIndex{};
};

struct Domains {
    cache::records::InvestmentConstants constants{};
    std::array<content::Definition, 1> named{};
    std::array<items::Definition, 1> items{};
    std::array<items::QuestTransition, 1> questTransitions{};
    std::array<collectibles::Definition, 1> collectibles{};
    std::array<material_requirements::Definition, 1> materialRequirementSets{};
    std::array<items::socket_plugs::Rule, 1> socketPlugRules{};
    std::array<items::socket_plugs::Pool, 1> socketPlugPools{};
    std::array<inventory::buckets::Descriptor, 1> inventoryBuckets{};
    std::array<socket_entry_lists::Definition, 1> socketEntryLists{};
    std::array<progressions::Definition, 1> progressions{};
    std::array<scenarios::Definition, 1> scenarios{};
    std::array<scenarios::RosterGroup, 1> rosterGroups{};
    std::array<hash_names::Name, 1> hashNames{};
    std::array<gameplay::entity_position_profiles::Row, 1> positionProfiles{};
    gameplay::entity_position_profiles::Fingerprint positionFingerprint{};
    std::array<gameplay::entity_object_types::Row, 1> objectTypes{};

    Domains() {
        questTransitions.front().sourceItemIndex = kSourceItemIndex;
        questTransitions.front().successorItemIndex = kSuccessorItemIndex;
    }

    [[nodiscard]] cache::records::Domains view(bool populated) const noexcept {
        cache::records::Domains result{};
        result.constants = constants;
        result.named = named;
        result.items = items;
        result.questTransitions =
            populated ? std::span(questTransitions) : std::span(questTransitions).first(0);
        result.collectibles = collectibles;
        result.materialRequirementSets = materialRequirementSets;
        result.socketPlugRules = socketPlugRules;
        result.socketPlugPools = socketPlugPools;
        result.inventoryBuckets = inventoryBuckets;
        result.socketEntryLists = socketEntryLists;
        result.progressions = progressions;
        result.scenarios = scenarios;
        result.rosterGroups = rosterGroups;
        result.hashNames = hashNames;
        result.positionProfiles = positionProfiles;
        result.positionFingerprint = positionFingerprint;
        result.objectTypes = objectTypes;
        return result;
    }

    [[nodiscard]] cache::records::MutableDomains mutable_view() noexcept {
        cache::records::MutableDomains result{};
        result.constants = &constants;
        result.named = named;
        result.items = items;
        result.questTransitions = questTransitions;
        result.collectibles = collectibles;
        result.materialRequirementSets = materialRequirementSets;
        result.socketPlugRules = socketPlugRules;
        result.socketPlugPools = socketPlugPools;
        result.inventoryBuckets = inventoryBuckets;
        result.socketEntryLists = socketEntryLists;
        result.progressions = progressions;
        result.scenarios = scenarios;
        result.rosterGroups = rosterGroups;
        result.hashNames = hashNames;
        result.positionProfiles = positionProfiles;
        result.positionFingerprint = &positionFingerprint;
        result.objectTypes = objectTypes;
        return result;
    }
};

[[nodiscard]] std::uint64_t checksum(std::span<const items::QuestTransition> transitions) noexcept {
    std::uint64_t value = cache::records::kChecksumOffsetBasis;
    for (const items::QuestTransition& transition : transitions) {
        const TransitionRecord record{transition.sourceItemIndex, transition.successorItemIndex};
        for (const std::byte byte : std::as_bytes(std::span(&record, 1))) {
            value ^= std::to_integer<std::uint8_t>(byte);
            value *= cache::records::kChecksumPrime;
        }
    }
    return value;
}

} // namespace test_data

namespace sunrise::state::build_data::cache::records {

bool valid_domains(const BuildIdentity&, Domains) noexcept {
    return true;
}

} // namespace sunrise::state::build_data::cache::records

namespace sunrise::state::build_data::cache::writer {

bool payload_checksum(records::Domains domains, std::uint64_t& checksum) noexcept {
    checksum = test_data::checksum(domains.questTransitions);
    return true;
}

bool write_payload(HANDLE file, records::Domains domains) noexcept {
    for (const items::QuestTransition& transition : domains.questTransitions) {
        const test_data::TransitionRecord record{transition.sourceItemIndex,
                                                 transition.successorItemIndex};
        DWORD transferred = 0;
        if (WriteFile(file, &record, sizeof record, &transferred, nullptr) == FALSE
            || transferred != sizeof record) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::state::build_data::cache::writer

namespace sunrise::state::build_data::cache::read {

void clear(records::MutableDomains output) noexcept {
    if (output.constants != nullptr) {
        *output.constants = {};
    }
    if (output.positionFingerprint != nullptr) {
        *output.positionFingerprint = {};
    }
    std::fill(
        output.questTransitions.begin(), output.questTransitions.end(), items::QuestTransition{});
}

bool expected_size(const records::DomainCounts& counts, std::uint64_t& size) noexcept {
    size = sizeof(records::Header) + counts.questTransitions * sizeof(test_data::TransitionRecord);
    return true;
}

bool read_payload(HANDLE file,
                  const BuildIdentity&,
                  const records::InvestmentConstants&,
                  const gameplay::entity_position_profiles::Fingerprint&,
                  const records::DomainCounts& counts,
                  records::MutableDomains output,
                  std::uint64_t& checksum) noexcept {
    if (counts.questTransitions > output.questTransitions.size()) {
        return false;
    }
    for (std::size_t index = 0; index < counts.questTransitions; ++index) {
        test_data::TransitionRecord record{};
        if (!read_value(file, record)) {
            return false;
        }
        output.questTransitions[index].sourceItemIndex = record.sourceItemIndex;
        output.questTransitions[index].successorItemIndex = record.successorItemIndex;
    }
    checksum = test_data::checksum(output.questTransitions.first(counts.questTransitions));
    return true;
}

} // namespace sunrise::state::build_data::cache::read

namespace {

namespace cache = sunrise::state::build_data::cache;
namespace records = sunrise::state::build_data::cache::records;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "build_data_cache: %s\n", message);
        std::abort();
    }
}

[[nodiscard]] std::wstring path_of(std::wstring_view directory, const wchar_t* name) {
    std::wstring result(directory);
    result += L'\\';
    result += name;
    return result;
}

void copy_cache_file(const std::wstring& source, const std::wstring& destination) {
    check(CopyFileW(source.c_str(), destination.c_str(), FALSE) != FALSE,
          "cache fixture copy failed");
}

void mutate_header(const std::wstring& path, void (*mutate)(records::Header&)) {
    HANDLE file = CreateFileW(path.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    check(file != INVALID_HANDLE_VALUE, "cache fixture could not be opened for mutation");
    records::Header header{};
    DWORD transferred = 0;
    check(ReadFile(file, &header, sizeof header, &transferred, nullptr) != FALSE
              && transferred == sizeof header,
          "cache fixture header could not be read");
    mutate(header);
    LARGE_INTEGER beginning{};
    check(SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) != FALSE,
          "cache fixture could not rewind");
    transferred = 0;
    check(WriteFile(file, &header, sizeof header, &transferred, nullptr) != FALSE
              && transferred == sizeof header && CloseHandle(file) != FALSE,
          "cache fixture header could not be replaced");
}

[[nodiscard]] cache::LoadStatus load(const std::wstring& path,
                                     const sunrise::state::build_data::BuildIdentity& build,
                                     test_data::Domains& output,
                                     records::DomainCounts& counts) {
    return cache::load(path.c_str(), build, output.mutable_view(), counts);
}

void verify_round_trip(const std::wstring& directory,
                       const wchar_t* name,
                       bool populated,
                       const test_data::Domains& input) {
    const std::wstring path = path_of(directory, name);
    check(cache::write(directory.c_str(),
                       path.c_str(),
                       test_data::kBuild,
                       input.view(populated),
                       cache::WriteDisposition::createOnly),
          "cache write failed");
    test_data::Domains output;
    records::DomainCounts counts{};
    check(load(path, test_data::kBuild, output, counts) == cache::LoadStatus::loaded,
          "cache round trip failed");
    check(counts.questTransitions == (populated ? 1U : 0U),
          "cache round trip changed the transition count");
    if (populated) {
        check(output.questTransitions.front().sourceItemIndex == test_data::kSourceItemIndex
                  && output.questTransitions.front().successorItemIndex
                         == test_data::kSuccessorItemIndex,
              "cache round trip changed the transition row");
    }
}

void verify_rejections(const std::wstring& directory) {
    const std::wstring populated = path_of(directory, L"populated.bin");
    test_data::Domains output;
    records::DomainCounts counts{};

    auto wrongBuild = test_data::kBuild;
    ++wrongBuild.imageTimestamp;
    check(load(populated, wrongBuild, output, counts) == cache::LoadStatus::stale,
          "wrong build identity was accepted");
    check(load(path_of(directory, L"missing.bin"), test_data::kBuild, output, counts)
              == cache::LoadStatus::missing,
          "missing cache was not reported as missing");

    const std::wstring stale = path_of(directory, L"stale.bin");
    copy_cache_file(populated, stale);
    mutate_header(stale, [](records::Header& header) { --header.version; });
    check(load(stale, test_data::kBuild, output, counts) == cache::LoadStatus::stale,
          "old cache version was accepted");

    const std::wstring malformed = path_of(directory, L"malformed.bin");
    copy_cache_file(populated, malformed);
    mutate_header(malformed, [](records::Header& header) { ++header.payloadChecksum; });
    check(load(malformed, test_data::kBuild, output, counts) == cache::LoadStatus::invalid,
          "bad payload checksum was accepted");

    const std::wstring missingDomain = path_of(directory, L"missing-domain.bin");
    copy_cache_file(populated, missingDomain);
    mutate_header(missingDomain, [](records::Header& header) { header.namedCount = 0; });
    check(load(missingDomain, test_data::kBuild, output, counts) == cache::LoadStatus::invalid,
          "missing required domain was accepted");

    const std::wstring truncated = path_of(directory, L"truncated.bin");
    copy_cache_file(populated, truncated);
    HANDLE file = CreateFileW(truncated.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    check(file != INVALID_HANDLE_VALUE, "cache fixture could not be opened for truncation");
    LARGE_INTEGER size{};
    check(GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0,
          "cache fixture size could not be read");
    --size.QuadPart;
    check(SetFilePointerEx(file, size, nullptr, FILE_BEGIN) != FALSE && SetEndOfFile(file) != FALSE
              && CloseHandle(file) != FALSE,
          "cache fixture could not be truncated");
    check(load(truncated, test_data::kBuild, output, counts) == cache::LoadStatus::invalid,
          "truncated cache was accepted");
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    check(argumentCount == test_data::kExpectedArgumentCount, "expected a scratch directory");
    const std::wstring directory(arguments[test_data::kScratchDirectoryArgument]);
    check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE,
          "scratch directory could not be created");

    test_data::Domains input;
    verify_round_trip(directory, L"empty.bin", false, input);
    verify_round_trip(directory, L"populated.bin", true, input);
    verify_rejections(directory);

    for (const wchar_t* name : {L"empty.bin",
                                L"populated.bin",
                                L"stale.bin",
                                L"malformed.bin",
                                L"missing-domain.bin",
                                L"truncated.bin"}) {
        check(DeleteFileW(path_of(directory, name).c_str()) != FALSE,
              "scratch cache file could not be removed");
    }
    check(RemoveDirectoryW(directory.c_str()) != FALSE, "scratch directory could not be removed");
    std::puts("PASS: build-data cache empty/populated round trips and rejection boundaries");
    return EXIT_SUCCESS;
}
