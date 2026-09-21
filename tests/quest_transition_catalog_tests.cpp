#include <array>
#include <cstdio>
#include <cstdlib>

#include "state/build_data/items/item_catalog.h"

namespace {

namespace items = sunrise::state::build_data::items;

/** Unlimited Power's first retained transition exercises every preserved field family. */
constexpr items::QuestTransition kTransition{
    0,
    1,
    100,
    200,
    526,
    {{{462, 899, items::QuestPredicate::Input::family5}}},
    1,
    11481,
};

/** Stops the catalog check at its first failed contract. */
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "quest_transition_catalog: %s\n", message);
        std::abort();
    }
}

} // namespace

/** Verifies sparse publication, lookup, snapshot, ordering, and item-link rejection. */
void verify_quest_transition_catalog() {
    std::array<items::Definition, 2> definitions{};
    definitions[0].definitionHash = 1;
    definitions[0].definitionIndex = 0;
    definitions[0].bucketId = items::kPursuitBucketId;
    definitions[1].definitionHash = 2;
    definitions[1].definitionIndex = 1;
    definitions[1].bucketId = items::kPursuitBucketId;

    items::QuestTransition found{};
    std::array<items::QuestTransition, 1> snapshot{};
    std::size_t count = 1;
    check(items::replace(definitions, std::span<const items::QuestTransition>{})
              && items::transition_count() == 0 && !items::find_transition(0, found)
              && items::snapshot_transitions(snapshot, count) && count == 0,
          "empty transition catalog rejected or populated");

    check(items::replace(definitions, std::span<const items::QuestTransition>{&kTransition, 1}),
          "valid transition catalog rejected");
    check(items::transition_count() == 1 && items::find_transition(0, found)
              && found == kTransition,
          "published transition not found");

    check(items::snapshot_transitions(snapshot, count) && count == 1
              && snapshot.front() == kTransition,
          "transition snapshot differs");

    std::array<items::QuestTransition, 2> duplicates{kTransition, kTransition};
    check(!items::replace(definitions, duplicates), "duplicate source transition accepted");
    items::QuestTransition invalid = kTransition;
    invalid.successorItemIndex = 2;
    check(!items::replace(definitions, std::span<const items::QuestTransition>{&invalid, 1}),
          "out-of-range successor accepted");
    check(items::transition_count() == 1 && items::find_transition(0, found)
              && found == kTransition,
          "failed replacement changed catalog");
    items::clear();
}
