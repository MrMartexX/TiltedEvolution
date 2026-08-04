#include <Structs/Skyrim/PartyQuestRuntimeCheckpoint.h>

namespace
{
PartyQuestRuntimeCheckpointResult MakeResult(
    PartyQuestRuntimeCheckpointStatus aStatus,
    const PartyQuestRuntimeApplyEntry* apActive,
    const std::filesystem::path& acManifestPath = {})
{
    PartyQuestRuntimeCheckpointResult result;
    result.Status = aStatus;
    result.ManifestPath = acManifestPath;
    if (apActive)
    {
        result.TransactionId = apActive->TransactionId;
        result.TargetWorldRevision = apActive->TargetWorldRevision;
    }
    return result;
}

bool IsCheckpointReadyState(const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acActive.State == PartyQuestRuntimeApplyState::ReadyToApply &&
        acActive.SaveGuardActive &&
        acActive.CheckpointCreated &&
        !acActive.RuntimeMutationMayHaveOccurred;
}

bool IsAwaitingCheckpointState(const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acActive.State == PartyQuestRuntimeApplyState::AwaitingCheckpoint &&
        acActive.SaveGuardActive &&
        !acActive.CheckpointCreated &&
        !acActive.RuntimeMutationMayHaveOccurred;
}
} // namespace

PartyQuestRuntimeCheckpointResult
PartyQuestRuntimeCheckpointCoordinator::EnsurePreRepairCheckpoint(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acCheckpointPlan) noexcept
{
    try
    {
        if (!aSession.GetCampaignId().IsValid() ||
            !aSession.GetPlayerProfileId().IsValid())
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidIdentity,
                aSession.GetCoordinator().GetActive());
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidLayout,
                aSession.GetCoordinator().GetActive());
        }

        const PartyQuestRuntimeApplyEntry* pActive =
            aSession.GetCoordinator().GetActive();
        if (!pActive ||
            pActive->TransactionId == 0 ||
            pActive->TargetWorldRevision == 0)
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState,
                pActive);
        }

        const uint64_t transactionId = pActive->TransactionId;
        const uint64_t targetWorldRevision = pActive->TargetWorldRevision;
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);

        PartyQuestReplicaSnapshotManager manager(
            acPaths,
            aSession.GetCampaignId(),
            aSession.GetPlayerProfileId());

        if (IsCheckpointReadyState(*pActive))
        {
            PartyQuestRuntimeCheckpointResult result = MakeResult(
                PartyQuestRuntimeCheckpointStatus::AlreadyReady,
                pActive,
                manifestPath);
            const auto validation = manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                targetWorldRevision);
            result.SnapshotStatus = validation.Status;
            if (!validation.IsReady())
                result.Status = PartyQuestRuntimeCheckpointStatus::SnapshotFailed;
            return result;
        }

        if (!IsAwaitingCheckpointState(*pActive))
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState,
                pActive,
                manifestPath);
        }

        if (!acCheckpointPlan.IsReady())
        {
            return MakeResult(
                PartyQuestRuntimeCheckpointStatus::InvalidCheckpointPlan,
                pActive,
                manifestPath);
        }

        const auto snapshot = manager.EnsureRevisionCheckpoint(
            PartyQuestCheckpointKind::PreRepair,
            targetWorldRevision,
            acCheckpointPlan);

        PartyQuestRuntimeCheckpointResult result = MakeResult(
            snapshot.Status == PartyQuestReplicaSnapshotStatus::AlreadyReady
                ? PartyQuestRuntimeCheckpointStatus::AlreadyReady
                : PartyQuestRuntimeCheckpointStatus::Ready,
            pActive,
            manifestPath);
        result.SnapshotStatus = snapshot.Status;

        if (!snapshot.IsReady())
        {
            result.Status = snapshot.Status == PartyQuestReplicaSnapshotStatus::InvalidPlan
                ? PartyQuestRuntimeCheckpointStatus::InvalidCheckpointPlan
                : PartyQuestRuntimeCheckpointStatus::SnapshotFailed;
            return result;
        }

        result.RuntimeTransition = aSession.MarkCheckpointCreated(transactionId);
        switch (result.RuntimeTransition)
        {
        case PartyQuestRuntimeDurableTransitionStatus::Applied:
            break;

        case PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure:
            result.Status = PartyQuestRuntimeCheckpointStatus::RuntimeStatePersistenceFailed;
            break;

        case PartyQuestRuntimeDurableTransitionStatus::InvalidState:
        case PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired:
            result.Status = PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState;
            break;
        }

        return result;
    }
    catch (...)
    {
        return MakeResult(
            PartyQuestRuntimeCheckpointStatus::SnapshotFailed,
            aSession.GetCoordinator().GetActive());
    }
}
