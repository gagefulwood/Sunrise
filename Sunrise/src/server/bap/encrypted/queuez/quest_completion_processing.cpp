#include "quest_completion_processing.h"

#include "../../../../core/logging/log.h"
#include "../../../../state/account/inventory/inventory_state.h"
#include "../../../../state/runtime/state_quest_transition_runtime.h"
#include "../../internal.h"
#include "../internal.h"

namespace sunrise::server::bap::encrypted::queuez {
namespace {

/** One later servicing pass may recover a transient prepare or commit failure. */
constexpr std::uint8_t kQuestCompletionRetryLimit = 1;

[[nodiscard]] QuestCompletionResult retry_or_drop(Session& session) noexcept {
    if (session.questCompletion.retryCount < kQuestCompletionRetryLimit) {
        ++session.questCompletion.retryCount;
        return QuestCompletionResult::retryScheduled;
    }
    cancel_quest_completion(session);
    return QuestCompletionResult::dropped;
}

} // namespace

std::uint64_t quest_completion_event_character(const ServiceOutcome& outcome) noexcept {
    namespace inventory = state::account::inventory;
    if (const auto* transaction = transaction_if<EquipmentSwapTransaction>(outcome);
        transaction != nullptr && transaction->pending != nullptr
        && transaction->pending->equipmentSlotIndex
               <= static_cast<std::size_t>(inventory::EquipmentSlot::classItem)) {
        return transaction->pending->characterSoid;
    }
    if (const auto* transaction = transaction_if<ItemAcquisitionTransaction>(outcome);
        transaction != nullptr && transaction->pending != nullptr) {
        return transaction->pending->characterSoid;
    }
    return 0;
}

void arm_quest_completion(Session& session, std::uint64_t characterSoid) noexcept {
    if (characterSoid == 0) {
        return;
    }
    session.questCompletion.characterSoid = characterSoid;
    session.questCompletion.retryCount = 0;
    session.questCompletion.armed = true;
}

void cancel_quest_completion(Session& session) noexcept {
    session.questCompletion = {};
}

QuestCompletionResult process_quest_completion(Session& session) noexcept {
    if (!session.questCompletion.armed) {
        return QuestCompletionResult::idle;
    }
    if (session.investmentRefreshArmed || session.accountResyncArmed) {
        return QuestCompletionResult::waitingForPublication;
    }

    state::PendingQuestTransition pending{};
    const auto prepared =
        state::prepare_completed_quest_transition(session.questCompletion.characterSoid, pending);
    if (prepared == state::QuestCompletionPreparation::retry) {
        return retry_or_drop(session);
    }
    if (prepared == state::QuestCompletionPreparation::noWork) {
        cancel_quest_completion(session);
        return QuestCompletionResult::noWork;
    }

    const auto transition = pending.transition;
    if (!state::commit_quest_transition(transition, pending)) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::warn,
                          "ev=quest_completion stage=transaction_commit result=fail "
                          "reason=commit_refused_or_failed site=%u source_item=%u",
                          transition.completionEffect,
                          transition.sourceItemIndex);
        return retry_or_drop(session);
    }

    core::log::write(core::log::Channel::server,
                     core::log::Level::debug,
                     "ev=quest_completion stage=transaction_commit result=ok");
    session.questCompletion.retryCount = 0;
    session.investmentRefreshArmed = true;
    if (session.queuez.family4Active) {
        session.accountResyncArmed = true;
    }
    bap::arm_account_resync_elsewhere(session);
    return QuestCompletionResult::advanced;
}

} // namespace sunrise::server::bap::encrypted::queuez
