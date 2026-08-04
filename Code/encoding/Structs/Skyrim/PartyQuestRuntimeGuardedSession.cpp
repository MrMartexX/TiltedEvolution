#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <utility>

namespace
{
PartyQuestRuntimeGuardStatus TranslateBegin(
    PartyQuestRuntimeDurableBeginStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestRuntimeDurableBeginStatus::Started:
        return PartyQuestRuntimeGuardStatus::Ready;
    case PartyQuestRuntimeDurableBeginStatus::Deferred:
        return PartyQuestRuntimeGuardStatus::Deferred;
    case PartyQuestRuntimeDurableBeginStatus::DuplicatePending:
        return PartyQuestRuntimeGuardStatus::DuplicatePending;
    case PartyQuestRuntimeDurableBeginStatus::DuplicateCommitted:
        return PartyQuestRuntimeGuardStatus::DuplicateCommitted;
    case PartyQuestRuntimeDurableBeginStatus::TransactionConflict:
        return PartyQuestRuntimeGuardStatus::TransactionConflict;
    case PartyQuestRuntimeDurableBeginStatus::Busy:
        return PartyQuestRuntimeGuardStatus::InvalidState;
    case PartyQuestRuntimeDurableBeginStatus::RecoveryBlocked:
        return PartyQuestRuntimeGuardStatus::RecoveryBlocked;
    case PartyQuestRuntimeDurableBeginStatus::InvalidRequest:
        return PartyQuestRuntimeGuardStatus::InvalidRequest;
    case PartyQuestRuntimeDurableBeginStatus::UnsafePlan:
        return PartyQuestRuntimeGuardStatus::UnsafePlan;
    case PartyQuestRuntimeDurableBeginStatus::PersistenceFailure:
        return PartyQuestRuntimeGuardStatus::PersistenceFailure;
    }
    return PartyQuestRuntimeGuardStatus::InvalidState;
}

bool RequestDefersWorld(const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    return HasPartyQuestApplyAction(
        acRequest.Plan.Actions,
        PartyQuestApplyAction::WaitForWorldTargets);
}
} // namespace

PartyQuestRuntimeGuardedSession::PartyQuestRuntimeGuardedSession(
    PartyQuestRuntimeApplySession& aSession,
    PartyQuestSaveGuard& aSaveGuard) noexcept
    : m_session(aSession)
    , m_saveGuard(aSaveGuard)
{
}

bool PartyQuestRuntimeGuardedSession::HasGuard(uint64_t aTransactionId) const noexcept
{
    return aTransactionId != 0 &&
        m_saveGuard.IsActive() &&
        m_saveGuard.GetTransactionId() == aTransactionId;
}

