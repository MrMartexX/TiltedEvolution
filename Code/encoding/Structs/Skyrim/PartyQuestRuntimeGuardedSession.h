#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCheckpoint.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <cstdint>

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
 * This is the control-plane boundary future Skyrim save hooks should query;
 * callers must not treat the runtime state's SaveGuardActive bit alone as an
 * actual save interception lease.
 */
class PartyQuestRuntimeGuardedSession final
{
public:
    PartyQuestRuntimeGuardedSession(
        PartyQuestRuntimeApplySession& aSession,
        PartyQuestSaveGuard& aSaveGuard) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult Begin(
        const PartyQuestRuntimeApplyRequest& acRequest) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult MarkWorldReady(
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestRuntimeCheckpointResult EnsurePreRepairCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acCheckpointPlan) noexcept;

    [[nodiscard]] PartyQuestRuntimeGuardResult ArmRuntimeMutation(
        uint64_t aTransactionId) noexcept;

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
     * Resolve a crash barrier through the exact PreRepair checkpoint. The guard
     * is acquired first and retained on every unresolved/failure path.
     */
    [[nodiscard]] PartyQuestRuntimeRecoveryResult ResolveCrashRecovery(
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    [[nodiscard]] bool CanSave(PartyQuestSaveKind aKind) const noexcept
    {
        return m_saveGuard.CanSave(aKind);
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
};
