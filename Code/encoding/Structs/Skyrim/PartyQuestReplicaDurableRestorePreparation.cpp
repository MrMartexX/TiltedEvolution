#include <Structs/Skyrim/PartyQuestReplicaDurableRestorePreparation.h>

#include <Structs/Skyrim/PartyQuestReplicaDurableSnapshot.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <filesystem>
#include <system_error>

namespace
{
bool SamePlan(
    const PartyQuestReplicaRestorePlan& acLeft,
    const PartyQuestReplicaRestorePlan& acRight) noexcept
{
    return acLeft.Status == acRight.Status &&
        acLeft.CampaignId == acRight.CampaignId &&
        acLeft.PlayerProfileId == acRight.PlayerProfileId &&
        acLeft.CheckpointKind == acRight.CheckpointKind &&
        acLeft.CampaignWorldRevision == acRight.CampaignWorldRevision &&
        acLeft.Operations == acRight.Operations;
}

PartyQuestReplicaDurableRestorePreparationStatus MapLeaseStatus(
    PartyQuestReplicaWorkspaceLeaseStatus aStatus) noexcept
{
    if (aStatus == PartyQuestReplicaWorkspaceLeaseStatus::Busy)
        return PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceBusy;
    return PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceLeaseFailure;
}

bool IsMissingPath(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        return status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory;
    }
    catch (...)
    {
        return false;
    }
}

bool IsNewTransactionPath(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory)
        {
            return true;
        }
        return false;
    }
    catch (...)
    {
        return false;
    }
}

bool MatchesPreparedDestination(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    if (acOperation.DestinationExisted)
    {
        const auto current = PartyQuestReplicaFileExecutor::ObserveRegularFile(
            acOperation.ReplicaDestinationPath);
        return current.has_value() &&
            current->Size == acOperation.OriginalSize &&
            current->Digest == acOperation.OriginalDigest;
    }

    return IsMissingPath(acOperation.ReplicaDestinationPath);
}

bool MatchesCheckpointSource(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    const auto current = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        acOperation.CheckpointSourcePath);
    return current.has_value() &&
        current->Size == acOperation.ExpectedRestoredSize &&
        current->Digest == acOperation.ExpectedRestoredDigest;
}

PartyQuestReplicaDurableRestorePreparationReport Failure(
    PartyQuestReplicaDurableRestorePreparationStatus aStatus,
    const PartyQuestReplicaRestoreJournalState* apState = nullptr,
    size_t aFailedOperation = 0,
    std::filesystem::path aFailedPath = {})
{
    PartyQuestReplicaDurableRestorePreparationReport report;
    report.Status = aStatus;
    report.FailedOperation = aFailedOperation;
    report.FailedPath = std::move(aFailedPath);
    if (apState)
    {
        report.State = *apState;
        report.JournalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(*apState);
    }
    return report;
}
} // namespace

PartyQuestReplicaDurableRestorePreparationReport
PartyQuestReplicaDurableRestorePreparation::Prepare(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId) noexcept
{
    if (!acPlan.IsReady())
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidPlan);
    if (!acPlan.CampaignId.IsValid() || !acPlan.PlayerProfileId.IsValid())
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidIdentity);
    if (aRestoreId == 0)
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidRestoreId);

#ifdef _WIN32
    // Do not even acquire the persistent workspace lock on the unsupported
    // strong-restore platform path. Windows checkpoint directory promotion and
    // durable deletion are both still intentionally unproved.
    (void)acPaths;
    return Failure(PartyQuestReplicaDurableRestorePreparationStatus::UnsupportedPlatform);
