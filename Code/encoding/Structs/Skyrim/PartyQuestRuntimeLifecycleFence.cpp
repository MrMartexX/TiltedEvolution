#include <Structs/Skyrim/PartyQuestRuntimeLifecycleFence.h>

PartyQuestRuntimeLifecycleFenceResult PartyQuestRuntimeLifecycleFence::Prepare(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    PartyQuestRuntimeLifecycleFenceResult result;
    result.Event = aEvent;

    const auto& session = aGuardedSession.GetRuntimeSession();
    const auto& coordinator = session.GetCoordinator();
    const auto& saveGuard = aGuardedSession.GetSaveGuard();

    if (coordinator.IsRecoveryBlocked())
    {
        const auto* recovery = coordinator.GetRecoveryRecord();
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::RecoveryBlocked;
        result.TransactionId = recovery ? recovery->TransactionId : 0;
        result.GuardHeld = saveGuard.IsActive();
        return result;
    }

    const auto* active = coordinator.GetActive();
    if (!active)
    {
        if (saveGuard.IsActive())
        {
            result.Status = PartyQuestRuntimeLifecycleFenceStatus::GuardMismatch;
            result.TransactionId = saveGuard.GetTransactionId();
            result.GuardHeld = true;
            return result;
        }

        result.Status = PartyQuestRuntimeLifecycleFenceStatus::Allowed;
        return result;
    }

    result.TransactionId = active->TransactionId;
    result.GuardHeld = saveGuard.IsActive() &&
        saveGuard.GetTransactionId() == active->TransactionId;

    const auto aborted = aGuardedSession.AbortBeforeMutation(active->TransactionId);
    result.GuardHeld = aborted.GuardHeld;
    switch (aborted.Status)
    {
    case PartyQuestRuntimeGuardStatus::Ready:
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied;
        return result;

    case PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired:
        result.Status =
            PartyQuestRuntimeLifecycleFenceStatus::CheckpointRestoreRequired;
        return result;

    case PartyQuestRuntimeGuardStatus::PersistenceFailure:
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::PersistenceFailure;
        return result;

    case PartyQuestRuntimeGuardStatus::GuardMismatch:
    case PartyQuestRuntimeGuardStatus::GuardBusy:
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::GuardMismatch;
        return result;

    case PartyQuestRuntimeGuardStatus::GuardReleaseFailed:
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::GuardReleaseFailed;
        return result;

    case PartyQuestRuntimeGuardStatus::Deferred:
    case PartyQuestRuntimeGuardStatus::DuplicatePending:
    case PartyQuestRuntimeGuardStatus::DuplicateCommitted:
    case PartyQuestRuntimeGuardStatus::RecoveryBlocked:
    case PartyQuestRuntimeGuardStatus::ResourceLimitExceeded:
    case PartyQuestRuntimeGuardStatus::TransactionConflict:
    case PartyQuestRuntimeGuardStatus::InvalidRequest:
    case PartyQuestRuntimeGuardStatus::UnsafePlan:
    case PartyQuestRuntimeGuardStatus::InvalidState:
    case PartyQuestRuntimeGuardStatus::RecoveryFailed:
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::InvalidState;
        return result;
    }

    result.Status = PartyQuestRuntimeLifecycleFenceStatus::InvalidState;
    return result;
}
