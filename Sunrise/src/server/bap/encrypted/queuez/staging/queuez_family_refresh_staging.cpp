#include <algorithm>
#include <cstddef>
#include <limits>

#include "../queuez_state_validation.h"

namespace sunrise::server::bap::encrypted::queuez {

/** Replaces an active Family-4 manifest at exactly the next peer-local version. */
bool stage_family4_refresh(const SessionState& before,
                           const middleware::queuez::Family& family,
                           SessionState& after) noexcept {
    after = before;
    if (!valid(before) || !before.family4Active || before.family4RootSoid == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()
        || family.type != kAccountFamilyType || family.rootSoid != before.family4RootSoid
        || family.version != before.family4Version + 1
        || family.flags != middleware::queuez::kFullSnapshotFlag || family.objects.empty()
        || family.objects.size() > kResidentCapacity
        || family.objects.size()
               > static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())) {
        return false;
    }

    SessionState candidate = before;
    candidate.family4Residents = {};
    candidate.family4Version = family.version;
    candidate.family4ResidentCount = static_cast<std::uint16_t>(family.objects.size());
    for (std::size_t index = 0; index < family.objects.size(); ++index) {
        const middleware::queuez::Object& object = family.objects[index];
        if (object.id == 0 || object.version == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (family.objects[prior].version == object.version) {
                return false;
            }
        }
        candidate.family4Residents[index] = ResidentObject{object.version, object.id};
    }
    if (candidate.family4Residents.front().objectSoid != before.family4RootSoid
        || !valid(candidate)) {
        return false;
    }
    after = candidate;
    return true;
}

/**
 * Keeps item residents while publishing payment and reputation in one revision.
 * @param before Peer state before the pending refresh.
 * @param characterSoid Character credited by the reputation transaction.
 * @param family Complete refresh; narrowed only on success.
 * @param after Receives the next revision; unchanged on failure.
 * @return False when the refresh must retain full-snapshot semantics.
 */
bool stage_reputation_increment(const SessionState& before,
                                std::uint64_t characterSoid,
                                middleware::queuez::Family& family,
                                SessionState& after) noexcept {
    /** Reputation changes account payment and selected-character progression only. */
    constexpr std::size_t kReputationObjectCount = 2;
    /** Complete snapshots place the selected character directly after the account. */
    constexpr std::size_t kCharacterObjectIndex = 1;
    /** An increment leaves the full-snapshot bit clear. */
    constexpr std::uint8_t kIncrementalFlags = 0;
    SessionState refreshed{};
    if (characterSoid == 0 || characterSoid == before.family4RootSoid
        || family.objects.size() < kReputationObjectCount
        || family.objects[kCharacterObjectIndex].version != characterSoid
        || !stage_family4_refresh(before, family, refreshed)
        || refreshed.family4ResidentCount != before.family4ResidentCount) {
        return false;
    }
    const auto residents = std::span(before.family4Residents).first(before.family4ResidentCount);
    for (const auto& object : family.objects) {
        if (std::none_of(residents.begin(), residents.end(), [&](const ResidentObject& resident) {
                return resident.objectSoid == object.version && resident.definitionId == object.id;
            })) {
            return false;
        }
    }
    family.objects = family.objects.first(kReputationObjectCount);
    family.flags = kIncrementalFlags;
    after = before;
    after.family4Version = family.version;
    return true;
}

} // namespace sunrise::server::bap::encrypted::queuez
