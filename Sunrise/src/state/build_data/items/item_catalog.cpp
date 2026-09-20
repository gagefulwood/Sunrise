#include "item_catalog.h"

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::items {
namespace {

/** A lookup table twice the row count keeps the load factor under 50 percent. */
constexpr std::size_t kLookupCapacity = kDefinitionCapacity * 2;
/** An all-one row cannot clash with a real native definition index. */
constexpr std::uint16_t kEmptyLookupRow = (std::numeric_limits<std::uint16_t>::max)();
/** The standard 64-bit FNV-1a offset basis starts each item lookup hash. */
constexpr std::uint64_t kHashOffsetBasis = 14695981039346656037ULL;
/** The standard 64-bit FNV-1a prime mixes the item hash and bucket bytes. */
constexpr std::uint64_t kHashPrime = 1099511628211ULL;
/** Four definition-hash bytes precede the bucket byte in the lookup key. */
constexpr std::size_t kDefinitionHashByteCount = sizeof(std::uint32_t);

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;
std::vector<QuestTransition> g_questTransitions;
// Open-addressed probes into the dense rows, rebuilt with them under the same exclusive hold.
std::array<std::uint16_t, kLookupCapacity> g_lookup{};
std::array<std::uint16_t, kLookupCapacity> g_hashLookup{};

static_assert((kLookupCapacity & (kLookupCapacity - 1)) == 0);

/** @return FNV lookup value mixed from the definition hash. */
[[nodiscard]] std::uint64_t mix_definition_hash(std::uint32_t definitionHash) noexcept {
    std::uint64_t hash = kHashOffsetBasis;
    std::uint32_t value = definitionHash;
    for (std::size_t index = 0; index < kDefinitionHashByteCount; ++index) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= kHashPrime;
        value >>= 8U;
    }
    return hash;
}

/** @return Start slot for the hash-and-bucket lookup. */
[[nodiscard]] std::size_t start_slot(const Definition& definition) noexcept {
    std::uint64_t hash = mix_definition_hash(definition.definitionHash);
    hash ^= definition.bucketId;
    hash *= kHashPrime;
    return static_cast<std::size_t>(hash) & (kLookupCapacity - 1);
}

/** @return Start slot for the hash-only lookup. */
[[nodiscard]] std::size_t start_hash_slot(std::uint32_t definitionHash) noexcept {
    return static_cast<std::size_t>(mix_definition_hash(definitionHash)) & (kLookupCapacity - 1);
}

/**
 * Inserts one checked row with a known inventory bucket into the empty lookup table.
 * @param definition Dense installed-build mapping to index.
 */
void insert_lookup(const Definition& definition) noexcept {
    const std::size_t start = start_slot(definition);
    for (std::size_t probe = 0; probe < g_lookup.size(); ++probe) {
        std::uint16_t& row = g_lookup[(start + probe) & (g_lookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            row = definition.definitionIndex;
            return;
        }
    }
}

/**
 * Inserts one checked row into the empty hash-only lookup table.
 * @param definition Dense installed-build mapping to index.
 */
void insert_hash_lookup(const Definition& definition) noexcept {
    const std::size_t start = start_hash_slot(definition.definitionHash);
    for (std::size_t probe = 0; probe < g_hashLookup.size(); ++probe) {
        std::uint16_t& row = g_hashLookup[(start + probe) & (g_hashLookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            row = definition.definitionIndex;
            return;
        }
    }
}

} // namespace

/** Clears every generated item mapping under the catalog lock. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
    std::vector<QuestTransition>{}.swap(g_questTransitions);
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    std::fill(g_hashLookup.begin(), g_hashLookup.end(), kEmptyLookupRow);
}

/** Checks that the native indices cover the whole range once each, in any input order. */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.definitionIndex >= definitions.size() || occupied[definition.definitionIndex]
            || !valid(definition.questInitialization)
            || (definition.questInitialization.scope != QuestInitialization::Scope::none
                && definition.bucketId != kPursuitBucketId)) {
            return false;
        }
        occupied[definition.definitionIndex] = true;
    }
    return true;
}

/**
 * Checks sparse transitions against the complete dense item table.
 * @param definitions Candidate installed-build mappings.
 * @param transitions Candidate transitions sorted by source item index.
 * @return True when every transition names matching pursuit rows exactly once.
 */
bool valid(std::span<const Definition> definitions,
           std::span<const QuestTransition> transitions) noexcept {
    if (!valid(definitions) || transitions.size() > kQuestTransitionCapacity) {
        return false;
    }
    std::uint16_t previous = kUnavailableQuestItemIndex;
    for (const QuestTransition& transition : transitions) {
        if (!items::valid(transition)
            || (previous != kUnavailableQuestItemIndex
                && previous >= transition.sourceItemIndex)
            || transition.sourceItemIndex >= definitions.size()
            || transition.successorItemIndex >= definitions.size()
            || definitions[transition.sourceItemIndex].definitionIndex
                   != transition.sourceItemIndex
            || definitions[transition.successorItemIndex].definitionIndex
                   != transition.successorItemIndex
            || definitions[transition.sourceItemIndex].bucketId != kPursuitBucketId
            || definitions[transition.successorItemIndex].bucketId != kPursuitBucketId) {
            return false;
        }
        previous = transition.sourceItemIndex;
    }
    return true;
}

