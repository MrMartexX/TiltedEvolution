#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

enum class PartyQuestReplicaDurableRestoreStatus : uint8_t
{
    Success,
    AlreadyCommitted,
    RecoveredRollback,
    RecoveredCommit,
    ResumeBeforeMutation,
    UnsupportedPlatform,
    JournalNotFound,
    JournalLoadFailed,
    InvalidIdentity,
    InvalidPhase,
    UnsafePath,
    WorkspaceBusy,
    WorkspaceLeaseFailure,
    CheckpointDurabilityUnavailable,
    CheckpointPlanMismatch,
    BackupVerificationFailed,
    DestinationChanged,
    CheckpointSourceChanged,
    StagingFailed,
    JournalPersistenceFailed,
    ReplacementFailed,
    RestoredVerificationFailed,
    CommittedVerificationFailed,
    RollbackFailed,
    CleanupFailed,
    FaultInjected
};

enum class PartyQuestReplicaDurableRestoreBoundary : uint8_t
{
    MutationStartedDurable,
    RestoredFilePublished,
    RestoredDurable,
    CommittedDurable,
    OriginalStateRestored
};

enum class PartyQuestReplicaDurableRestoreDirective : uint8_t
{
    Continue,
    FailClosed
};

/**
 * Ephemeral deterministic fault observer. It grants no path, journal, checkpoint
 * or mutation authority and is never serialized.
 */
struct PartyQuestReplicaDurableRestoreHooks
{
    using Callback = PartyQuestReplicaDurableRestoreDirective (*)(
        PartyQuestReplicaDurableRestoreBoundary,
        size_t,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestReplicaDurableRestoreDirective Invoke(
        PartyQuestReplicaDurableRestoreBoundary aBoundary,
        size_t aOperation = 0) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, aOperation, Context)
            : PartyQuestReplicaDurableRestoreDirective::Continue;
    }
};

struct PartyQuestReplicaDurableRestoreReport
{
    PartyQuestReplicaDurableRestoreStatus Status{
        PartyQuestReplicaDurableRestoreStatus::JournalLoadFailed};
    std::optional<PartyQuestReplicaRestoreJournalPhase> Phase;
    std::filesystem::path JournalPath;
    size_t CompletedOperations{};
    size_t FailedOperation{};
    std::filesystem::path FailedPath;
    bool RollbackPerformed{};
    bool CleanupPending{};
    bool RequiresRecovery{};

    [[nodiscard]] bool IsCheckpointRestored() const noexcept
    {
        return Status == PartyQuestReplicaDurableRestoreStatus::Success ||
            Status == PartyQuestReplicaDurableRestoreStatus::AlreadyCommitted ||
            Status == PartyQuestReplicaDurableRestoreStatus::RecoveredCommit;
    }

    [[nodiscard]] bool IsRecoveryHandled() const noexcept
    {
        return IsCheckpointRestored() ||
            Status == PartyQuestReplicaDurableRestoreStatus::RecoveredRollback ||
            Status == PartyQuestReplicaDurableRestoreStatus::ResumeBeforeMutation;
    }
};

/**
 * Linux/POSIX power-loss durable continuation for an already prepared restore.
 *
 * Continue() accepts only an exact strong BackupsReady journal produced by
 * PartyQuestReplicaDurableRestorePreparation. Before crossing the destructive
 * barrier it reacquires the exact workspace lease, re-promotes/rebinds the
 * immutable revision checkpoint, verifies rollback evidence and live
 * destinations, durably stages every requested checkpoint file, then durably
 * publishes MutationStarted. Only after that journal barrier may same-directory
 * durable rename replace protected co-op replica files.
 *
 * Ordering after the barrier is:
 *
 *   MutationStarted durable
 *   -> each staged destination durably published and verified
 *   -> all restored targets durably reverified
 *   -> Restored durable
 *   -> Committed durable
 *   -> committed transaction compaction.
 *
 * Committed compaction durably removes large rollback/staging evidence and
 * obsolete journal .tmp/.bak files, but deliberately retains the Committed
 * primary journal and transaction directory as a small terminal tombstone. This
 * preserves RestoreId idempotency instead of making a completed transaction id
 * reusable merely because cleanup ran.
 *
 * Recover() never crosses a mutation barrier that was not already durable:
 * Prepared/BackupsReady return ResumeBeforeMutation; MutationStarted restores
 * the exact pre-mutation replica bytes; Restored verifies then commits;
 * Committed verifies then resumes safe compaction. Rollback keeps the
 * MutationStarted journal and rollback evidence because the current durable
 * schema has no terminal RolledBack phase; repeated recovery is therefore
 * intentionally idempotent and conservative.
 *
 * Windows fails closed before filesystem mutation. This class never calls
 * Skyrim, Papyrus, TESQuest or native world mutation APIs and grants no such
 * authority.
 */
class PartyQuestReplicaDurableRestoreExecutor final
{
public:
    [[nodiscard]] static PartyQuestReplicaDurableRestoreReport Continue(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        PartyQuestReplicaDurableRestoreHooks aHooks = {}) noexcept;

    [[nodiscard]] static PartyQuestReplicaDurableRestoreReport Recover(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        PartyQuestReplicaDurableRestoreHooks aHooks = {}) noexcept;
};
