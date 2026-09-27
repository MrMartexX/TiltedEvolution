#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestShadowPeer.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildShadowPeerSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}};
    return snapshot;
}

RequestPartyQuestTransaction BuildShadowPeerRequest(
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
    request.Transaction.ProposedSnapshot = BuildShadowPeerSnapshot(aQuestId, aStage);
    return request;
}
} // namespace

TEST_CASE("One-PC shadow peer automatically repairs a missed update and digest divergence", "[quest.party-state.shadow-peer]")
{
    const PartyQuestCampaignId campaignId{0x1020304050607080ull, 0x90A0B0C0D0E0F001ull};
    const GameId questId(30, 0x1234);

    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(11));

    PartyQuestShadowPeerHarness shadowPeer;
    REQUIRE(shadowPeer.Start(coordinator, campaignId));
    REQUIRE(shadowPeer.GetState() == PartyQuestShadowPeerState::WaitingForBaseline);
    REQUIRE(coordinator.IsClientConnected(PartyQuestShadowPeerHarness::kClientId));

    const auto first = coordinator.HandleTransaction(
        11,
        BuildShadowPeerRequest(100, 1000, 11, questId, 0, 10));
    REQUIRE(first.Broadcast.has_value());
    shadowPeer.HandleCanonicalUpdate(coordinator, *first.Broadcast);

    REQUIRE(shadowPeer.GetState() == PartyQuestShadowPeerState::WaitingForMissedUpdate);
    REQUIRE_FALSE(coordinator.IsClientConnected(PartyQuestShadowPeerHarness::kClientId));
    REQUIRE(shadowPeer.GetMetrics().BaselineWorldRevision == 1);

    const auto second = coordinator.HandleTransaction(
        11,
        BuildShadowPeerRequest(101, 1001, 11, questId, 1, 20));
    REQUIRE(second.Broadcast.has_value());
    shadowPeer.HandleCanonicalUpdate(coordinator, *second.Broadcast);

    REQUIRE(shadowPeer.GetState() == PartyQuestShadowPeerState::Passed);
    REQUIRE(shadowPeer.GetFailure() == PartyQuestShadowPeerFailure::None);
    REQUIRE_FALSE(coordinator.IsClientConnected(PartyQuestShadowPeerHarness::kClientId));

    const auto& metrics = shadowPeer.GetMetrics();
    REQUIRE(metrics.MissedWorldRevision == 2);
    REQUIRE(metrics.FinalWorldRevision == 2);
    REQUIRE(metrics.MissedUpdateRepairSummary.RepairItemCount() == 1);
    REQUIRE(metrics.MissedUpdateRepairSummary.RevisionMismatchCount == 1);
    REQUIRE(metrics.MissedUpdateRepairSummary.MissingQuestCount == 0);
    REQUIRE(metrics.MissedUpdateRepairSummary.DigestMismatchCount == 0);
    REQUIRE(metrics.DigestRepairSummary.RepairItemCount() == 1);
    REQUIRE(metrics.DigestRepairSummary.DigestMismatchCount == 1);

    const PartyQuestState& canonical = coordinator.GetCanonicalState();
    REQUIRE(shadowPeer.GetClient().GetReplica().GetWorldRevision() == canonical.GetWorldRevision());
    REQUIRE(*shadowPeer.GetClient().GetReplica().FindQuest(questId) == *canonical.FindQuest(questId));
    REQUIRE(PartyQuestRepairPlanner::Build(
                canonical,
                shadowPeer.GetClient().GetReplica().BuildReport()).Status ==
            PartyQuestRepairPlanStatus::UpToDate);
}
