#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"

namespace sunrise::state::build_data {

/** @return True when the whole item definition table is in State. */
bool item_definitions_ready() noexcept {
    return items::count() != 0;
}

/** Publishes one dense item table and saves every ready build-data domain. */
bool publish_item_definitions(std::span<const items::Definition> definitions) noexcept {
    return publish_item_definitions(definitions, {});
}

/** Publishes dense items and sparse transitions through one persistence transaction. */
bool publish_item_definitions(std::span<const items::Definition> definitions,
                              std::span<const items::QuestTransition> transitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active()
           && transaction.finish(items::replace(definitions, transitions), items::clear);
}

/** Finds one authored item hash, only after a complete item table is published. */
bool find_item_definition(std::uint32_t definitionHash,
                          std::uint8_t bucketId,
                          items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find(definitionHash, bucketId, definition);
}

/** Finds one authored hash without needing a native bucket in settings. */
bool find_item_definition_hash(std::uint32_t definitionHash,
                               items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find_hash(definitionHash, definition);
}

/** Finds one installed item by the native index carried by a Collections request. */
bool find_item_definition_index(std::uint16_t definitionIndex,
                                items::Definition& definition) noexcept {
    definition = {};
    return item_definitions_ready() && items::find_index(definitionIndex, definition)
           && definition.definitionIndex == definitionIndex;
}

/** Finds one retained quest transition only after the complete item table is published. */
bool find_quest_transition(std::uint16_t sourceItemIndex,
                           items::QuestTransition& transition) noexcept {
    transition = {};
    return item_definitions_ready() && items::find_transition(sourceItemIndex, transition);
}

} // namespace sunrise::state::build_data
