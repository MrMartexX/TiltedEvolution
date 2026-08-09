#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

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
    InsufficientDiskSpace
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
 * Safety ordering for a fresh restore is:
 *
 *  1. persist Prepared journal;
 *  2. create and verify rollback copies of every existing live destination;
 *  3. persist BackupsReady;
 *  4. stage and verify all checkpoint bytes without changing live files;
 *  5. re-verify that live destinations still match the Prepared observations;
 *  6. persist MutationStarted;
 *  7. replace live files using same-directory staged renames;
 *  8. verify all restored targets and persist Restored;
 *  9. persist Committed.
 *
 * A crash observed at MutationStarted is never resumed forward blindly. Recover
 * first restores the pre-mutation bytes (or removes destinations that originally
 * did not exist), verifies that rollback, and terminates that restore attempt.
 * The caller may then build a fresh plan from current canonical state.
 */
class PartyQuestReplicaRestoreExecutor final
{
public:
    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport Execute(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId) noexcept;

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
        const std::filesystem::path& acJournalPath) noexcept;
};
