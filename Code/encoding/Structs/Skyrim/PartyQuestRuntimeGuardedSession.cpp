#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <utility>

namespace
{
constexpr uint64_t kCheckpointCaptureBudgetNanoseconds = 30ull * 1000ull * 1000ull * 1000ull;

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
    case PartyQuestRuntimeDurableBeginStatus::ResourceLimitExceeded:
        return PartyQuestRuntimeGuardStatus::ResourceLimitExceeded;
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

uint64_t NextCheckpointCaptureEpochId() noexcept
{
    static std::atomic<uint64_t> sequence{1};
    const uint64_t counter = sequence.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    uint64_t value = now ^ (counter * 0x9E3779B97F4A7C15ull);
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value != 0 ? value : counter;
}

uint64_t NextCheckpointCaptureDeadline() noexcept
{
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now <= 0)
        return 0;
    const auto ticks = static_cast<uint64_t>(now);
    if (ticks > std::numeric_limits<uint64_t>::max() -
        kCheckpointCaptureBudgetNanoseconds)
    {
        return std::numeric_limits<uint64_t>::max();
    }
    return ticks + kCheckpointCaptureBudgetNanoseconds;
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

    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    if (&m_saveGuard == &processGuard)
    {
        auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        if (!processOwner.IsBound() ||
            processOwner.GetGuardedSession() != this)
        {
            // INV-LIFECYCLE-001: the physical process SaveGuard is critical
            // repair ownership. A private session that the process lifecycle
            // owner cannot fence must never acquire or inherit that guard.
            return PartyQuestRuntimeGuardStatus::InvalidState;
        }
    }

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
    case PartyQuestRuntimeDurableTransitionStatus::InsufficientDurability:
        result.Status = PartyQuestRuntimeGuardStatus::InsufficientDurability;
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
    const PartyQuestRuntimeApplyRequest& acCurrentRequest) noexcept
{
    if (&m_saveGuard == &PartyQuestSaveGuard::GetProcessGuard())
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.TransactionId = acCurrentRequest.TransactionId;
        result.GuardHeld = HasGuard(acCurrentRequest.TransactionId);
        return result;
    }

    return MarkWorldReadyPinned(acCurrentRequest);
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::MarkWorldReadyPinned(
    const PartyQuestRuntimeApplyRequest& acCurrentRequest) noexcept
{
    const uint64_t transactionId = acCurrentRequest.TransactionId;
    PartyQuestRuntimeGuardResult result;
    result.TransactionId = transactionId;

    const auto* active = m_session.GetCoordinator().GetActive();
    if (m_checkpointCaptureEpoch.IsVerified() ||
        !active ||
        active->TransactionId != transactionId ||
        active->State != PartyQuestRuntimeApplyState::DeferredWorld ||
        active->SaveGuardActive)
    {
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        return result;
    }

    PartyQuestRuntimeApplyCoordinator validation = m_session.GetCoordinator();
    if (!validation.MarkWorldReady(acCurrentRequest))
    {
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        return result;
    }

    bool acquiredHere{};
    const auto guardStatus = AcquireGuard(transactionId, acquiredHere);
    if (guardStatus != PartyQuestRuntimeGuardStatus::Ready)
    {
        result.Status = guardStatus;
        return result;
    }

    const auto transition = m_session.MarkWorldReady(acCurrentRequest);
    result = Transition(transactionId, transition);
    if (transition != PartyQuestRuntimeDurableTransitionStatus::Applied && acquiredHere)
    {
        m_saveGuard.Release(transactionId);
        result.GuardHeld = HasGuard(transactionId);
    }
    return result;
}

