#pragma once

#include <cstdint>

namespace sunrise::server::bap {

struct Session;

namespace encrypted {

struct ServiceOutcome;

namespace queuez {

/** Result of one bounded deferred quest-completion attempt. */
enum class QuestCompletionResult : std::uint8_t {
    idle,
    waitingForPublication,
    noWork,
    advanced,
    retryScheduled,
    dropped,
};

/**
 * @param outcome Prepared service result whose State transaction has not yet been consumed.
 * @return Character affected by an event that may satisfy quest predicates, or zero.
 */
[[nodiscard]] std::uint64_t
quest_completion_event_character(const ServiceOutcome& outcome) noexcept;

/**
 * Replaces any older obligation with one produced by the named character.
 * @param session Connection that will service the deferred work.
 * @param characterSoid Nonzero character that produced the progression event.
 */
void arm_quest_completion(Session& session, std::uint64_t characterSoid) noexcept;

/** @param session Connection whose obligation was invalidated by a character change. */
void cancel_quest_completion(Session& session) noexcept;

/**
 * Processes at most one supported quest transition and retains bounded follow-up work.
 * @param session Connection that owns the character-bound obligation.
 * @return Outcome that tells the deferred pump whether work remains or was refused.
 */
[[nodiscard]] QuestCompletionResult process_quest_completion(Session& session) noexcept;

} // namespace queuez
} // namespace encrypted
} // namespace sunrise::server::bap
