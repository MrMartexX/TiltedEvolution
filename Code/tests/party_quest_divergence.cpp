#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildDivergenceSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}};
    return snapshot;
}

RequestPartyQuestTransaction BuildDivergenceRequest(
    uint64_t aRequestId,
    uint64_t aTransactionId,
    uint32_t aClientId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    RequestPartyQuestTransaction request;
    request.RequestId = aRequestId;
    request.Transaction.TransactionId = aTransactionId;
    request.Transaction.InitiatorPlayerId = aClientId;
    request.Transaction.QuestId = aQuestId;
    request.Transaction.ExpectedQuestRevision = aExpectedRevision;
    request.Transaction.ProposedSnapshot = BuildDivergenceSnapshot(aQuestId, aStage);
    return request;
}
} // namespace

TEST_CASE("Two connected clients repair independent digest and revision divergence", "[quest.party-state.divergence]")
{
    const PartyQuestCampaignId campaignId{0xA0A0A0A0A0A0A0A0ull, 0xB0B0B0B0B0B0B0B0ull};
    const GameId questA(20, 0x1000);
    const GameId questB(20, 0x2000);
    const GameId clientOnlyQuest(99, 0xDEAD);

    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(11));
    REQUIRE(coordinator.ConnectClient(22));

    PartyQuestClientSession clientA(11);
    PartyQuestClientSession clientB(22);

    const auto first = coordinator.HandleTransaction(
        11,
        BuildDivergenceRequest(100, 1000, 11, questA, 0, 10));
    REQUIRE(first.Broadcast.has_value());
    REQUIRE(clientA.HandleCanonicalUpdate(*first.Broadcast) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(clientB.HandleCanonicalUpdate(*first.Broadcast) == PartyQuestClientCanonicalStatus::Applied);

    const auto second = coordinator.HandleTransaction(
        11,
        BuildDivergenceRequest(101, 1001, 11, questB, 0, 10));
    REQUIRE(second.Broadcast.has_value());
    REQUIRE(clientA.HandleCanonicalUpdate(*second.Broadcast) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(clientB.HandleCanonicalUpdate(*second.Broadcast) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 2);

    // Client A keeps the correct revision metadata but silently diverges in the
    // quest payload. This must be detected by the digest, not the revision.
    QuestSnapshot digestDivergence = *clientA.GetReplica().FindQuest(questA);
    digestDivergence.CurrentStage = 15;
    clientA.GetReplica().ObserveLocalSnapshot(digestDivergence);

    // The server then advances quest B. Deliver the canonical broadcast only to
    // client A to model a lost update for client B.
    const auto third = coordinator.HandleTransaction(
        11,
        BuildDivergenceRequest(102, 1002, 11, questB, 1, 30));
    REQUIRE(third.Broadcast.has_value());
    REQUIRE(clientA.HandleCanonicalUpdate(*third.Broadcast) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(clientA.GetReplica().GetWorldRevision() == 3);
    REQUIRE(clientB.GetReplica().GetWorldRevision() == 2);

    // A client-only quest must survive repair and must not be admitted into the
    // shared campaign merely because it appears in a replica report.
    QuestSnapshot localOnly = BuildDivergenceSnapshot(clientOnlyQuest, 80);
    localOnly.Revision = 1;
    clientB.GetReplica().ObserveLocalSnapshot(localOnly);

    // The same ReportId is valid in separate authenticated client sessions.
    const auto reportA = clientA.BuildReplicaReport(7000, false);
    const auto reportB = clientB.BuildReplicaReport(7000, false);
    const auto planA = coordinator.HandleReplicaReport(11, reportA);
    const auto planB = coordinator.HandleReplicaReport(22, reportB);

    REQUIRE(planA.Status == PartyQuestReportHandleStatus::Generated);
    REQUIRE(planB.Status == PartyQuestReportHandleStatus::Generated);
    REQUIRE(planA.Response.has_value());
    REQUIRE(planB.Response.has_value());
    REQUIRE(planA.Response->PlanId != planB.Response->PlanId);

    const auto summaryA = PartyQuestRepairPlanner::Summarize(
        coordinator.GetCanonicalState(), reportA.Report, planA.Response->Plan);
    const auto summaryB = PartyQuestRepairPlanner::Summarize(
        coordinator.GetCanonicalState(), reportB.Report, planB.Response->Plan);

    REQUIRE(summaryA.MissingQuestCount == 0);
    REQUIRE(summaryA.RevisionMismatchCount == 0);
    REQUIRE(summaryA.DigestMismatchCount == 1);
    REQUIRE(summaryA.ClientOnlyQuestCount == 0);
    REQUIRE(planA.Response->Plan.Items.size() == 1);
    REQUIRE(planA.Response->Plan.Items.front().Reason == PartyQuestRepairReason::DigestMismatch);

    REQUIRE(summaryB.MissingQuestCount == 0);
    REQUIRE(summaryB.RevisionMismatchCount == 1);
    REQUIRE(summaryB.DigestMismatchCount == 0);
    REQUIRE(summaryB.ClientOnlyQuestCount == 1);
    REQUIRE(planB.Response->Plan.Items.size() == 1);
    REQUIRE(planB.Response->Plan.Items.front().Reason == PartyQuestRepairReason::RevisionMismatch);

    NotifyPartyQuestRepairPlan deliveredA = *planA.Response;
    deliveredA.CampaignId = campaignId;
    const auto repairA = clientA.HandleRepairPlan(deliveredA);
    REQUIRE(repairA.Status == PartyQuestClientRepairStatus::Applied);

    // Plans are correlated per authenticated session. A valid ACK from client A
    // cannot be replayed as client B's repair acknowledgement.
    const auto crossedAck = coordinator.HandleRepairAck(22, repairA.Ack);
    REQUIRE(crossedAck.Status == PartyQuestAckHandleStatus::UnknownPlan);
    REQUIRE(coordinator.HandleRepairAck(11, repairA.Ack).Status == PartyQuestAckHandleStatus::Verified);

    NotifyPartyQuestRepairPlan deliveredB = *planB.Response;
    deliveredB.CampaignId = campaignId;
    const auto repairB = clientB.HandleRepairPlan(deliveredB);
    REQUIRE(repairB.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE(coordinator.HandleRepairAck(22, repairB.Ack).Status == PartyQuestAckHandleStatus::Verified);

    const PartyQuestState& canonical = coordinator.GetCanonicalState();
    REQUIRE(clientA.GetReplica().GetWorldRevision() == canonical.GetWorldRevision());
    REQUIRE(clientB.GetReplica().GetWorldRevision() == canonical.GetWorldRevision());
    REQUIRE(*clientA.GetReplica().FindQuest(questA) == *canonical.FindQuest(questA));
    REQUIRE(*clientA.GetReplica().FindQuest(questB) == *canonical.FindQuest(questB));
    REQUIRE(*clientB.GetReplica().FindQuest(questA) == *canonical.FindQuest(questA));
    REQUIRE(*clientB.GetReplica().FindQuest(questB) == *canonical.FindQuest(questB));

    REQUIRE(clientB.GetReplica().FindQuest(clientOnlyQuest) != nullptr);
    REQUIRE(canonical.FindQuest(clientOnlyQuest) == nullptr);
    REQUIRE(PartyQuestRepairPlanner::Build(canonical, clientA.GetReplica().BuildReport()).Status ==
            PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(PartyQuestRepairPlanner::Build(canonical, clientB.GetReplica().BuildReport()).Status ==
            PartyQuestRepairPlanStatus::UpToDate);
}