#else
    PartyQuestReplicaWorkspaceLease workspace;
    const auto leaseStatus = workspace.Acquire(
        acPaths,
        acPlan.CampaignId,
        acPlan.PlayerProfileId);
    if (leaseStatus != PartyQuestReplicaWorkspaceLeaseStatus::Acquired)
        return Failure(MapLeaseStatus(leaseStatus));

    const auto promotion = PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
        acPaths,
        acPlan.CampaignId,
        acPlan.PlayerProfileId,
        acPlan.CheckpointKind,
        acPlan.CampaignWorldRevision);
    if (!promotion.IsPromoted())
    {
        return Failure(
            promotion.Status == PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform
                ? PartyQuestReplicaDurableRestorePreparationStatus::UnsupportedPlatform
                : PartyQuestReplicaDurableRestorePreparationStatus::CheckpointDurabilityUnavailable);
    }

    // Rebuild the exact restore plan from the now-durable manifest. This makes
    // the supplied source/digest/path set prove it belongs to the same promoted
    // authority marker rather than to another legacy checkpoint with coincident
    // campaign/kind/revision fields.
    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        acPaths,
        acPlan.CheckpointKind,
        acPlan.CampaignWorldRevision);
    const auto manifest = PartyQuestReplicaManifestStore::Load(manifestPath);
    if (manifest.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
        !manifest.Manifest)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::CheckpointDurabilityUnavailable);
    }

    const auto rebuiltPlan = PartyQuestReplicaRestorePlanner::Build(
        acPaths,
        acPlan.CampaignId,
        acPlan.PlayerProfileId,
        *manifest.Manifest);
    if (!rebuiltPlan.IsReady() || !SamePlan(acPlan, rebuiltPlan))
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::CheckpointPlanMismatch);
    }

    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        acPaths,
        acPlan,
        aRestoreId);
    if (!prepared.IsReady())
    {
        return Failure(
            prepared.Status == PartyQuestReplicaRestoreJournalStatus::InvalidIdentity
                ? PartyQuestReplicaDurableRestorePreparationStatus::InvalidIdentity
                : PartyQuestReplicaDurableRestorePreparationStatus::InvalidPlan);
    }

    PartyQuestReplicaRestoreJournalState state = std::move(*prepared.State);
    if (!IsNewTransactionPath(state.TransactionDirectory))
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::RestoreIdConflict,
            &state,
            0,
            state.TransactionDirectory);
    }

    const auto requiredBytes =
        PartyQuestReplicaRestoreResourcePolicy::RequiredFreeBytes(state);
    if (!requiredBytes)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::ResourceLimitExceeded,
            &state);
    }

    try
    {
        std::error_code ec;
        const auto space = std::filesystem::space(acPaths.PlayerDirectory, ec);
        if (ec)
        {
            return Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::StableStorageFailure,
                &state,
                0,
                acPaths.PlayerDirectory);
        }
        if (!PartyQuestReplicaRestoreResourcePolicy::HasSufficientDiskSpace(
                state,
                space.available))
        {
            return Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::InsufficientDiskSpace,
                &state);
        }
    }
    catch (...)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::StableStorageFailure,
            &state,
            0,
            acPaths.PlayerDirectory);
    }

    auto stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
        state.TransactionDirectory / "rollback");
    if (stable != PartyQuestStableStorageStatus::Success)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::StableStorageFailure,
            &state,
            0,
            state.TransactionDirectory);
    }

    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    const auto preparedSave =
        PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
            journalPath,
            state);
    if (preparedSave != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::JournalPersistenceFailed,
            &state,
            0,
            journalPath);
    }

    size_t completedBackups = 0;
    for (size_t index = 0; index < state.Operations.size(); ++index)
    {
        const auto& operation = state.Operations[index];
        if (!MatchesPreparedDestination(operation))
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::DestinationChanged,
                &state,
                index,
                operation.ReplicaDestinationPath);
            report.CompletedBackups = completedBackups;
            return report;
        }
        if (!MatchesCheckpointSource(operation))
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::CheckpointSourceChanged,
                &state,
                index,
                operation.CheckpointSourcePath);
            report.CompletedBackups = completedBackups;
            return report;
        }

        if (!operation.DestinationExisted)
            continue;

        stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
            operation.RollbackPath.parent_path());
        if (stable != PartyQuestStableStorageStatus::Success)
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::StableStorageFailure,
                &state,
                index,
                operation.RollbackPath.parent_path());
            report.CompletedBackups = completedBackups;
            return report;
        }

        stable = PartyQuestStableStorage::CopyFileDurably(
            operation.ReplicaDestinationPath,
            operation.RollbackPath);
        if (stable != PartyQuestStableStorageStatus::Success)
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::BackupCreationFailed,
                &state,
                index,
                operation.RollbackPath);
            report.CompletedBackups = completedBackups;
            return report;
        }

        const auto backup = PartyQuestReplicaFileExecutor::ObserveRegularFile(
            operation.RollbackPath);
        if (!backup ||
            backup->Size != operation.OriginalSize ||
            backup->Digest != operation.OriginalDigest)
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
                &state,
                index,
                operation.RollbackPath);
            report.CompletedBackups = completedBackups;
            return report;
        }
        ++completedBackups;
    }

    // Revalidate both sides immediately before advancing journal authority.
    for (size_t index = 0; index < state.Operations.size(); ++index)
    {
        const auto& operation = state.Operations[index];
        if (!MatchesPreparedDestination(operation))
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::DestinationChanged,
                &state,
                index,
                operation.ReplicaDestinationPath);
            report.CompletedBackups = completedBackups;
            return report;
        }
        if (!MatchesCheckpointSource(operation))
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::CheckpointSourceChanged,
                &state,
                index,
                operation.CheckpointSourcePath);
            report.CompletedBackups = completedBackups;
            return report;
        }
    }

    if (!PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(state))
    {
        auto report = Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
            &state);
        report.CompletedBackups = completedBackups;
        return report;
    }

    PartyQuestReplicaRestoreJournalState backupsReady = state;
    if (PartyQuestReplicaRestoreJournal::MarkBackupsReady(backupsReady) !=
        PartyQuestReplicaRestoreJournalStatus::Ready)
    {
        auto report = Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
            &state);
        report.CompletedBackups = completedBackups;
        return report;
    }

    const auto readySave =
        PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
            journalPath,
            backupsReady);
    if (readySave != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        auto report = Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::JournalPersistenceFailed,
            &state,
            0,
            journalPath);
        report.CompletedBackups = completedBackups;
        return report;
    }

    PartyQuestReplicaDurableRestorePreparationReport report;
    report.Status = PartyQuestReplicaDurableRestorePreparationStatus::BackupsReady;
    report.State = std::move(backupsReady);
    report.JournalPath = journalPath;
    report.CompletedBackups = completedBackups;
    return report;
#endif
}
