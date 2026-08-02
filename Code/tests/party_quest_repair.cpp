#include <Structs/Skyrim/PartyQuestRepair.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildRepairSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {
        {10, QuestObjectiveState::Completed},
        {20, QuestObjectiveState::Displayed}
    };
    snapshot.ReferenceAliases = {
        {1, GameId(1, 0x100), false}
    };
    return snapshot;
}

PartyQuestTransaction BuildRepairTransaction(
    uint64_t aTransactionId,
    uint32_t aInitiatorPlayerId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = aInitiatorPlayerId;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildRepairSnapshot(aQuestId, aStage);
    return transaction;
}
} // namespace

TEST_CASE("Two simulated clients converge on the canonical quest state", "[quest.party-state.repair]")
{
    const GameId questId(1, 0x1000);
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(1001, 11, questId, 0, 20)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica clientA = PartyQuestReplica::FromCanonical(server);
    PartyQuestReplica clientB = PartyQuestReplica::FromCanonical(server);

    QuestSnapshot localAdvance = *clientA.FindQuest(questId);
    localAdvance.CurrentStage = 30;
    localAdvance.CompletedStages.push_back(30);
    clientA.ObserveLocalSnapshot(localAdvance);

    PartyQuestTransaction acceptedAdvance;
    acceptedAdvance.TransactionId = 1002;
    acceptedAdvance.InitiatorPlayerId = 11;
    acceptedAdvance.QuestId = questId;
    acceptedAdvance.ExpectedQuestRevision = 1;
    acceptedAdvance.ProposedSnapshot = localAdvance;
    REQUIRE(server.Apply(acceptedAdvance).Status == PartyQuestApplyStatus::Accepted);

    const auto repairA = PartyQuestRepairPlanner::Build(server, clientA.BuildReport());
    const auto repairB = PartyQuestRepairPlanner::Build(server, clientB.BuildReport());

    REQUIRE(repairA.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(repairB.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(repairA.Items.size() == 1);
    REQUIRE(repairB.Items.size() == 1);
    REQUIRE(repairA.Items[0].Reason == PartyQuestRepairReason::RevisionMismatch);
    REQUIRE(repairB.Items[0].Reason == PartyQuestRepairReason::RevisionMismatch);

    REQUIRE(clientA.Apply(repairA) == PartyQuestReplicaApplyStatus::Applied);
    REQUIRE(clientB.Apply(repairB) == PartyQuestReplicaApplyStatus::Applied);

    const QuestSnapshot* pCanonical = server.FindQuest(questId);
    REQUIRE(pCanonical != nullptr);
    REQUIRE(clientA.FindQuest(questId) != nullptr);
    REQUIRE(clientB.FindQuest(questId) != nullptr);
    REQUIRE(*clientA.FindQuest(questId) == *pCanonical);
    REQUIRE(*clientB.FindQuest(questId) == *pCanonical);
    REQUIRE(clientA.GetWorldRevision() == server.GetWorldRevision());
    REQUIRE(clientB.GetWorldRevision() == server.GetWorldRevision());

    REQUIRE(PartyQuestRepairPlanner::Build(server, clientA.BuildReport()).Status == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(PartyQuestRepairPlanner::Build(server, clientB.BuildReport()).Status == PartyQuestRepairPlanStatus::UpToDate);
}

TEST_CASE("Repair planner detects hidden same-revision digest divergence", "[quest.party-state.repair]")
{
    const GameId questId(2, 0x2000);
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(2001, 5, questId, 0, 20)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica client = PartyQuestReplica::FromCanonical(server);
    QuestSnapshot divergent = *client.FindQuest(questId);
    divergent.ReferenceAliases[0].ReferenceId = GameId(9, 0x999);
    client.ObserveLocalSnapshot(divergent);

    const auto plan = PartyQuestRepairPlanner::Build(server, client.BuildReport());
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(plan.Items.size() == 1);
    REQUIRE(plan.Items[0].Reason == PartyQuestRepairReason::DigestMismatch);
    REQUIRE(client.Apply(plan) == PartyQuestReplicaApplyStatus::Applied);
    REQUIRE(*client.FindQuest(questId) == *server.FindQuest(questId));
}

TEST_CASE("Repair planner supplies campaign quests missing from a client", "[quest.party-state.repair]")
{
    const GameId questId(3, 0x3000);
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(3001, 8, questId, 0, 40)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplicaReport emptyClient;
    emptyClient.WorldRevision = 0;

    const auto plan = PartyQuestRepairPlanner::Build(server, emptyClient);
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(plan.Items.size() == 1);
    REQUIRE(plan.Items[0].Reason == PartyQuestRepairReason::MissingQuest);
    REQUIRE(plan.Items[0].CanonicalSnapshot.QuestId == questId);
}

TEST_CASE("Repair planner refuses a replica ahead of the server", "[quest.party-state.repair]")
{
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(4001, 4, GameId(4, 0x4000), 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica replica = PartyQuestReplica::FromCanonical(server);
    replica.SetObservedWorldRevision(server.GetWorldRevision() + 1);

    const auto plan = PartyQuestRepairPlanner::Build(server, replica.BuildReport());
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::ClientAhead);
    REQUIRE(plan.Items.empty());
    REQUIRE(replica.Apply(plan) == PartyQuestReplicaApplyStatus::ClientAhead);
}

TEST_CASE("Client-only quests do not enter the shared campaign repair plan", "[quest.party-state.repair]")
{
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(5001, 2, GameId(5, 0x5000), 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica replica = PartyQuestReplica::FromCanonical(server);
    QuestSnapshot personalQuest = BuildRepairSnapshot(GameId(99, 0xDEAD), 80);
    personalQuest.Revision = 1;
    replica.ObserveLocalSnapshot(personalQuest);

    const auto plan = PartyQuestRepairPlanner::Build(server, replica.BuildReport());
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(plan.Items.empty());
    REQUIRE(replica.GetQuestCount() == 2);
}

TEST_CASE("Repair items are ordered deterministically by stable quest id", "[quest.party-state.repair]")
{
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(6001, 1, GameId(5, 0x200), 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(server.Apply(BuildRepairTransaction(6002, 1, GameId(1, 0x900), 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(server.Apply(BuildRepairTransaction(6003, 1, GameId(1, 0x100), 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    const auto plan = PartyQuestRepairPlanner::Build(server, PartyQuestReplicaReport{});
    REQUIRE(plan.Items.size() == 3);
    REQUIRE(plan.Items[0].CanonicalSnapshot.QuestId == GameId(1, 0x100));
    REQUIRE(plan.Items[1].CanonicalSnapshot.QuestId == GameId(1, 0x900));
    REQUIRE(plan.Items[2].CanonicalSnapshot.QuestId == GameId(5, 0x200));
}

TEST_CASE("Replica rejects a repair plan built for an older local report", "[quest.party-state.repair]")
{
    PartyQuestState server;
    const GameId questId(7, 0x7000);
    REQUIRE(server.Apply(BuildRepairTransaction(7001, 3, questId, 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica replica;
    const auto plan = PartyQuestRepairPlanner::Build(server, replica.BuildReport());
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::RepairRequired);

    replica.SetObservedWorldRevision(1);
    REQUIRE(replica.Apply(plan) == PartyQuestReplicaApplyStatus::StalePlan);
    REQUIRE(replica.FindQuest(questId) == nullptr);
}

TEST_CASE("Repair can advance only replica metadata when quest digests already match", "[quest.party-state.repair]")
{
    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(8001, 6, GameId(8, 0x8000), 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica replica = PartyQuestReplica::FromCanonical(server);
    replica.SetObservedWorldRevision(0);

    const auto plan = PartyQuestRepairPlanner::Build(server, replica.BuildReport());
    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(plan.Items.empty());
    REQUIRE(replica.Apply(plan) == PartyQuestReplicaApplyStatus::Applied);
    REQUIRE(replica.GetWorldRevision() == server.GetWorldRevision());
}

TEST_CASE("Repair summary separates missing revision digest and client-only divergence", "[quest.party-state.repair]")
{
    const GameId revisionQuest(10, 0x100);
    const GameId digestQuest(10, 0x200);
    const GameId missingQuest(10, 0x300);
    const GameId clientOnlyQuest(99, 0xDEAD);

    PartyQuestState server;
    REQUIRE(server.Apply(BuildRepairTransaction(9001, 1, revisionQuest, 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(server.Apply(BuildRepairTransaction(9002, 1, digestQuest, 0, 20)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(server.Apply(BuildRepairTransaction(9003, 1, missingQuest, 0, 30)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplicaReport report;
    report.WorldRevision = server.GetWorldRevision();
    report.Quests.emplace(revisionQuest, PartyQuestReplicaEntry{0, server.FindQuest(revisionQuest)->ComputeDigest()});
    report.Quests.emplace(digestQuest, PartyQuestReplicaEntry{1, server.FindQuest(digestQuest)->ComputeDigest() ^ 1ull});
    report.Quests.emplace(clientOnlyQuest, PartyQuestReplicaEntry{1, 0x1234});

    const auto plan = PartyQuestRepairPlanner::Build(server, report);
    const auto summary = PartyQuestRepairPlanner::Summarize(server, report, plan);

    REQUIRE(plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(plan.Items.size() == 3);
    REQUIRE(summary.MissingQuestCount == 1);
    REQUIRE(summary.RevisionMismatchCount == 1);
    REQUIRE(summary.DigestMismatchCount == 1);
    REQUIRE(summary.ClientOnlyQuestCount == 1);
    REQUIRE(summary.RepairItemCount() == plan.Items.size());
}
