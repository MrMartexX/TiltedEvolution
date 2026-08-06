#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointCaptureEpoch.h>
#include <Structs/Skyrim/PartyQuestRuntimeCheckpoint.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <cstdint>
#include <utility>

enum class PartyQuestRuntimeGuardStatus : uint8_t
{
    Ready,
    Deferred,
    DuplicatePending,
    DuplicateCommitted,
    GuardBusy,
    GuardMismatch,
    RecoveryBlocked,
    CheckpointRestoreRequired,
    PersistenceFailure,
    TransactionConflict,
    InvalidRequest,
    UnsafePlan,
    InvalidState,
    GuardReleaseFailed,
    RecoveryFailed
};

struct PartyQuestRuntimeGuardResult
{
    PartyQuestRuntimeGuardStatus Status{PartyQuestRuntimeGuardStatus::InvalidState};
    PartyQuestRuntimeDurableBeginStatus BeginStatus{PartyQuestRuntimeDurableBeginStatus::InvalidRequest};
    PartyQuestRuntimeDurableTransitionStatus TransitionStatus{PartyQuestRuntimeDurableTransitionStatus::InvalidState};
    uint64_t TransactionId{};
    bool GuardHeld{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestRuntimeGuardStatus::Ready ||
            Status == PartyQuestRuntimeGuardStatus::Deferred ||
            Status == PartyQuestRuntimeGuardStatus::DuplicatePending ||
            Status == PartyQuestRuntimeGuardStatus::DuplicateCommitted;
    }
};

/**
 * Couples the durable runtime-apply state machine to the concrete save-guard
 * lease without calling Skyrim save APIs itself.
 *
 * The ordering closes the dangerous gap between the persisted logical
 * SaveGuardActive bit and the actual guard used by future save interception:
 *
 * - immediate repair: acquire physical guard before durable Begin();
 * - deferred repair: hold no guard while waiting for world targets;
 * - world ready: acquire physical guard before durable MarkWorldReady();
 * - persistence failure before publication: release only a lease acquired by
 *   that failed call;
 * - commit/safe abort: persist the state transition first, then release guard;
 * - checkpoint restore required: retain/acquire the guard until physical
 *   recovery and durable barrier clearance both succeed.
 *
 * Production runtime integration should use the one-argument constructor so
 * the session is coupled to PartyQuestSaveGuard::GetProcessGuard(), which is
 * the same guard observed by the Skyrim BGSSaveLoadManager hook. The explicit
 * two-argument constructor remains available for isolated unit tests.
 */
class PartyQuestRuntimeGuardedSession final
{
public:
    explicit PartyQuestRuntimeGuardedSession(
        PartyQuestRuntimeApplySession& aSession) noexcept
        : PartyQuestRuntimeGuardedSession(
              aSession,
              PartyQuestSaveGuard::GetProcessGuard())
    {
    }

    PartyQuestRuntimeGuardedSession(
        PartyQuestRuntimeApplySession& aSession,
        PartyQuestSaveGuard& aSaveGuard) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult Begin(
        const PartyQuestRuntimeApplyRequest& acRequest) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult MarkWorldReady(
        uint64_t aTransactionId) noexcept;

    /**
     * Begin one process-local logical checkpoint capture epoch for the exact
     * AwaitingCheckpoint transaction/revision/sidecar contract. Overlapping
     * epochs are rejected; callers must complete or explicitly abort the active
     * epoch before recapturing.
     */
    [[nodiscard]] PartyQuestCheckpointCaptureEpochResult BeginCheckpointCaptureEpoch() noexcept;

    [[nodiscard]] bool IsCheckpointCaptureEpochActive(
        const PartyQuestCheckpointCaptureEpoch& acEpoch) const noexcept;

