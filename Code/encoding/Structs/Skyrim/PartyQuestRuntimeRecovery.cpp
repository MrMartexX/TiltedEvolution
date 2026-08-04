#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>

#include <iomanip>
#include <sstream>

namespace
{
std::filesystem::path GetRestoreJournalPath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return acPaths.MetadataDirectory / "restore" / stream.str() / "journal.bin";
}

PartyQuestRuntimeRecoveryResult MakeResult(
    PartyQuestRuntimeRecoveryStatus aStatus,
    const PartyQuestRuntimeApplyEntry* apRecovery,
    uint64_t aRestoreId,
    const std::filesystem::path& acManifestPath = {},
    const std::filesystem::path& acJournalPath = {})
{
    PartyQuestRuntimeRecoveryResult result;
    result.Status = aStatus;
    result.RestoreId = aRestoreId;
    result.ManifestPath = acManifestPath;
    result.RestoreJournalPath = acJournalPath;
    if (apRecovery)
    {
        result.TransactionId = apRecovery->TransactionId;
        result.TargetWorldRevision = apRecovery->TargetWorldRevision;
    }
    return result;
}

bool IsExpectedRecoveryRecord(const PartyQuestRuntimeApplyEntry& acRecovery) noexcept
{
    return acRecovery.TransactionId != 0 &&
        acRecovery.TargetWorldRevision != 0 &&
        acRecovery.QuestId &&
        acRecovery.CanonicalDigest != 0 &&
        acRecovery.Actions != PartyQuestApplyAction::None &&
        acRecovery.CheckpointCreated &&
        acRecovery.RuntimeMutationMayHaveOccurred;
}

bool JournalMatchesPlan(
    const PartyQuestReplicaRestoreJournalState& acState,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId) noexcept
{
    if (acState.RestoreId != aRestoreId ||
        acState.CampaignId != acPlan.CampaignId ||
        acState.PlayerProfileId != acPlan.PlayerProfileId ||
        acState.CheckpointKind != acPlan.CheckpointKind ||
        acState.CampaignWorldRevision != acPlan.CampaignWorldRevision ||
        acState.Operations.size() != acPlan.Operations.size())
    {
        return false;
    }

    for (size_t i = 0; i < acPlan.Operations.size(); ++i)
    {
        const auto& journal = acState.Operations[i];
        const auto& plan = acPlan.Operations[i];
        if (journal.Kind != plan.Kind ||
            journal.CheckpointSourcePath.lexically_normal() !=
                plan.CheckpointSourcePath.lexically_normal() ||
            journal.ReplicaDestinationPath.lexically_normal() !=
                plan.ReplicaDestinationPath.lexically_normal() ||
            journal.ExpectedRestoredSize != plan.ExpectedSize ||
            journal.ExpectedRestoredDigest != plan.ExpectedDigest)
        {
            return false;
        }
    }

    return true;
}
} // namespace

