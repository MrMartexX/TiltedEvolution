#include <Structs/Skyrim/PartyQuestShadowPeer.h>

#include <limits>

bool PartyQuestShadowPeerHarness::Synchronize(
    PartyQuestProtocolCoordinator& aCoordinator,
    bool aReconnect,
    ExpectedRepair aExpectedRepair,
    PartyQuestRepairSummary& aSummary)
{
    RequestPartyQuestReplicaReport request = m_client.BuildReplicaReport(m_nextReportId++, aReconnect);
    request.CampaignId = m_campaignId;

    auto dispatch = aCoordinator.HandleReplicaReport(kClientId, request);
    if (dispatch.Status != PartyQuestReportHandleStatus::Generated || !dispatch.Response)
        return false;

    NotifyPartyQuestRepairPlan plan = *dispatch.Response;
    plan.CampaignId = m_campaignId;
    aSummary = PartyQuestRepairPlanner::Summarize(
        aCoordinator.GetCanonicalState(),
        request.Report,
        plan.Plan);

    if (aExpectedRepair == ExpectedRepair::MissedUpdate)
    {
        const size_t missedRepairCount =
            aSummary.MissingQuestCount + aSummary.RevisionMismatchCount;
        if (aSummary.RepairItemCount() == 0 || missedRepairCount == 0 ||
            aSummary.DigestMismatchCount != 0)
        {
            return false;
        }
    }
    else if (aExpectedRepair == ExpectedRepair::DigestMismatch)
    {
        if (aSummary.RepairItemCount() != 1 || aSummary.DigestMismatchCount != 1 ||
            aSummary.MissingQuestCount != 0 || aSummary.RevisionMismatchCount != 0)
        {
            return false;
        }
    }

    const PartyQuestClientRepairResult repair = m_client.HandleRepairPlan(plan);
    if (repair.Status != PartyQuestClientRepairStatus::Applied &&
        repair.Status != PartyQuestClientRepairStatus::NoChanges)
    {
        return false;
    }

    const PartyQuestAckResult ack = aCoordinator.HandleRepairAck(kClientId, repair.Ack);
    if (ack.Status != PartyQuestAckHandleStatus::Verified ||
        ack.VerificationStatus != PartyQuestRepairPlanStatus::UpToDate)
    {
        return false;
    }

    return m_client.GetReplica().GetWorldRevision() ==
        aCoordinator.GetCanonicalState().GetWorldRevision();
}

void PartyQuestShadowPeerHarness::Fail(PartyQuestShadowPeerFailure aFailure) noexcept
{
    m_failure = aFailure;
    m_state = PartyQuestShadowPeerState::Failed;
}

bool PartyQuestShadowPeerHarness::Start(
    PartyQuestProtocolCoordinator& aCoordinator,
    const PartyQuestCampaignId& acCampaignId)
{
    if (m_state != PartyQuestShadowPeerState::Idle)
        return m_state != PartyQuestShadowPeerState::Failed;

    if (!acCampaignId.IsValid())
    {
        Fail(PartyQuestShadowPeerFailure::InvalidCampaign);
        return false;
    }

    m_campaignId = acCampaignId;
    m_metrics.StartWorldRevision = aCoordinator.GetCanonicalState().GetWorldRevision();

    if (!aCoordinator.ConnectClient(kClientId))
    {
        Fail(PartyQuestShadowPeerFailure::ConnectFailed);
        return false;
    }

    if (!Synchronize(
            aCoordinator,
            false,
            ExpectedRepair::Any,
            m_metrics.InitialSyncSummary))
    {
        aCoordinator.DisconnectClient(kClientId);
        Fail(PartyQuestShadowPeerFailure::InitialSyncFailed);
        return false;
    }

    m_state = PartyQuestShadowPeerState::WaitingForBaseline;
    return true;
}

void PartyQuestShadowPeerHarness::HandleCanonicalUpdate(
    PartyQuestProtocolCoordinator& aCoordinator,
    const NotifyPartyQuestCanonicalUpdate& acUpdate)
{
    if (m_state == PartyQuestShadowPeerState::WaitingForBaseline)
    {
        const PartyQuestClientCanonicalStatus status = m_client.HandleCanonicalUpdate(acUpdate);
        if (status != PartyQuestClientCanonicalStatus::Applied)
        {
            aCoordinator.DisconnectClient(kClientId);
            Fail(PartyQuestShadowPeerFailure::BaselineApplyFailed);
            return;
        }

        m_metrics.BaselineWorldRevision = acUpdate.WorldRevision;
        if (!aCoordinator.DisconnectClient(kClientId))
        {
            Fail(PartyQuestShadowPeerFailure::DisconnectFailed);
            return;
        }

        m_state = PartyQuestShadowPeerState::WaitingForMissedUpdate;
        return;
    }

    if (m_state != PartyQuestShadowPeerState::WaitingForMissedUpdate)
        return;

    // Deliberately do not apply this broadcast. The synthetic client was
    // disconnected before the transaction, so its replica is now stale.
    m_metrics.MissedWorldRevision = acUpdate.WorldRevision;

    if (!aCoordinator.ConnectClient(kClientId))
    {
        Fail(PartyQuestShadowPeerFailure::ReconnectFailed);
        return;
    }

    if (!Synchronize(
            aCoordinator,
            true,
            ExpectedRepair::MissedUpdate,
            m_metrics.MissedUpdateRepairSummary))
    {
        aCoordinator.DisconnectClient(kClientId);
        Fail(PartyQuestShadowPeerFailure::MissedUpdateRepairFailed);
        return;
    }

    const QuestSnapshot* pReplicaSnapshot =
        m_client.GetReplica().FindQuest(acUpdate.CanonicalSnapshot.QuestId);
    if (!pReplicaSnapshot)
    {
        aCoordinator.DisconnectClient(kClientId);
        Fail(PartyQuestShadowPeerFailure::DigestMutationFailed);
        return;
    }

    QuestSnapshot divergentSnapshot = *pReplicaSnapshot;
    const uint64_t originalDigest = divergentSnapshot.ComputeDigest();
    if (divergentSnapshot.CurrentStage == std::numeric_limits<uint16_t>::max())
        --divergentSnapshot.CurrentStage;
    else
        ++divergentSnapshot.CurrentStage;

    if (divergentSnapshot.ComputeDigest() == originalDigest)
    {
        aCoordinator.DisconnectClient(kClientId);
        Fail(PartyQuestShadowPeerFailure::DigestMutationFailed);
        return;
    }

    // Keep the same world/quest revision while changing only payload content.
    // The following report therefore has to detect divergence by digest.
    m_client.GetReplica().ObserveLocalSnapshot(std::move(divergentSnapshot));

    if (!Synchronize(
            aCoordinator,
            false,
            ExpectedRepair::DigestMismatch,
            m_metrics.DigestRepairSummary))
    {
        aCoordinator.DisconnectClient(kClientId);
        Fail(PartyQuestShadowPeerFailure::DigestRepairFailed);
        return;
    }

    m_metrics.FinalWorldRevision = m_client.GetReplica().GetWorldRevision();
    aCoordinator.DisconnectClient(kClientId);
    m_state = PartyQuestShadowPeerState::Passed;
}