PartyQuestCheckpointCaptureEpochResult
PartyQuestRuntimeGuardedSession::BeginCheckpointCaptureEpoch() noexcept
{
    PartyQuestCheckpointCaptureEpochResult result;
    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint ||
        !active->SaveGuardActive ||
        active->CheckpointCreated ||
        active->RuntimeMutationMayHaveOccurred ||
        active->TransactionId == 0 ||
        active->TargetWorldRevision == 0 ||
        active->SidecarManifestFingerprint == 0)
    {
        result.Status = PartyQuestCheckpointCaptureEpochStatus::InvalidRuntimeState;
        return result;
    }

    if (!HasGuard(active->TransactionId))
    {
        result.Status = PartyQuestCheckpointCaptureEpochStatus::GuardMismatch;
        return result;
    }

    if (m_checkpointCaptureEpoch.IsVerified())
    {
        if (!m_checkpointCaptureEpoch.IsExpired())
        {
            result.Status = PartyQuestCheckpointCaptureEpochStatus::AlreadyActive;
            return result;
        }
        m_checkpointCaptureEpoch = {};
    }

    const uint64_t epochId = NextCheckpointCaptureEpochId();
    const uint64_t deadlineTicks = NextCheckpointCaptureDeadline();
    m_checkpointCaptureEpoch = PartyQuestCheckpointCaptureEpoch(
        epochId,
        active->TransactionId,
        active->TargetWorldRevision,
        active->SidecarManifestFingerprint,
        deadlineTicks);
    if (!m_checkpointCaptureEpoch.IsVerified())
    {
        m_checkpointCaptureEpoch = {};
        result.Status = PartyQuestCheckpointCaptureEpochStatus::InvalidRuntimeState;
        return result;
    }

    result.Status = PartyQuestCheckpointCaptureEpochStatus::Ready;
    result.Epoch = m_checkpointCaptureEpoch;
    return result;
}

bool PartyQuestRuntimeGuardedSession::IsCheckpointCaptureEpochActive(
    const PartyQuestCheckpointCaptureEpoch& acEpoch) const noexcept
{
    if (!m_checkpointCaptureEpoch.IsVerified() ||
        !acEpoch.IsVerified() ||
        m_checkpointCaptureEpoch.m_epochId != acEpoch.m_epochId)
    {
        return false;
    }

    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        (active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint &&
         active->State != PartyQuestRuntimeApplyState::ReadyToApply) ||
        !active->SaveGuardActive ||
        active->RuntimeMutationMayHaveOccurred ||
        !HasGuard(active->TransactionId))
    {
        return false;
    }

    return acEpoch.MatchesContext(
        active->TransactionId,
        active->TargetWorldRevision,
        active->SidecarManifestFingerprint) &&
        m_checkpointCaptureEpoch.MatchesContext(
            active->TransactionId,
            active->TargetWorldRevision,
            active->SidecarManifestFingerprint);
}

bool PartyQuestRuntimeGuardedSession::CompleteCheckpointCaptureEpoch(
    const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept
{
    if (!IsCheckpointCaptureEpochActive(acEpoch))
        return false;

    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->State != PartyQuestRuntimeApplyState::ReadyToApply ||
        !active->CheckpointCreated ||
        active->RuntimeMutationMayHaveOccurred)
    {
        return false;
    }

    m_checkpointCaptureEpoch = {};
    return true;
}

