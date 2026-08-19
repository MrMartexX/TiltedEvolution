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

bool SamePreparedIdentity(
    const PartyQuestReplicaRestoreJournalState& acExpected,
    const PartyQuestReplicaRestoreJournalState& acPersisted) noexcept
{
    return acExpected.CampaignId == acPersisted.CampaignId &&
        acExpected.PlayerProfileId == acPersisted.PlayerProfileId &&
        acExpected.RestoreId == acPersisted.RestoreId &&
        acExpected.CheckpointKind == acPersisted.CheckpointKind &&
        acExpected.CampaignWorldRevision == acPersisted.CampaignWorldRevision &&
        acExpected.TransactionDirectory.lexically_normal() ==
            acPersisted.TransactionDirectory.lexically_normal() &&
        acExpected.Operations == acPersisted.Operations;
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
        return status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory;
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

enum class RollbackEvidence : uint8_t
{
    Missing,
    Exact,
    Invalid
};

RollbackEvidence InspectRollbackEvidence(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    if (IsMissingPath(acOperation.RollbackPath))
        return RollbackEvidence::Missing;

    const auto backup = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        acOperation.RollbackPath);
    if (!backup ||
        backup->Size != acOperation.OriginalSize ||
        backup->Digest != acOperation.OriginalDigest)
    {
        return RollbackEvidence::Invalid;
    }
    return RollbackEvidence::Exact;
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

PartyQuestReplicaDurableRestorePreparationReport ReadyReport(
    PartyQuestReplicaRestoreJournalState aState,
    size_t aCompletedBackups)
{
    PartyQuestReplicaDurableRestorePreparationReport report;
    report.Status = PartyQuestReplicaDurableRestorePreparationStatus::BackupsReady;
    report.JournalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(aState);
    report.CompletedBackups = aCompletedBackups;
    report.State = std::move(aState);
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

    const auto capability = workspace.CreatePublicationCapability(
        acPaths,
        acPlan.CampaignId,
        acPlan.PlayerProfileId);
    if (!capability.Protects(
            acPaths,
            acPlan.CampaignId,
            acPlan.PlayerProfileId))
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceLeaseFailure);
    }

    return PrepareAuthorized(acPaths, acPlan, aRestoreId, capability);
#endif
}

PartyQuestReplicaDurableRestorePreparationReport
PartyQuestReplicaDurableRestorePreparation::PrepareAuthorized(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId,
    const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept
{
    if (!acPlan.IsReady())
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidPlan);
    if (!acPlan.CampaignId.IsValid() || !acPlan.PlayerProfileId.IsValid())
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidIdentity);
    if (aRestoreId == 0)
        return Failure(PartyQuestReplicaDurableRestorePreparationStatus::InvalidRestoreId);

#ifdef _WIN32
    (void)acPaths;
    (void)acWorkspaceCapability;
    return Failure(PartyQuestReplicaDurableRestorePreparationStatus::UnsupportedPlatform);
#else
    if (!acWorkspaceCapability.Protects(
            acPaths,
            acPlan.CampaignId,
            acPlan.PlayerProfileId))
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceLeaseFailure);
    }

    const auto promotion =
        PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpointAuthorized(
            acPaths,
            acPlan.CampaignId,
            acPlan.PlayerProfileId,
            acPlan.CheckpointKind,
            acPlan.CampaignWorldRevision,
            acWorkspaceCapability);
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

    PartyQuestReplicaRestoreJournalState expected = std::move(*prepared.State);
    PartyQuestReplicaRestoreJournalState state = expected;
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(expected);
    const bool freshTransaction = IsNewTransactionPath(expected.TransactionDirectory);

    if (!freshTransaction)
    {
        // An occupied RestoreId is resumable only when the exact strong journal
        // already names this same plan and remains strictly before mutation.
        // Ambiguous/v3/wrong-id/wrong-plan evidence is never overwritten.
        const auto persisted =
            PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
                journalPath);
        if (persisted.Status !=
                PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
            !persisted.State ||
            !SamePreparedIdentity(expected, *persisted.State) ||
            (persisted.State->Phase != PartyQuestReplicaRestoreJournalPhase::Prepared &&
             persisted.State->Phase != PartyQuestReplicaRestoreJournalPhase::BackupsReady))
        {
            return Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::RestoreIdConflict,
                &expected,
                0,
                expected.TransactionDirectory);
        }
        state = *persisted.State;
    }

    // RequiredFreeBytes is also the bounded-resource validation for a restore
    // journal. A resumed transaction already paid for any existing rollback
    // copies, so the fresh-operation free-space estimate must not be applied a
    // second time; missing copies still fail closed at CopyFileDurably on ENOSPC.
    const auto requiredBytes =
        PartyQuestReplicaRestoreResourcePolicy::RequiredFreeBytes(state);
    if (!requiredBytes)
    {
        return Failure(
            PartyQuestReplicaDurableRestorePreparationStatus::ResourceLimitExceeded,
            &state);
    }

    if (freshTransaction)
    {
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

        const auto rollbackEvidence = InspectRollbackEvidence(operation);
        if (!operation.DestinationExisted)
        {
            if (rollbackEvidence != RollbackEvidence::Missing)
            {
                auto report = Failure(
                    PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
                    &state,
                    index,
                    operation.RollbackPath);
                report.CompletedBackups = completedBackups;
                return report;
            }
            continue;
        }

        if (rollbackEvidence == RollbackEvidence::Invalid)
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
                &state,
                index,
                operation.RollbackPath);
            report.CompletedBackups = completedBackups;
            return report;
        }

        if (rollbackEvidence == RollbackEvidence::Exact)
        {
            ++completedBackups;
            continue;
        }

        // BackupsReady is durable authority that every required backup already
        // crossed its barrier. Missing evidence in that phase is corruption, not
        // permission to silently reconstruct it.
        if (state.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady)
        {
            auto report = Failure(
                PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed,
                &state,
                index,
                operation.RollbackPath);
            report.CompletedBackups = completedBackups;
            return report;
        }

        auto stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
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

    // Revalidate both sides immediately before advancing journal authority or
    // accepting an already-durable BackupsReady phase after restart.
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

    if (state.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady)
        return ReadyReport(std::move(state), completedBackups);

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

    return ReadyReport(std::move(backupsReady), completedBackups);
#endif
}
