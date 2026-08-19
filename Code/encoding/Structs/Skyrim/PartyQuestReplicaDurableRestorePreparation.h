#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <cstddef>
#include <cstdint>
#include <optional>

enum class PartyQuestReplicaDurableRestorePreparationStatus : uint8_t
{
    BackupsReady,
    UnsupportedPlatform,
    InvalidPlan,
    InvalidIdentity,
    InvalidRestoreId,
    WorkspaceBusy,
    WorkspaceLeaseFailure,
    CheckpointDurabilityUnavailable,
    CheckpointPlanMismatch,
    RestoreIdConflict,
    ResourceLimitExceeded,
    InsufficientDiskSpace,
    StableStorageFailure,
    JournalPersistenceFailed,
    DestinationChanged,
    BackupCreationFailed,
    BackupVerificationFailed,
    CheckpointSourceChanged
};

struct PartyQuestReplicaDurableRestorePreparationReport
{
    PartyQuestReplicaDurableRestorePreparationStatus Status{
        PartyQuestReplicaDurableRestorePreparationStatus::InvalidPlan};
    std::optional<PartyQuestReplicaRestoreJournalState> State;
    std::filesystem::path JournalPath;
    size_t CompletedBackups{};
    size_t FailedOperation{};
    std::filesystem::path FailedPath;

    /**
     * True only after rollback evidence and BackupsReady journal state have both
     * crossed the reviewed stable-storage barriers. No live replica mutation has
     * occurred and MutationStarted has deliberately not been published.
     */
    [[nodiscard]] bool IsBackupsReady() const noexcept
    {
        return Status == PartyQuestReplicaDurableRestorePreparationStatus::BackupsReady &&
            State.has_value() &&
            State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady;
    }
};

/**
 * Power-loss durability preparation for destructive replica restore.
 *
 * This is deliberately the non-destructive half of the future strong executor.
 * It acquires the exact workspace lease, requires the immutable revision
 * checkpoint to pass the data-before-manifest durability promotion, binds the
 * supplied restore plan back to that exact promoted manifest, durably publishes
 * Prepared, creates/verifies durable rollback copies, then durably publishes
 * BackupsReady.
 *
 * It never stages a live replacement, never publishes MutationStarted, never
 * renames/removes a live replica destination and grants no Skyrim/world mutation
 * authority. A later destructive phase must reacquire/revalidate the same
 * workspace and all evidence before it may publish MutationStarted.
 *
 * Windows fails closed before acquiring a workspace lease or creating restore
 * metadata because durable checkpoint directory promotion and durable rollback
 * deletion are not yet proved there.
 */
class PartyQuestReplicaDurableRestorePreparation final
{
public:
    [[nodiscard]] static PartyQuestReplicaDurableRestorePreparationReport Prepare(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId) noexcept;
};