bool PartyQuestRuntimeGuardedSession::AbortCheckpointCaptureEpoch(
    const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept
{
    if (!IsCheckpointCaptureEpochActive(acEpoch))
        return false;

    const auto* active = m_session.GetCoordinator().GetActive();
    if (!active ||
        active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint ||
        active->CheckpointCreated ||
        active->RuntimeMutationMayHaveOccurred)
    {
        return false;
    }

    m_checkpointCaptureEpoch = {};
    return true;
}

PartyQuestRuntimeCheckpointResult
PartyQuestRuntimeGuardedSession::EnsurePreRepairCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acCheckpointPlan,
    const PartyQuestRuntimeCheckpointCoverageAuthorization& acCoverage) noexcept
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
        acCheckpointPlan,
        acCoverage);
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::ArmRuntimeMutation(
    uint64_t aTransactionId) noexcept
{
    const auto* active = m_session.GetCoordinator().GetActive();
    if (m_checkpointCaptureEpoch.IsVerified() ||
        !active ||
        active->TransactionId != aTransactionId ||
        !active->SaveGuardActive ||
        !HasGuard(aTransactionId))
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = m_checkpointCaptureEpoch.IsVerified()
            ? PartyQuestRuntimeGuardStatus::InvalidState
            : PartyQuestRuntimeGuardStatus::GuardMismatch;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }
    return Transition(
        aTransactionId,
        m_session.ArmRuntimeMutationInternal(aTransactionId));
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
    // Production must use SubmitVerificationResnapshot() so every post-mutation
    // sample participates in the bounded verification window.
    if (&m_saveGuard == &PartyQuestSaveGuard::GetProcessGuard())
        return {PartyQuestRuntimeVerificationStatus::InvalidState, false};

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
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId,
    uint64_t aNowMs) noexcept
{
    PartyQuestRuntimeGuardResult result;
    result.TransactionId = aTransactionId;
    result.GuardHeld = HasGuard(aTransactionId);

    const auto* active = m_session.GetCoordinator().GetActive();
    if (&m_saveGuard != &PartyQuestSaveGuard::GetProcessGuard() ||
        !active ||
        aTransactionId == 0 ||
        active->TransactionId != aTransactionId ||
        active->State != PartyQuestRuntimeApplyState::ReadyToCommit ||
        !active->SaveGuardActive ||
        !active->CheckpointCreated ||
        !active->RuntimeMutationMayHaveOccurred ||
        !HasGuard(aTransactionId) ||
        aMonitor.GetTransactionId() != aTransactionId)
    {
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        return result;
    }

    const auto monitorStatus = aMonitor.Poll(aTransactionId, aNowMs);
    switch (monitorStatus)
    {
    case PartyQuestRuntimeVerificationMonitorStatus::Stable:
        result = Transition(aTransactionId, m_session.Commit(aTransactionId));
        if (result.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied)
        {
            aMonitor.Cancel(aTransactionId);
            m_checkpointCaptureEpoch = {};
            if (!m_saveGuard.Release(aTransactionId))
                result.Status = PartyQuestRuntimeGuardStatus::GuardReleaseFailed;
            result.GuardHeld = HasGuard(aTransactionId);
        }
        return result;

    case PartyQuestRuntimeVerificationMonitorStatus::TimedOut:
    case PartyQuestRuntimeVerificationMonitorStatus::DivergenceBudgetExceeded:
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidClock:
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification:
        return Transition(
            aTransactionId,
            m_session.AbortBeforeMutation(aTransactionId));

    case PartyQuestRuntimeVerificationMonitorStatus::Inactive:
    case PartyQuestRuntimeVerificationMonitorStatus::Waiting:
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidTransaction:
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }

    result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
    result.GuardHeld = HasGuard(aTransactionId);
    return result;
}

PartyQuestRuntimeGuardResult PartyQuestRuntimeGuardedSession::Commit(
    uint64_t aTransactionId) noexcept
{
    // Production commit must prove that Stable evidence is still inside the
    // original verification deadline at the instant of publication.
    if (&m_saveGuard == &PartyQuestSaveGuard::GetProcessGuard())
    {
        PartyQuestRuntimeGuardResult result;
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }

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
        m_checkpointCaptureEpoch = {};
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
    if (result.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied)
    {
        m_checkpointCaptureEpoch = {};
        if (hadGuard)
        {
            if (!m_saveGuard.Release(aTransactionId))
                result.Status = PartyQuestRuntimeGuardStatus::GuardReleaseFailed;
            result.GuardHeld = HasGuard(aTransactionId);
        }
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
        m_checkpointCaptureEpoch = {};
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

    if (m_checkpointCaptureEpoch.IsVerified())
    {
        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.GuardHeld = m_saveGuard.IsActive();
        result.TransactionId = m_checkpointCaptureEpoch.GetTransactionId();
        return result;
    }

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
        m_checkpointCaptureEpoch = {};
        if (!m_saveGuard.Release(transactionId))
            result.Status = PartyQuestRuntimeRecoveryStatus::SaveGuardReleaseFailed;
    }
    return result;
}
