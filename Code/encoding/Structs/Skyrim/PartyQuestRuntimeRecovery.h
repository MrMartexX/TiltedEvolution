#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <cstdint>
#include <filesystem>

class PartyQuestRuntimeGuardedSession;
class PartyQuestRuntimeRecoveryCoordinatorTestAccess;

enum class PartyQuestRuntimeRecoveryStatus : uint8_t
{
    Restored,
    AlreadyRestored,
    RollbackRecoveredRetryRequired,
    InvalidIdentity,
    InvalidLayout,
    InvalidRecoveryState,
    SaveGuardBusy,
    SaveGuardReleaseFailed,
    CheckpointMissing,
    CheckpointManifestRecoveryRequired,
    CheckpointManifestInvalid,
    CheckpointVerificationFailed,
    RestorePlanInvalid,
    RestoreJournalConflict,
    RestoreFailed,
    RuntimeStatePersistenceFailed
};

struct PartyQuestRuntimeRecoveryResult
{
    PartyQuestRuntimeRecoveryStatus Status{
        PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState};
    PartyQuestReplicaManifestPersistenceStatus ManifestStatus{
        PartyQuestReplicaManifestPersistenceStatus::InvalidData};
    PartyQuestReplicaManifestVerificationStatus VerificationStatus{
        PartyQuestReplicaManifestVerificationStatus::InvalidManifest};
    PartyQuestReplicaRestorePlanStatus RestorePlanStatus{
        PartyQuestReplicaRestorePlanStatus::InvalidIdentity};
    PartyQuestReplicaRestoreExecutionStatus RestoreStatus{
        PartyQuestReplicaRestoreExecutionStatus::InvalidPlan};
    PartyQuestRuntimeDurableTransitionStatus RuntimeTransition{
        PartyQuestRuntimeDurableTransitionStatus::InvalidState};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t RestoreId{}; // Always equals TransactionId for a valid exact recovery.
    std::filesystem::path ManifestPath;
    std::filesystem::path RestoreJournalPath;

    [[nodiscard]] bool IsResolved() const noexcept
    {
        return Status == PartyQuestRuntimeRecoveryStatus::Restored ||
            Status == PartyQuestRuntimeRecoveryStatus::AlreadyRestored;
    }
};

/**
 * Coordinates exact recovery after a runtime quest mutation may have occurred.
 *
 * This layer intentionally supports only the exact immutable
 * PreRepair/Revision_<TargetWorldRevision> checkpoint recorded by the runtime
 * transaction. It does not guess a LastKnownGood fallback.
 *
 * RestoreId is deterministically the runtime TransactionId. A caller cannot
 * accidentally fork one quest transaction into multiple filesystem restore
 * journals by choosing a different id on retry.
 *
 * Ordering is fail-closed:
 *
 *  1. require the exact campaign/player-bound runtime recovery record;
 *  2. load and verify the exact PreRepair revision manifest and bytes;
 *  3. build a confined restore plan;
 *  4. prove an exact kernel-backed workspace lease/capability;
 *  5. resume the transaction-id-bound durable restore journal when it exists,
 *     otherwise start a new crash-resumable restore;
 *  6. independently reverify the live replica against the exact restore plan;
 *  7. clear the runtime barrier only when checkpoint bytes are proven present
 *     in the live co-op replica at that instant;
 *  8. persist that cleared runtime state before exposing recovery as resolved.
 *
 * ResolveCrashRecovery() consumes a persisted crash barrier. ResolveLiveRecovery()
 * consumes the still-active post-mutation transaction after a live fail-closed
 * condition such as a terminal Papyrus monitor outcome. Both use the same exact
 * PreRepair revision and deterministic RestoreId contract.
 *
 * SaveGuard and the replica workspace lease protect different boundaries and
 * neither substitutes for the other. PartyQuestRuntimeGuardedSession proves or
 * acquires the physical process SaveGuard before entering this layer. For the
 * filesystem restore, a RuntimeSessionOwner-bound session reuses its exact
 * session-bound workspace publication capability; a standalone guarded session
 * falls back to the public restore executor, which acquires its own exact OS
 * workspace lease. This avoids recursive acquisition while still making every
 * journal/staging/live-replica mutation lease-bound.
 *
 * The coordinator entry points remain private because direct use could let a
 * logical SaveGuardActive field substitute for the physical process save lease.
 * Production access is only through PartyQuestRuntimeGuardedSession. A named
 * test friend exists solely for isolated filesystem recovery tests.
 *
 * A recovered rollback is deliberately not success: the old replica bytes are
 * safe again, but the requested checkpoint still has to be restored by a later
 * call. No Skyrim/Papyrus/save hook is invoked here.
 */
class PartyQuestRuntimeRecoveryCoordinator final
{
private:
    [[nodiscard]] static PartyQuestRuntimeRecoveryResult ResolveCrashRecovery(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    [[nodiscard]] static PartyQuestRuntimeRecoveryResult ResolveLiveRecovery(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    friend class PartyQuestRuntimeGuardedSession;
    // Defined only in Code/tests; isolated filesystem recovery tests use it
    // without expanding the production authority surface.
    friend class PartyQuestRuntimeRecoveryCoordinatorTestAccess;
};