PartyQuestRuntimeGuardStatus PartyQuestRuntimeGuardedSession::AcquireGuard(
    uint64_t aTransactionId,
    bool& aAcquiredHere) noexcept
{
    aAcquiredHere = false;
    switch (m_saveGuard.Acquire(aTransactionId))
    {
    case PartyQuestSaveGuardAcquireStatus::Acquired:
        aAcquiredHere = true;
        return PartyQuestRuntimeGuardStatus::Ready;
    case PartyQuestSaveGuardAcquireStatus::Duplicate:
        return PartyQuestRuntimeGuardStatus::Ready;
    case PartyQuestSaveGuardAcquireStatus::Busy:
        return PartyQuestRuntimeGuardStatus::GuardBusy;
    case PartyQuestSaveGuardAcquireStatus::InvalidTransaction:
        return PartyQuestRuntimeGuardStatus::InvalidRequest;
    }
    return PartyQuestRuntimeGuardStatus::InvalidState;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::Transition(
    uint64_t aTransactionId,
    PartyQuestRuntimeDurableTransitionStatus aStatus) noexcept
{
    PartyQuestRuntimeGuardResult result;
    result.TransactionId = aTransactionId;
    result.TransitionStatus = aStatus;
    result.GuardHeld = HasGuard(aTransactionId);
    switch (aStatus)
    {
    case PartyQuestRuntimeDurableTransitionStatus::Applied:
        result.Status = PartyQuestRuntimeGuardStatus::Ready;
        break;
    case PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired:
        result.Status = PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired;
        break;
    case PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure:
        result.Status = PartyQuestRuntimeGuardStatus::PersistenceFailure;
        break;
    case PartyQuestRuntimeDurableTransitionStatus::InvalidState:
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        break;
    }
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::Begin(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    PartyQuestRuntimeGuardResult result;
    result.TransactionId = acRequest.TransactionId;

    const bool deferred = RequestDefersWorld(acRequest);
    bool acquiredHere{};
    if (deferred)
    {
        if (m_saveGuard.IsActive())
        {
            result.Status = m_saveGuard.GetTransactionId() == acRequest.TransactionId
                ? PartyQuestRuntimeGuardStatus::GuardMismatch
                : PartyQuestRuntimeGuardStatus::GuardBusy;
            result.GuardHeld = HasGuard(acRequest.TransactionId);
            return result;
        }
    }
    else
    {
        const auto guardStatus = AcquireGuard(acRequest.TransactionId, acquiredHere);
        if (guardStatus != PartyQuestRuntimeGuardStatus::Ready)
        {
            result.Status = guardStatus;
            return result;
        }
    }

    result.BeginStatus = m_session.Begin(acRequest);
    result.Status = TranslateBegin(result.BeginStatus);

    const bool shouldKeepGuard =
        result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::Started ||
        (result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::DuplicatePending &&
         m_session.GetCoordinator().GetActive() &&
         m_session.GetCoordinator().GetActive()->SaveGuardActive);

    if (!deferred && !shouldKeepGuard && acquiredHere)
        m_saveGuard.Release(acRequest.TransactionId);

    if (result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::Started)
    {
        const auto* active = m_session.GetCoordinator().GetActive();
        if (!active || !active->SaveGuardActive || !HasGuard(acRequest.TransactionId))
            result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
    }
    else if (result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::Deferred)
    {
        const auto* active = m_session.GetCoordinator().GetActive();
        if (!active || active->SaveGuardActive || m_saveGuard.IsActive())
            result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
    }

    result.GuardHeld = HasGuard(acRequest.TransactionId);
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::MarkWorldReady(
    uint64_t aTransactionId) noexcept
{
    PartyQuestRuntimeGuardResult result;
    result.TransactionId = aTransactionId;

    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->TransactionId != aTransactionId ||
        active->State != PartyQuestRuntimeApplyState::DeferredWorld ||
        active->SaveGuardActive)
    {
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        return result;
    }

    bool acquiredHere{};
    const auto guardStatus = AcquireGuard(aTransactionId, acquiredHere);
    if (guardStatus != PartyQuestRuntimeGuardStatus::Ready)
    {
        result.Status = guardStatus;
        return result;
    }

    const auto transition = m_session.MarkWorldReady(aTransactionId);
    result = Transition(aTransactionId, transition);
    if (transition != PartyQuestRuntimeDurableTransitionStatus::Applied && acquiredHere)
    {
        m_saveGuard.Release(aTransactionId);
        result.GuardHeld = HasGuard(aTransactionId);
    }
    return result;
}

PartyQuestRuntimeCheckpointResult
PartyQuestRuntimeGuardedSession::EnsurePreRepairCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acCheckpointPlan) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active || !active->SaveGuardActive || !HasGuard(active->TransactionId))
    {
        PartyQuestRuntimeCheckpointResult result;
        result.Status = PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState;
        if (active)
        {
            result.TransactionId = active->TransactionId;
            result.TargetWorldRevision = active->TargetWorldRevision;
        }
        return result;
    }

    return PartyQuestRuntimeCheckpointCoordinator::EnsurePreRepairCheckpoint(
        m_session,
        acPaths,
        acCheckpointPlan);
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::ArmRuntimeMutation(
    uint64_t aTransactionId) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->TransactionId != aTransactionId ||
        !active->SaveGuardActive ||
        !HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }
    return Transition(aTransactionId, m_session.ArmRuntimeMutation(aTransactionId));
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::MarkPapyrusQuiescent(
    uint64_t aTransactionId) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->TransactionId != aTransactionId ||
        !active->SaveGuardActive ||
        !HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }
    return Transition(aTransactionId, m_session.MarkPapyrusQuiescent(aTransactionId));
}