PartyQuestRuntimeRecoveryResult
PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId) noexcept
{
    try
    {
        const auto& coordinator = aSession.GetCoordinator();
        const PartyQuestRuntimeApplyEntry* pRecovery = coordinator.GetRecoveryRecord();

        if (!aSession.GetCampaignId().IsValid() ||
            !aSession.GetPlayerProfileId().IsValid())
        {
            return MakeResult(
                PartyQuestRuntimeRecoveryStatus::InvalidIdentity,
                pRecovery,
                aRestoreId);
        }
        if (aRestoreId == 0)
        {
            return MakeResult(
                PartyQuestRuntimeRecoveryStatus::InvalidRestoreId,
                pRecovery,
                aRestoreId);
        }
        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return MakeResult(
                PartyQuestRuntimeRecoveryStatus::InvalidLayout,
                pRecovery,
                aRestoreId);
        }
        if (!coordinator.IsRecoveryBlocked() ||
            !pRecovery ||
            !IsExpectedRecoveryRecord(*pRecovery))
        {
            return MakeResult(
                PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState,
                pRecovery,
                aRestoreId);
        }

        const uint64_t transactionId = pRecovery->TransactionId;
        const uint64_t targetWorldRevision = pRecovery->TargetWorldRevision;
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);
        const auto journalPath = GetRestoreJournalPath(acPaths, aRestoreId);

        PartyQuestRuntimeRecoveryResult result = MakeResult(
            PartyQuestRuntimeRecoveryStatus::CheckpointMissing,
            pRecovery,
            aRestoreId,
            manifestPath,
            journalPath);

        const auto loadedManifest = PartyQuestReplicaManifestStore::Load(manifestPath);
        result.ManifestStatus = loadedManifest.Status;
        if (loadedManifest.Status == PartyQuestReplicaManifestPersistenceStatus::FileNotFound)
            return result;
        if (loadedManifest.Status ==
            PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired)
        {
            result.Status =
                PartyQuestRuntimeRecoveryStatus::CheckpointManifestRecoveryRequired;
            return result;
        }
        if (loadedManifest.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
            !loadedManifest.Manifest)
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::CheckpointManifestInvalid;
            return result;
        }

        const PartyQuestReplicaManifest& manifest = *loadedManifest.Manifest;
        if (manifest.CampaignId != aSession.GetCampaignId() ||
            manifest.PlayerProfileId != aSession.GetPlayerProfileId() ||
            manifest.SnapshotType != PartyQuestReplicaSnapshotType::RevisionCheckpoint ||
            manifest.CheckpointKind != PartyQuestCheckpointKind::PreRepair ||
            manifest.CampaignWorldRevision != targetWorldRevision)
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::CheckpointManifestInvalid;
            return result;
        }

        result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
            acPaths,
            aSession.GetCampaignId(),
            aSession.GetPlayerProfileId(),
            manifest);
        if (result.VerificationStatus !=
            PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::CheckpointVerificationFailed;
            return result;
        }

        const auto restorePlan = PartyQuestReplicaRestorePlanner::Build(
            acPaths,
            aSession.GetCampaignId(),
            aSession.GetPlayerProfileId(),
            manifest);
        result.RestorePlanStatus = restorePlan.Status;
        if (!restorePlan.IsReady() ||
            restorePlan.CheckpointKind != PartyQuestCheckpointKind::PreRepair ||
            restorePlan.CampaignWorldRevision != targetWorldRevision)
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::RestorePlanInvalid;
            return result;
        }

        PartyQuestReplicaRestoreExecutionReport restoreReport;
        const auto loadedJournal =
            PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
        if (loadedJournal.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            if (!loadedJournal.State ||
                !JournalMatchesPlan(*loadedJournal.State, restorePlan, aRestoreId))
            {
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                return result;
            }

            restoreReport = PartyQuestReplicaRestoreExecutor::Recover(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId(),
                journalPath);
        }
        else if (loadedJournal.Status ==
            PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        {
            restoreReport = PartyQuestReplicaRestoreExecutor::Execute(
                acPaths,
                restorePlan,
                aRestoreId);
        }
        else
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
            result.RestoreStatus = loadedJournal.Status ==
                    PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired
                ? PartyQuestReplicaRestoreExecutionStatus::BackupRecoveryRequired
                : PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed;
            return result;
        }

        result.RestoreStatus = restoreReport.Status;
        result.RestoreJournalPath = restoreReport.JournalPath.empty()
            ? journalPath
            : restoreReport.JournalPath;

        if (restoreReport.Status ==
            PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback)
        {
            result.Status =
                PartyQuestRuntimeRecoveryStatus::RollbackRecoveredRetryRequired;
            return result;
        }
        if (!restoreReport.IsCheckpointRestored())
        {
            result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
            return result;
        }

        result.RuntimeTransition =
            aSession.CompleteCrashCheckpointRestore(transactionId);
        switch (result.RuntimeTransition)
        {
        case PartyQuestRuntimeDurableTransitionStatus::Applied:
            result.Status = restoreReport.Status ==
                    PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted
                ? PartyQuestRuntimeRecoveryStatus::AlreadyRestored
                : PartyQuestRuntimeRecoveryStatus::Restored;
            return result;

        case PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure:
            result.Status =
                PartyQuestRuntimeRecoveryStatus::RuntimeStatePersistenceFailed;
            return result;

        case PartyQuestRuntimeDurableTransitionStatus::InvalidState:
        case PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired:
            result.Status = PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState;
            return result;
        }
    }
    catch (...)
    {
        return MakeResult(
            PartyQuestRuntimeRecoveryStatus::RestoreFailed,
            aSession.GetCoordinator().GetRecoveryRecord(),
            aRestoreId);
    }

    return MakeResult(
        PartyQuestRuntimeRecoveryStatus::RestoreFailed,
        aSession.GetCoordinator().GetRecoveryRecord(),
        aRestoreId);
}
