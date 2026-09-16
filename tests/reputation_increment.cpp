#include <array>
#include <cassert>
#include <iostream>
#include <limits>

#include "server/bap/encrypted/queuez/queuez_state_validation.h"

namespace wire = sunrise::middleware::queuez;
namespace queuez = sunrise::server::bap::encrypted::queuez;

/** Checks publication narrowing without touching a database or game process. */
int main() {
    // Synthetic account, character and item keys deliberately differ from their definition IDs.
    constexpr std::uint64_t accountKey = 100;
    constexpr std::uint64_t characterKey = 200;
    constexpr std::uint64_t itemKey = 300;
    std::array<std::byte, 1> payload{std::byte{1}};
    std::array<wire::Object, 3> objects{{
        {10, accountKey, wire::Encoding::oodle, payload},
        {20, characterKey, wire::Encoding::oodle, payload},
        {30, itemKey, wire::Encoding::oodle, payload},
    }};
    queuez::SessionState before{};
    before.family4Active = true;
    before.family4RootSoid = accountKey;
    before.family4Version = 7;
    before.family4ResidentCount = static_cast<std::uint16_t>(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        before.family4Residents[i] = {objects[i].version, objects[i].id};
    }
    const wire::Family full{queuez::kAccountFamilyType,
                            accountKey,
                            before.family4Version + 1,
                            wire::kFullSnapshotFlag,
                            objects};
    auto family = full;
    queuez::SessionState after{};
    assert(queuez::stage_reputation_increment(before, characterKey, family, after));
    assert(family.flags == 0 && family.objects.size() == 2);
    assert(family.objects.data() == objects.data());
    assert(family.objects[0].payload.data() == payload.data());
    assert(family.objects[1].payload.data() == payload.data());
    assert(after.family4Version == full.version && queuez::valid(after));
    assert(after.family4ResidentCount == before.family4ResidentCount);
    assert(after.family4Residents[2].objectSoid == itemKey);

    const auto refused = [&](wire::Family candidate, std::uint64_t character) {
        queuez::SessionState unchanged = before;
        const auto count = candidate.objects.size();
        const auto flags = candidate.flags;
        assert(!queuez::stage_reputation_increment(before, character, candidate, unchanged));
        assert(candidate.objects.size() == count && candidate.flags == flags);
        assert(unchanged.family4Version == before.family4Version);
    };
    refused(full, 0);
    refused(full, accountKey);
    refused(full, characterKey + 1);
    family = full;
    family.version += 1;
    refused(family, characterKey);
    family = full;
    family.objects = family.objects.first(2);
    refused(family, characterKey);
    objects[2].version += 1;
    refused(full, characterKey);
    objects[2].version = itemKey;
    objects[2].id += 1;
    refused(full, characterKey);
    objects[2].id -= 1;
    objects[2].version = characterKey;
    refused(full, characterKey);
    objects[2].version = itemKey;
    family = full;
    family.flags = 0;
    refused(family, characterKey);
    before.family4Version = (std::numeric_limits<std::int32_t>::max)();
    refused(full, characterKey);
    std::cout
        << "PASS: two-object publication, payload identity, revision, residents and fallbacks\n";
}