/** Rebuilds the dense rows and the lookups, only after the whole input passes the checks. */
bool replace(std::span<const Definition> definitions) noexcept {
    return replace(definitions, {});
}

/**
 * Rebuilds dense items, sparse transitions, and lookups under one exclusive hold.
 * @param definitions Complete dense installed-build mappings.
 * @param transitions Supported transitions sorted by source item index.
 * @return True when both domains validate and publish together.
 */
bool replace(std::span<const Definition> definitions,
             std::span<const QuestTransition> transitions) noexcept {
    if (!valid(definitions, transitions)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    std::fill(g_hashLookup.begin(), g_hashLookup.end(), kEmptyLookupRow);
    // valid() proved each index appears once, so every row lands in its own slot.
    const std::span<Definition> storage = g_definitions.reset(definitions.size());
    if (storage.size() != definitions.size()) {
        return false;
    }
    for (const Definition& definition : definitions) {
        storage[definition.definitionIndex] = definition;
        insert_hash_lookup(definition);
        // A row with no bucket is still reachable by native index, but not by bucket lookup.
        if (definition.bucketId != kUnresolvedBucketId) {
            insert_lookup(definition);
        }
    }
    g_questTransitions.assign(transitions.begin(), transitions.end());
    return true;
}

/** Probes one hash-only key and rejects duplicate installed mappings. */
bool find_hash(std::uint32_t definitionHash, Definition& definition) noexcept {
    definition = {};
    const std::size_t start = start_hash_slot(definitionHash);
    std::uint16_t match = kEmptyLookupRow;
    bool ambiguous = false;
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    for (std::size_t probe = 0; probe < g_hashLookup.size(); ++probe) {
        const std::uint16_t row = g_hashLookup[(start + probe) & (g_hashLookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            break;
        }
        if (rows[row].definitionHash == definitionHash) {
            ambiguous = match != kEmptyLookupRow;
            match = row;
        }
    }
    if (!ambiguous && match != kEmptyLookupRow) {
        definition = rows[match];
    }
    return !ambiguous && match != kEmptyLookupRow;
}

/** Probes one hash-and-bucket key and rejects duplicate installed mappings. */
bool find(std::uint32_t definitionHash, std::uint8_t bucketId, Definition& definition) noexcept {
    definition = {};
    if (bucketId == kUnresolvedBucketId) {
        return false;
    }
    const Definition key{definitionHash, 0, bucketId};
    const std::size_t start = start_slot(key);
    std::uint16_t match = kEmptyLookupRow;
    bool ambiguous = false;
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    for (std::size_t probe = 0; probe < g_lookup.size(); ++probe) {
        const std::uint16_t row = g_lookup[(start + probe) & (g_lookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            break;
        }
        const Definition& candidate = rows[row];
        if (candidate.definitionHash == definitionHash && candidate.bucketId == bucketId) {
            ambiguous = match != kEmptyLookupRow;
            match = row;
        }
    }
    if (!ambiguous && match != kEmptyLookupRow) {
        definition = rows[match];
    }
    return !ambiguous && match != kEmptyLookupRow;
}

/** Finds one dense installed-build row by its native definition index. */
bool find_index(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = static_cast<std::size_t>(definitionIndex) < rows.size();
    if (found) {
        definition = rows[definitionIndex];
    }
    return found;
}

/** Copies the dense rows in native-index order, without exposing the catalog storage. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/**
 * Copies the sparse transition catalog without exposing its storage.
 * @param output Caller-owned transition storage.
 * @param count Receives the copied row count, or zero on failure.
 * @return False only when the storage cannot hold the catalog.
 */
bool snapshot_transitions(std::span<QuestTransition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    count = 0;
    if (output.size() < g_questTransitions.size()) {
        return false;
    }
    std::copy(g_questTransitions.begin(), g_questTransitions.end(), output.begin());
    count = g_questTransitions.size();
    return true;
}

/**
 * Finds one transition by its unique source item index.
 * @param sourceItemIndex Native source item index.
 * @param transition Receives the matching transition; cleared when absent.
 * @return True when the catalog contains the source stage.
 */
bool find_transition(std::uint16_t sourceItemIndex, QuestTransition& transition) noexcept {
    transition = {};
    const std::shared_lock guard(g_lock);
    const auto found = std::lower_bound(
        g_questTransitions.begin(),
        g_questTransitions.end(),
        sourceItemIndex,
        [](const QuestTransition& row, std::uint16_t index) {
            return row.sourceItemIndex < index;
        });
    if (found == g_questTransitions.end() || found->sourceItemIndex != sourceItemIndex) {
        return false;
    }
    transition = *found;
    return true;
}

/** @return Number of installed-build item mappings, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

/** @return Sparse transition count, read under the catalog lock. */
std::size_t transition_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_questTransitions.size();
}

} // namespace sunrise::state::build_data::items
