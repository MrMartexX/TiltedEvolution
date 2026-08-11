#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

class PartyQuestRuntimeRecoveryCoordinator;
class PartyQuestReplicaRestoreExecutorTestAccess;

enum class PartyQuestReplicaRestoreExecutionStatus : uint8_t
{
    Success,
    AlreadyCommitted,
    RecoveredRollback,
    InvalidPlan,
    InvalidIdentity,
    RestoreIdConflict,
    JournalLoadFailed,
    BackupRecoveryRequired,
    JournalPersistenceFailed,
    UnsafePath,
    DestinationChanged,
    CheckpointSourceChanged,
    BackupCreationFailed,
    BackupVerificationFailed,
    StagingFailed,
    ReplacementFailed,
    RestoredVerificationFailed,
    RollbackFailed,
    ResourceLimitExceeded,
    InsufficientDiskSpace,
    OperationDeadlineExceeded,
    WorkspaceBusy,
    WorkspaceLeaseFailure
};

enum class PartyQuestReplicaRestoreExecutionBoundary : uint8_t
{
    OriginalMovedAside,
    RestoredFilePublished,
    OriginalStateRestored
};

enum class PartyQuestReplicaRestoreExecutionDirective : uint8_t
{
    Continue,
    FailClosed
};

/**
 * Ephemeral local execution hooks used by deterministic fault validation.
 * They are never serialized and grant no path or mutation authority. The
 * optional disk-space probe receives only the trusted player-root path chosen
 * by the executor and can only report available bytes. Production callers leave
 * it unset so std::filesystem::space remains authoritative.
 */
struct PartyQuestReplicaRestoreExecutionHooks
{
    using Callback = PartyQuestReplicaRestoreExecutionDirective (*)(
        PartyQuestReplicaRestoreExecutionBoundary,
        size_t,
        void*) noexcept;
    using Clock = uint64_t (*)(void*) noexcept;
    using DiskSpaceQuery = bool (*)(
        const std::filesystem::path&,
        uint64_t&,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};
    Clock MonotonicNow{};
    DiskSpaceQuery QueryAvailableBytes{};

    [[nodiscard]] PartyQuestReplicaRestoreExecutionDirective Invoke(
        PartyQuestReplicaRestoreExecutionBoundary aBoundary,
        size_t aOperation) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, aOperation, Context)
            : PartyQuestReplicaRestoreExecutionDirective::Continue;
    }

    [[nodiscard]] uint64_t NowTicks() const noexcept;
};

/** Local-only budget for a fresh crash-resumable restore before its journal exists. */
struct PartyQuestReplicaRestoreResourcePolicy
{
    [[nodiscard]] static std::optional<uint64_t> RequiredFreeBytes(
        const PartyQuestReplicaRestoreJournalState& acState) noexcept;

    [[nodiscard]] static bool HasSufficientDiskSpace(
        const PartyQuestReplicaRestoreJournalState& acState,
        uint64_t aAvailableBytes) noexcept;
};

struct PartyQuestReplicaRestoreExecutionReport
{
    PartyQuestReplicaRestoreExecutionStatus Status{
        PartyQuestReplicaRestoreExecutionStatus::InvalidPlan};
    PartyQuestReplicaRestoreJournalPhase Phase{
        PartyQuestReplicaRestoreJournalPhase::Prepared};
    size_t CompletedOperations{};
    size_t FailedOperation{};
    std::filesystem::path FailedPath;
    std::filesystem::path JournalPath;
    bool RollbackPerformed{};
    bool CleanupPending{};

    /** The requested checkpoint bytes are durably present in the live replica. */
    [[nodiscard]] bool IsCheckpointRestored() const noexcept
    {
        return Status == PartyQuestReplicaRestoreExecutionStatus::Success ||
            Status == PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted;
    }

    /**
     * The recovery action itself reached a safe terminal state. RecoveredRollback
     * is handled but deliberately is not a completed checkpoint restore.
     */
    [[nodiscard]] bool IsRecoveryHandled() const noexcept
    {
        return IsCheckpointRestored() ||
            Status == PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback;
    }

    /** Conventional success means the requested restore, not merely rollback, completed. */
    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return IsCheckpointRestored();
    }
};

/**
 * Crash-resumable executor for restoring an isolated co-op replica from an
 * already verified checkpoint plan.
 *
 * This executor is deliberately game-independent. It never calls Skyrim save,
 * load, TESQuest or Papyrus APIs and it is confined to the current player's
 * CoopCampaigns replica tree.
 *
 * Every public Execute/Recover call acquires an exact kernel-backed workspace
 * lease before it may publish a restore journal, staging file, rollback file or
 * live replica replacement. RuntimeSessionOwner already holds that lease for a
 * hydrated session, so the runtime recovery coordinator uses the private
 * capability-bearing entry points instead of recursively acquiring the same OS
 * lock. The capability is exact-root/campaign/player bound and pins the native
 * lease for the whole synchronous execution.
 *
 * Safety ordering for a fresh restore is:
 *
 *  1. prove the exact workspace lease/capability;
 *  2. persist Prepared journal;
 *  3. create and verify rollback copies of every existing live destination;
 *  4. persist BackupsReady;
 *  5. stage and verify all checkpoint bytes without changing live files;
 *  6. re-verify that live destinations still match the Prepared observations;
 *  7. persist MutationStarted;
 *  8. replace live files using same-directory staged renames;
 *  9. verify all restored targets and persist Restored;
 * 10. persist Committed.
 *
 * A crash observed at MutationStarted is never resumed forward blindly. Recover
 * first restores the pre-mutation bytes (or removes destinations that originally
 * did not exist), verifies that rollback, and terminates that restore attempt.
 * The caller may then build a fresh plan from current canonical state.
 * A local monotonic deadline is checked around synchronous filesystem
 * boundaries. Before MutationStarted, expiry leaves resumable journal state and
 * no live mutation. After that barrier, expiry takes the exact rollback path;
 * rollback itself is never interrupted by the deadline. Once Restored is
 * durable, expiry leaves that phase for re-verification by Recover. One blocking
 * OS call already in progress cannot be forcibly cancelled.
 */
class PartyQuestReplicaRestoreExecutor final
{
public:
    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport Execute(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept;

    /**
     * Recovers a durable restore journal.
     *
     * Prepared/BackupsReady work can continue because no live mutation was
     * allowed yet. MutationStarted is rolled back and terminated. Restored is
     * verified and committed. A stale .bak-only journal fails closed.
     */
    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport Recover(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept;

private:
    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport ExecuteAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport RecoverAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept;

    friend class PartyQuestRuntimeRecoveryCoordinator;
    // Defined only in Code/tests for exact capability rejection tests.
    friend class PartyQuestReplicaRestoreExecutorTestAccess;
};
