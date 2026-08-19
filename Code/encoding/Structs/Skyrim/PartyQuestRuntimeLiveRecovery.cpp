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

PartyQuestReplicaRestoreExecutionStatus MapDurableLiveRestoreStatus(
    PartyQuestReplicaDurableRestoreStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestReplicaDurableRestoreStatus::Success:
        return PartyQuestReplicaRestoreExecutionStatus::Success;
    case PartyQuestReplicaDurableRestoreStatus::AlreadyCommitted:
    case PartyQuestReplicaDurableRestoreStatus::RecoveredCommit:
        return PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted;
    case PartyQuestReplicaDurableRestoreStatus::RecoveredRollback:
    case PartyQuestReplicaDurableRestoreStatus::AlreadyRolledBack:
        return PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback;
    case PartyQuestReplicaDurableRestoreStatus::InvalidIdentity:
        return PartyQuestReplicaRestoreExecutionStatus::InvalidIdentity;
    case PartyQuestReplicaDurableRestoreStatus::JournalNotFound:
    case PartyQuestReplicaDurableRestoreStatus::JournalLoadFailed:
        return PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed;
    case PartyQuestReplicaDurableRestoreStatus::UnsafePath:
        return PartyQuestReplicaRestoreExecutionStatus::UnsafePath;
    case PartyQuestReplicaDurableRestoreStatus::WorkspaceBusy:
        return PartyQuestReplicaRestoreExecutionStatus::WorkspaceBusy;
    case PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure:
    case PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform:
        return PartyQuestReplicaRestoreExecutionStatus::WorkspaceLeaseFailure;
    case PartyQuestReplicaDurableRestoreStatus::CheckpointSourceChanged:
        return PartyQuestReplicaRestoreExecutionStatus::CheckpointSourceChanged;
    case PartyQuestReplicaDurableRestoreStatus::BackupVerificationFailed:
        return PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed;
    case PartyQuestReplicaDurableRestoreStatus::DestinationChanged:
        return PartyQuestReplicaRestoreExecutionStatus::DestinationChanged;
    case PartyQuestReplicaDurableRestoreStatus::StagingFailed:
        return PartyQuestReplicaRestoreExecutionStatus::StagingFailed;
    case PartyQuestReplicaDurableRestoreStatus::ReplacementFailed:
        return PartyQuestReplicaRestoreExecutionStatus::ReplacementFailed;
    case PartyQuestReplicaDurableRestoreStatus::RestoredVerificationFailed:
    case PartyQuestReplicaDurableRestoreStatus::CommittedVerificationFailed:
        return PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed;
    case PartyQuestReplicaDurableRestoreStatus::RollbackFailed:
    case PartyQuestReplicaDurableRestoreStatus::RolledBackVerificationFailed:
        return PartyQuestReplicaRestoreExecutionStatus::RollbackFailed;
    case PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed:
        return PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed;
    case PartyQuestReplicaDurableRestoreStatus::CheckpointDurabilityUnavailable:
    case PartyQuestReplicaDurableRestoreStatus::CheckpointPlanMismatch:
    case PartyQuestReplicaDurableRestoreStatus::InvalidPhase:
    case PartyQuestReplicaDurableRestoreStatus::ResumeBeforeMutation:
    case PartyQuestReplicaDurableRestoreStatus::CleanupFailed:
    case PartyQuestReplicaDurableRestoreStatus::FaultInjected:
        return PartyQuestReplicaRestoreExecutionStatus::InvalidPlan;
    }
    return PartyQuestReplicaRestoreExecutionStatus::InvalidPlan;
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
        const uint64_t targetWorldRevision = pRecovery->TargetWorldRevision;
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);
        const auto legacyJournalPath =
            GetLiveRestoreJournalPath(acPaths, transactionId);

        PartyQuestRuntimeRecoveryResult result = MakeLiveRecoveryResult(
            PartyQuestRuntimeRecoveryStatus::CheckpointMissing,
            pRecovery,
            transactionId,
            manifestPath,
            legacyJournalPath);

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

        // Persisted attempt identity is authoritative before the transaction-id
        // path is interpreted as legacy. RestoreId may numerically equal the
        // runtime TransactionId, but a persisted attempt makes that path v4.
        auto attempt = PartyQuestRuntimeRestoreAttemptStore::Load(
            acPaths,
            aSession.GetCampaignId(),
            aSession.GetPlayerProfileId(),
            transactionId);
        result.RestoreAttemptStatus = attempt.Status;

        if (attempt.Status == PartyQuestRuntimeRestoreAttemptStatus::Success &&
            attempt.State)
        {
            const auto& currentAttempt = *attempt.State;
            const auto strongJournalPath = attempt.JournalPath;
            result.RestoreId = currentAttempt.CurrentRestoreId;
            result.RestoreJournalPath = strongJournalPath;
            result.RestoreDomain =
                PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable;

            if (strongJournalPath.lexically_normal() !=
                legacyJournalPath.lexically_normal())
            {
                const auto legacyEvidence =
                    PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
                if (legacyEvidence.Status !=
                    PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
                {
                    result.Status =
                        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                    result.RestoreStatus = legacyEvidence.Status ==
                            PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired
                        ? PartyQuestReplicaRestoreExecutionStatus::BackupRecoveryRequired
                        : PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed;
                    return result;
                }
            }

            PartyQuestReplicaDurableRestoreReport durableReport;
            const auto strongJournal =
                PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
                    strongJournalPath);
            if (strongJournal.Status ==
                PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
            {
                const auto prepared =
                    PartyQuestReplicaDurableRestorePreparation::PrepareAuthorized(
                        acPaths,
                        restorePlan,
                        currentAttempt.CurrentRestoreId,
                        workspaceCapability);
                result.DurablePreparationStatus = prepared.Status;
                if (!prepared.IsBackupsReady())
                {
                    result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                    return result;
                }

                durableReport =
                    PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
                        acPaths,
                        aSession.GetCampaignId(),
                        aSession.GetPlayerProfileId(),
                        prepared.JournalPath,
                        workspaceCapability);
            }
            else if (strongJournal.Status ==
                         PartyQuestReplicaRestoreJournalPersistenceStatus::Success &&
                     strongJournal.State &&
                     LiveJournalMatchesPlan(
                         *strongJournal.State,
                         restorePlan,
                         currentAttempt.CurrentRestoreId))
            {
                switch (strongJournal.State->Phase)
                {
                case PartyQuestReplicaRestoreJournalPhase::Prepared:
                {
                    const auto prepared =
                        PartyQuestReplicaDurableRestorePreparation::PrepareAuthorized(
                            acPaths,
                            restorePlan,
                            currentAttempt.CurrentRestoreId,
                            workspaceCapability);
                    result.DurablePreparationStatus = prepared.Status;
                    if (!prepared.IsBackupsReady())
                    {
                        result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                        return result;
                    }
                    durableReport =
                        PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
                            acPaths,
                            aSession.GetCampaignId(),
                            aSession.GetPlayerProfileId(),
                            prepared.JournalPath,
                            workspaceCapability);
                    break;
                }
                case PartyQuestReplicaRestoreJournalPhase::BackupsReady:
                    durableReport =
                        PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
                            acPaths,
                            aSession.GetCampaignId(),
                            aSession.GetPlayerProfileId(),
                            strongJournalPath,
                            workspaceCapability);
                    break;
                case PartyQuestReplicaRestoreJournalPhase::MutationStarted:
                case PartyQuestReplicaRestoreJournalPhase::Restored:
                case PartyQuestReplicaRestoreJournalPhase::Committed:
                case PartyQuestReplicaRestoreJournalPhase::RolledBack:
                    durableReport =
                        PartyQuestReplicaDurableRestoreExecutor::RecoverAuthorized(
                            acPaths,
                            aSession.GetCampaignId(),
                            aSession.GetPlayerProfileId(),
                            strongJournalPath,
                            workspaceCapability);
                    break;
                }
            }
            else
            {
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                result.RestoreStatus =
                    PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed;
                return result;
            }

            result.DurableRestoreStatus = durableReport.Status;
            result.RestoreStatus = MapDurableLiveRestoreStatus(durableReport.Status);
            result.RestoreJournalPath = durableReport.JournalPath.empty()
                ? strongJournalPath
                : durableReport.JournalPath;

            if (durableReport.Status ==
                    PartyQuestReplicaDurableRestoreStatus::RecoveredRollback ||
                durableReport.Status ==
                    PartyQuestReplicaDurableRestoreStatus::AlreadyRolledBack)
            {
                const auto advanced =
                    PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
                        acPaths,
                        aSession.GetCampaignId(),
                        aSession.GetPlayerProfileId(),
                        transactionId,
                        currentAttempt.CurrentOrdinal,
                        workspaceCapability);
                result.RestoreAttemptStatus = advanced.Status;
                if (advanced.Status != PartyQuestRuntimeRestoreAttemptStatus::Success &&
                    advanced.Status !=
                        PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced)
                {
                    result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                    return result;
                }

                result.Status =
                    PartyQuestRuntimeRecoveryStatus::RollbackRecoveredRetryRequired;
                return result;
            }

            if (!durableReport.IsCheckpointRestored())
            {
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                return result;
            }
        }
        else if (attempt.Status == PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        {
            const auto loadedLegacy =
                PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
            if (loadedLegacy.Status ==
                PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            {
                if (!loadedLegacy.State ||
                    !LiveJournalMatchesPlan(
                        *loadedLegacy.State,
                        restorePlan,
                        transactionId))
                {
                    result.Status =
                        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                    return result;
                }

                result.RestoreDomain =
                    PartyQuestRuntimeRestoreDurabilityDomain::ProcessCrashResilient;
                auto restoreReport = PartyQuestReplicaRestoreExecutor::RecoverAuthorized(
                    acPaths,
                    aSession.GetCampaignId(),
                    aSession.GetPlayerProfileId(),
                    legacyJournalPath,
                    workspaceCapability);
                result.RestoreStatus = restoreReport.Status;
                result.RestoreJournalPath = restoreReport.JournalPath.empty()
                    ? legacyJournalPath
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
            }
            else if (loadedLegacy.Status ==
                PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
            {
#ifdef _WIN32
                // Windows fresh live recovery remains explicit v3 until its
                // strong directory/delete durability contract is accepted.
                result.RestoreDomain =
                    PartyQuestRuntimeRestoreDurabilityDomain::ProcessCrashResilient;
                auto restoreReport = PartyQuestReplicaRestoreExecutor::ExecuteAuthorized(
                    acPaths,
                    restorePlan,
                    transactionId,
                    workspaceCapability);
                result.RestoreStatus = restoreReport.Status;
                result.RestoreJournalPath = restoreReport.JournalPath.empty()
                    ? legacyJournalPath
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
#else
                attempt =
                    PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
                        acPaths,
                        aSession.GetCampaignId(),
                        aSession.GetPlayerProfileId(),
                        transactionId,
                        workspaceCapability);
                result.RestoreAttemptStatus = attempt.Status;
                if (!attempt.IsUsable() || !attempt.State)
                {
                    result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                    return result;
                }

                result.RestoreDomain =
                    PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable;
                result.RestoreId = attempt.State->CurrentRestoreId;
                result.RestoreJournalPath = attempt.JournalPath;

                const auto prepared =
                    PartyQuestReplicaDurableRestorePreparation::PrepareAuthorized(
                        acPaths,
                        restorePlan,
                        attempt.State->CurrentRestoreId,
                        workspaceCapability);
                result.DurablePreparationStatus = prepared.Status;
                if (!prepared.IsBackupsReady())
                {
                    result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                    return result;
                }

                const auto durableReport =
                    PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
                        acPaths,
                        aSession.GetCampaignId(),
                        aSession.GetPlayerProfileId(),
                        prepared.JournalPath,
                        workspaceCapability);
                result.DurableRestoreStatus = durableReport.Status;
                result.RestoreStatus =
                    MapDurableLiveRestoreStatus(durableReport.Status);
                result.RestoreJournalPath = durableReport.JournalPath.empty()
                    ? prepared.JournalPath
                    : durableReport.JournalPath;
                if (!durableReport.IsCheckpointRestored())
                {
                    result.Status = PartyQuestRuntimeRecoveryStatus::RestoreFailed;
                    return result;
                }
#endif
            }
            else
            {
                result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
                result.RestoreStatus = loadedLegacy.Status ==
                        PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired
                    ? PartyQuestReplicaRestoreExecutionStatus::BackupRecoveryRequired
                    : PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed;
                return result;
            }
        }
        else
        {
            // Corrupt/mismatched attempt identity is authoritative local recovery
            // evidence. Never ignore it and silently create/resume legacy state.
            result.Status = PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict;
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
            result.Status = result.RestoreStatus ==
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