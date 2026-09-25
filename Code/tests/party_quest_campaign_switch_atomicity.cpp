#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildCampaignAtomicitySnapshot(
    GameId aQuestId,
    uint16_t aStage,
    uint64_t aRevision,
    uint32_t aInitiator)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = aRevision;
    snapshot.InitiatorPlayerId = aInitiator;
    snapshot.CompletedStages = {aStage};
    snapshot.Canonicalize();
    return snapshot;
}

NotifyPartyQuestRepairPlan BuildInitialCampaignPlan(
    const PartyQuestCampaignId& acCampaignId,
    GameId aQuestId)
{
    NotifyPartyQuestRepairPlan plan;
    plan.ReportId = 1;
    plan.PlanId = 1;
    plan.CampaignId = acCampaignId;
    plan.Plan.Status = PartyQuestRepairPlanStatus::RepairRequired;
    plan.Plan.BaseClientWorldRevision = 0;
    plan.Plan.TargetWorldRevision = 1;
    plan.Plan.Items.push_back({
        PartyQuestRepairReason::MissingQuest,
        BuildCampaignAtomicitySnapshot(aQuestId, 10, 1, 1)});
    return plan;
}
} // namespace

TEST_CASE("Rejected campaign candidate preserves the published client session", "[quest.party-state.coordinator.reconnect][campaign-atomicity]")
{
    const PartyQuestCampaignId campaignA{0xA1A1, 0xB1B1};
    const PartyQuestCampaignId campaignB{0xA2A2, 0xB2B2};
    const GameId questA(21, 0x1100);
    const GameId questB(22, 0x2200);

    PartyQuestClientSession client(1);
    const auto initialPlan = BuildInitialCampaignPlan(campaignA, questA);
    const auto initial = client.HandleRepairPlan(initialPlan);
    REQUIRE(initial.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE_FALSE(initial.CampaignChanged);
    REQUIRE(client.GetCampaignId() == campaignA);

    NotifyPartyQuestCanonicalUpdate update;
    update.TransactionId = 55;
    update.WorldRevision = 2;
    update.InitiatorPlayerId = 7;
    update.CanonicalSnapshot =
        BuildCampaignAtomicitySnapshot(questA, 20, 2, update.InitiatorPlayerId);
    REQUIRE(client.HandleCanonicalUpdate(update) ==
        PartyQuestClientCanonicalStatus::Applied);

    const PartyQuestReplicaReport publishedBefore = client.GetReplica().BuildReport();
    REQUIRE(publishedBefore.WorldRevision == 2);
    REQUIRE(client.GetReplica().FindQuest(questA));

    NotifyPartyQuestRepairPlan invalidSwitch =
        BuildInitialCampaignPlan(campaignB, questB);
    invalidSwitch.ReportId = 2;
    invalidSwitch.PlanId = 1; // Reuse is valid only if the new campaign commits.
    invalidSwitch.Plan.Items.push_back(invalidSwitch.Plan.Items.front());

    const auto rejected = client.HandleRepairPlan(invalidSwitch);
    REQUIRE(rejected.Status == PartyQuestClientRepairStatus::InvalidPlan);
    REQUIRE_FALSE(rejected.CampaignChanged);
    REQUIRE(client.GetCampaignId() == campaignA);
    REQUIRE(client.GetReplica().BuildReport() == publishedBefore);
    REQUIRE(client.GetReplica().FindQuest(questA));
    REQUIRE_FALSE(client.GetReplica().FindQuest(questB));

    // Both old campaign caches must still be authoritative after rejection.
    REQUIRE(client.HandleRepairPlan(initialPlan).Status ==
        PartyQuestClientRepairStatus::Duplicate);
    REQUIRE(client.HandleCanonicalUpdate(update) ==
        PartyQuestClientCanonicalStatus::Duplicate);

    // The same server-local PlanId may then be reused by a valid new campaign;
    // old cache identity must not create a false cross-campaign conflict.
    NotifyPartyQuestRepairPlan validSwitch =
        BuildInitialCampaignPlan(campaignB, questB);
    validSwitch.ReportId = 3;
    validSwitch.PlanId = 1;

    const auto accepted = client.HandleRepairPlan(validSwitch);
    REQUIRE(accepted.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE(accepted.CampaignChanged);
    REQUIRE(client.GetCampaignId() == campaignB);
    REQUIRE(client.GetReplica().GetWorldRevision() == 1);
    REQUIRE_FALSE(client.GetReplica().FindQuest(questA));
    REQUIRE(client.GetReplica().FindQuest(questB));
    REQUIRE(client.HandleRepairPlan(validSwitch).Status ==
        PartyQuestClientRepairStatus::Duplicate);
}
