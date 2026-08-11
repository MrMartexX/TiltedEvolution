#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestRuntimeWorkspacePublicationAuthority.h>

#include <iomanip>
#include <sstream>

namespace
{
std::filesystem::path GetLiveRestoreJournalPath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return acPaths.MetadataDirectory / "restore" / stream.str() / "journal.bin";
}

PartyQuestRuntimeRecoveryResult MakeLiveRecoveryResult(
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

bool IsExpectedLiveRecoveryRecord(
    const PartyQuestRuntimeApplyEntry& acRecovery) noexcept
{
    if (acRecovery.TransactionId == 0 ||
        acRecovery.TargetWorldRevision == 0 ||
        !acRecovery.QuestId ||
        acRecovery.CanonicalDigest == 0 ||
        acRecovery.Actions == PartyQuestApplyAction::None ||
        !PartyQuestVerificationPolicy::IsCompleteForActions(
            acRecovery.ExpectedVerification,
            acRecovery.Actions) ||
        acRecovery.ExpectedVerification.QuestSnapshotDigest != acRecovery.CanonicalDigest ||
        !acRecovery.SaveGuardActive ||
        !acRecovery.CheckpointCreated ||
        !acRecovery.RuntimeMutationMayHaveOccurred)
    {
        return false;
    }

    switch (acRecovery.State)
    {
    case PartyQuestRuntimeApplyState::WaitingForPapyrus:
    case PartyQuestRuntimeApplyState::Verifying:
    case PartyQuestRuntimeApplyState::ReadyToCommit:
        return true;
    case PartyQuestRuntimeApplyState::DeferredWorld:
    case PartyQuestRuntimeApplyState::AwaitingCheckpoint:
    case PartyQuestRuntimeApplyState::ReadyToApply:
        return false;
    }

    return false;
}

bool LiveJournalMatchesPlan(
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

bool VerifyLiveRecoveryDestinations(
    const PartyQuestReplicaRestorePlan& acPlan) noexcept
{
    if (!acPlan.IsReady() || acPlan.Operations.empty())
        return false;

    for (const auto& operation : acPlan.Operations)
    {
        const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(
            operation.ReplicaDestinationPath);
        if (!observation ||
            observation->Size != operation.ExpectedSize ||
            observation->Digest != operation.ExpectedDigest)
        {
            return false;
        }
    }

    return true;
}
} // namespace

PartyQuestRuntimeRecoveryResult
PartyQuestRuntimeRecoveryCoordinator::ResolveLiveRecovery(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        const auto& coordinator = aSession.GetCoordinator();
        const PartyQuestRuntimeApplyEntry* pRecovery = coordinator.GetActive();
        const uint64_t candidateRestoreId = pRecovery ? pRecovery->TransactionId : 0;

        if (!aSession.GetCampaignId().IsValid() ||
            !aSession.GetPlayerProfileId().IsValid())
        {
            return MakeLiveRecoveryResult(
                PartyQuestRuntimeRecoveryStatus::InvalidIdentity,
                pRecovery,
                candidateRestoreId);
        }
        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return MakeLiveRecoveryResult(
                PartyQuestRuntimeRecoveryStatus::InvalidLayout,
                pRecovery,
                candidateRestoreId);
        }
        if (coordinator.IsRecoveryBlocked() ||
            !pRecovery ||
            !IsExpectedLiveRecoveryRecord(*pRecovery))
        {
            return MakeLiveRecoveryResult(
                PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState,
                pRecovery,
                candidateRestoreId);
        }

        const uint64_t transactionId = pRecovery->TransactionId;
        const uint64_t restoreId = transactionId;
        const uint64_t targetWorldRevision = pRecovery->TargetWorldRevision;
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);
        const auto journalPath = GetLiveRestoreJournalPath(acPaths, restoreId);

        PartyQuestRuntimeRecoveryResult result = MakeLiveRecoveryResult(
            PartyQuestRuntimeRecoveryStatus::CheckpointMissing,
            pRecovery,
            restoreId,
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

        // Keep one exact workspace capability alive through restore, the
        // independent live-byte reverify and the durable barrier clear. The
        // capability itself pins the native lease state, so a temporary lease
        // can hand off ownership without opening a TOCTOU window.
        auto workspaceCapability =
            PartyQuestRuntimeWorkspacePublicationAuthority::Acquire(
                aSession,
                acPaths);
        if (!workspaceCapability.IsVerified())
        {
            PartyQuestReplicaWorkspaceLease recoveryLease;
            const auto leaseStatus = recoveryLease.Acquire(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId());
            if (leaseStatus != PartyQuestReplicaWorkspaceLeaseStatus::Acquired)
            {
                result.RestoreStatus = leaseStatus ==
                        PartyQuestReplicaWorkspaceLeaseStatus::Busy
                    ? PartyQuestReplicaRestoreExecutionStatus::WorkspaceBusy
                    : PartyQuestReplicaRestoreExecutionStatus::WorkspaceLeaseFailure;
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                return result;
            }
            workspaceCapability = recoveryLease.CreatePublicationCapability(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId());
            if (!workspaceCapability.IsVerified())
            {
                result.RestoreStatus =
                    PartyQuestReplicaRestoreExecutionStatus::WorkspaceLeaseFailure;
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                return result;
            }
        }

        PartyQuestReplicaRestoreExecutionReport restoreReport;
        const auto loadedJournal =
            PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
        if (loadedJournal.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            if (!loadedJournal.State ||
                !LiveJournalMatchesPlan(*loadedJournal.State, restorePlan, restoreId))
            {
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                return result;
            }

            restoreReport = PartyQuestReplicaRestoreExecutor::RecoverAuthorized(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId(),
                journalPath,
                workspaceCapability);
        }
        else if (loadedJournal.Status ==
            PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        {
            restoreReport = PartyQuestReplicaRestoreExecutor::ExecuteAuthorized(
                acPaths,
                restorePlan,
                restoreId,
                workspaceCapability);
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

        if (!VerifyLiveRecoveryDestinations(restorePlan))
        {
            result.RestoreStatus =
                PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed;
            result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
            return result;
        }

        result.RuntimeTransition =
            aSession.CompleteLiveCheckpointRestoreInternal(transactionId);
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
        case PartyQuestRuntimeDurableTransitionStatus::InsufficientDurability:
            result.Status = PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState;
            return result;
        }
    }
    catch (...)
    {
        const auto* recovery = aSession.GetCoordinator().GetActive();
        return MakeLiveRecoveryResult(
            PartyQuestRuntimeRecoveryStatus::RestoreFailed,
            recovery,
            recovery ? recovery->TransactionId : 0);
    }

    const auto* recovery = aSession.GetCoordinator().GetActive();
    return MakeLiveRecoveryResult(
        PartyQuestRuntimeRecoveryStatus::RestoreFailed,
        recovery,
        recovery ? recovery->TransactionId : 0);
}