    /**
     * Release an epoch only after the durable checkpoint transition reached
     * ReadyToApply. Runtime mutation remains blocked while any epoch is active.
     */
    [[nodiscard]] bool CompleteCheckpointCaptureEpoch(
        const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept;

    /** Safe recapture path while no checkpoint/mutation has been published. */
    [[nodiscard]] bool AbortCheckpointCaptureEpoch(
        const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept;

    /** Low-level publication gate; callers need assembler-issued coverage. */
    [[nodiscard]] PartyQuestRuntimeCheckpointResult EnsurePreRepairCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acCheckpointPlan,
        const PartyQuestRuntimeCheckpointCoverageAuthorization& acCoverage) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult ArmRuntimeMutation(
        uint64_t aTransactionId) noexcept;

    /**
     * Poll the authoritative Papyrus runtime monitor under the real process
     * SaveGuard. Waiting/Quiescent are non-terminal and keep the guarded repair
     * active. TimedOut/Unsupported/InvalidClock/InvalidObservation after the
     * durable mutation barrier are mapped immediately to
     * CheckpointRestoreRequired; the active transaction and physical guard are
     * intentionally retained until ResolveLiveRecovery() proves the exact
     * PreRepair bytes restored.
     *
     * InvalidTransaction/Inactive are caller/control-plane errors and do not
     * themselves authorize a filesystem restore.
     */
    [[nodiscard]] PartyQuestRuntimeGuardResult PollPapyrusRuntime(
        PartyQuestPapyrusRuntimeMonitor& aMonitor,
        uint64_t aTransactionId,
        uint64_t aNowMs) noexcept
    {
        PartyQuestRuntimeGuardResult result;
        result.TransactionId = aTransactionId;
        result.GuardHeld = HasGuard(aTransactionId);

        const auto* active = m_session.GetCoordinator().GetActive();
        auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
        if (&m_saveGuard != &processGuard ||
            !active ||
            aTransactionId == 0 ||
            active->TransactionId != aTransactionId ||
            active->State != PartyQuestRuntimeApplyState::WaitingForPapyrus ||
            !active->SaveGuardActive ||
            !active->CheckpointCreated ||
            !active->RuntimeMutationMayHaveOccurred ||
            !HasGuard(aTransactionId) ||
            !aMonitor.IsAuthoritativeSession() ||
            aMonitor.GetTransactionId() != aTransactionId)
        {
            result.Status = &m_saveGuard == &processGuard
                ? PartyQuestRuntimeGuardStatus::InvalidState
                : PartyQuestRuntimeGuardStatus::GuardMismatch;
            return result;
        }

        const auto monitorStatus = aMonitor.Poll(aTransactionId, aNowMs);
        switch (monitorStatus)
        {
        case PartyQuestPapyrusRuntimeMonitorStatus::Waiting:
        case PartyQuestPapyrusRuntimeMonitorStatus::Quiescent:
            result.Status = PartyQuestRuntimeGuardStatus::Ready;
            result.GuardHeld = true;
            return result;

        case PartyQuestPapyrusRuntimeMonitorStatus::TimedOut:
        case PartyQuestPapyrusRuntimeMonitorStatus::Unsupported:
        case PartyQuestPapyrusRuntimeMonitorStatus::InvalidClock:
        case PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation:
            return Transition(
                aTransactionId,
                m_session.AbortBeforeMutation(aTransactionId));

        case PartyQuestPapyrusRuntimeMonitorStatus::Inactive:
        case PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction:
            result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
            result.GuardHeld = HasGuard(aTransactionId);
            return result;
        }

        result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
        result.GuardHeld = HasGuard(aTransactionId);
        return result;
    }

    /**
     * Production quiescence gate. Only the process SaveGuard may cross into the
     * authoritative monitor path; the monitor itself must then consume trusted
     * observer-backed one-shot evidence before the durable transition occurs.
     */
    [[nodiscard]] PartyQuestRuntimeGuardResult MarkPapyrusQuiescent(
        PartyQuestPapyrusRuntimeMonitor& aMonitor,
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept
    {
        const uint64_t transactionId = aAuthorization.GetTransactionId();
        const auto* active = m_session.GetCoordinator().GetActive();
        auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
        if (&m_saveGuard != &processGuard ||
            !active ||
            transactionId == 0 ||
            active->TransactionId != transactionId ||
            !active->SaveGuardActive ||
            !HasGuard(transactionId))
        {
            PartyQuestRuntimeGuardResult result;
            result.Status = &m_saveGuard == &processGuard
                ? PartyQuestRuntimeGuardStatus::GuardMismatch
                : PartyQuestRuntimeGuardStatus::InvalidState;
            result.TransactionId = transactionId;
            result.GuardHeld = HasGuard(transactionId);
            return result;
        }

        return Transition(
            transactionId,
            m_session.MarkPapyrusQuiescent(aMonitor, std::move(aAuthorization)));
    }

    /**
     * Diagnostic low-level quiescence surface. It is deliberately rejected when
     * this wrapper owns the real process SaveGuard, so manually observed tracker
     * state cannot advance a production repair into Verifying.
     */
    [[nodiscard]] PartyQuestRuntimeGuardResult MarkPapyrusQuiescent(
        PartyQuestPapyrusQuiescenceTracker& aTracker,
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept
    {
        const uint64_t transactionId = aAuthorization.GetTransactionId();
        if (&m_saveGuard == &PartyQuestSaveGuard::GetProcessGuard())
        {
            PartyQuestRuntimeGuardResult result;
            result.Status = PartyQuestRuntimeGuardStatus::InvalidState;
            result.TransactionId = transactionId;
            result.GuardHeld = HasGuard(transactionId);
            return result;
        }

        const auto* active = m_session.GetCoordinator().GetActive();
        if (!active ||
            transactionId == 0 ||
            active->TransactionId != transactionId ||
            !active->SaveGuardActive ||
            !HasGuard(transactionId))
        {
            PartyQuestRuntimeGuardResult result;
            result.Status = PartyQuestRuntimeGuardStatus::GuardMismatch;
            result.TransactionId = transactionId;
            result.GuardHeld = HasGuard(transactionId);
            return result;
        }

        return Transition(
            transactionId,
            m_session.MarkPapyrusQuiescent(aTracker, std::move(aAuthorization)));
    }

    /** Legacy compatibility surface: naked transaction assertions fail closed. */
    [[nodiscard]] PartyQuestRuntimeGuardResult MarkPapyrusQuiescent(
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestRuntimeDurableVerificationResult SubmitResnapshot(
        uint64_t aTransactionId,
        QuestSnapshot aObservedSnapshot) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult Commit(
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult AbortBeforeMutation(
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult CompleteLiveCheckpointRestore(
        uint64_t aTransactionId) noexcept;

    /** Reconstruct the physical lease required by a loaded durable session. */
    [[nodiscard]] PartyQuestRuntimeGuardResult ReconcileLoadedState() noexcept;

    /**
     * Resolve an in-process post-mutation failure through the exact PreRepair
     * checkpoint. Only the real process SaveGuard may perform this restore.
     * Every unresolved/failure path keeps the lease; success releases it only
     * after exact live-byte verification and durable runtime-state clearance.
     */
    [[nodiscard]] PartyQuestRuntimeRecoveryResult ResolveLiveRecovery(
        const PartyQuestCoopSavePaths& acPaths) noexcept
    {
        const auto* active = m_session.GetCoordinator().GetActive();
        auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
        if (&m_saveGuard != &processGuard ||
            !active ||
            active->TransactionId == 0 ||
            !active->SaveGuardActive ||
            !active->CheckpointCreated ||
            !active->RuntimeMutationMayHaveOccurred ||
            !HasGuard(active->TransactionId))
        {
            PartyQuestRuntimeRecoveryResult result;
            result.Status = &m_saveGuard == &processGuard
                ? PartyQuestRuntimeRecoveryStatus::SaveGuardBusy
                : PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState;
            if (active)
            {
                result.TransactionId = active->TransactionId;
                result.TargetWorldRevision = active->TargetWorldRevision;
                result.RestoreId = active->TransactionId;
            }
            return result;
        }

        const uint64_t transactionId = active->TransactionId;
        auto result = PartyQuestRuntimeRecoveryCoordinator::ResolveLiveRecovery(
            m_session,
            acPaths);
        if (result.IsResolved())
        {
            m_checkpointCaptureEpoch = {};
            if (!m_saveGuard.Release(transactionId))
                result.Status = PartyQuestRuntimeRecoveryStatus::SaveGuardReleaseFailed;
        }
        // Runtime mutation may have happened, so every unresolved path retains
        // the physical save lease for a deterministic retry/recovery attempt.
        return result;
    }

    /**
     * Resolve a crash barrier through the exact PreRepair checkpoint. The guard
     * is acquired first and retained on every unresolved/failure path.
     */
    [[nodiscard]] PartyQuestRuntimeRecoveryResult ResolveCrashRecovery(
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    [[nodiscard]] bool CanSave(PartyQuestSaveKind aKind) const noexcept
    {
        return m_saveGuard.CanSave(aKind);
    }

    /** Read-only ownership bridge for Skyrim-specific checkpoint code. */
    [[nodiscard]] const PartyQuestRuntimeApplySession& GetRuntimeSession() noexcept
    {
        return m_session;
    }

    [[nodiscard]] const PartyQuestRuntimeApplySession& GetRuntimeSession() const noexcept
    {
        return m_session;
    }

    /**
     * Exposes the exact physical guard owned by this control-plane session so a
     * Skyrim-specific controlled checkpoint cannot accidentally authorize a
     * different process-local lease.
     */
    [[nodiscard]] PartyQuestSaveGuard& GetSaveGuard() noexcept
    {
        return m_saveGuard;
    }

    [[nodiscard]] const PartyQuestSaveGuard& GetSaveGuard() const noexcept
    {
        return m_saveGuard;
    }

private:
    [[nodiscard]] bool HasGuard(uint64_t aTransactionId) const noexcept;
    [[nodiscard]] PartyQuestRuntimeGuardStatus AcquireGuard(
        uint64_t aTransactionId,
        bool& aAcquiredHere) noexcept;
    [[nodiscard]] PartyQuestRuntimeGuardResult Transition(
        uint64_t aTransactionId,
        PartyQuestRuntimeDurableTransitionStatus aStatus) noexcept;

    PartyQuestRuntimeApplySession& m_session;
    PartyQuestSaveGuard& m_saveGuard;
    PartyQuestCheckpointCaptureEpoch m_checkpointCaptureEpoch;
};