PartyQuestRuntimeDurableVerificationResult PartyQuestRuntimeGuardedSession::SubmitResnapshot(
    uint64_t aTransactionId,
    QuestSnapshot aObservedSnapshot) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->TransactionId != aTransactionId ||
        !active->SaveGuardActive ||
        !HasGuard(aTransactionId))
    {
        return {PartyQuestRuntimeVerificationStatus::InvalidState, false};
    }
    return m_session.SubmitResnapshot(aTransactionId, std::move(aObservedSnapshot));
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::Commit(
    uint64_t aTransactionId) noexcept
{
    if (!HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        return result;
    }

    auto result = Transition(aTransactionId, m_session.Commit(aTransactionId));
    if (result.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied)
    {
        if (!m_saveGuard.Release(aTransactionId))
            result.Status = PartyQuestRuntimeGuardStatus::GuardReleaseFailed;
        result.GuardHeld = HasGuard(aTransactionId);
    }
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::AbortBeforeMutation(
    uint64_t aTransactionId) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active || active->TransactionId != aTransactionId)
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.TransactionId = aTransactionId;
        return result;
    }

    if (active->SaveGuardActive && !HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        return result;
    }
    if (!active->SaveGuardActive && m_saveGuard.IsActive())
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }

    const bool hadGuard = active->SaveGuardActive;
    auto result = Transition(aTransactionId, m_session.AbortBeforeMutation(aTransactionId));
    if (result.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied && hadGuard)
    {
        if (!m_saveGuard.Release(aTransactionId))
            result.Status = PartyQuestRuntimeGuardStatus::GuardReleaseFailed;
        result.GuardHeld = HasGuard(aTransactionId);
    }
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::CompleteLiveCheckpointRestore(
    uint64_t aTransactionId) noexcept
{
    if (!HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        return result;
    }

    auto result = Transition(
        aTransactionId,
        m_session.CompleteLiveCheckpointRestore(aTransactionId));
    if (result.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied)
    {
        if (!m_saveGuard.Release(aTransactionId))
            result.Status = PartyQuestRuntimeGuardStatus::GuardReleaseFailed;
        result.GuardHeld = HasGuard(aTransactionId);
    }
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::ReconcileLoadedState() noexcept
{
    PartyQuestRuntimeGuardResult result;
    const auto& coordinator = m_session.GetCoordinator();
    const PartyQuestRuntimeApplyEntry* required = nullptr;

    if (coordinator.IsRecoveryBlocked())
        required = coordinator.GetRecoveryRecord();
    else if (coordinator.GetActive() && coordinator.GetActive()->SaveGuardActive)
        required = coordinator.GetActive();

    if (!required)
    {
        if (m_saveGuard.IsActive())
        {
            result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
            result.GuardHeld = true;
            result.TransactionId = m_saveGuard.GetTransactionId();
            return result;
        }
        result.Status = PartyQuestRuntimeGuardStatus::Ready;
        return result;
    }

    result.TransactionId = required->TransactionId;
    bool acquiredHere{};
    result.Status = AcquireGuard(required->TransactionId, acquiredHere);
    result.GuardHeld = HasGuard(required->TransactionId);
    return result;
}

PartyQuestRuntimeRecoveryResult PartyQuestRuntimeGuardedSession::ResolveCrashRecovery(
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    const auto* recovery = m_session.GetCoordinator().GetRecoveryRecord();
    if (!m_session.GetCoordinator().IsRecoveryBlocked() || !recovery)
    {
        PartyQuestRuntimeRecoveryResult result;
        result.Status = PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState;
        return result;
    }

    const uint64_t transactionId = recovery->TransactionId;
    const uint64_t targetWorldRevision = recovery->TargetWorldRevision;
    bool acquiredHere{};
    const auto guardStatus = AcquireGuard(transactionId, acquiredHere);
    if (guardStatus != PartyQuestRuntimeGuardStatus::Ready)
    {
        PartyQuestRuntimeRecoveryResult result;
        result.Status = PartyQuestRuntimeRecoveryStatus::SaveGuardBusy;
        result.TransactionId = transactionId;
        result.TargetWorldRevision = targetWorldRevision;
        result.RestoreId = transactionId;
        return result;
    }

    auto result = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        m_session,
        acPaths);
    if (result.IsResolved())
    {
        if (!m_saveGuard.Release(transactionId))
            result.Status = PartyQuestRuntimeRecoveryStatus::SaveGuardReleaseFailed;
    }
    // Any unresolved recovery deliberately keeps the lease, including when this
    // call acquired it, because runtime mutation may already have happened.
    return result;
}
