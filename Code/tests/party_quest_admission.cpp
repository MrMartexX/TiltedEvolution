#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestAdmission.h>
#include <Structs/Skyrim/PartyQuestProtocol.h>
#include <Structs/Skyrim/PartyQuestRepair.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildAdmissionSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {aStage};
    snapshot.Objectives = {{aStage, QuestObjectiveState::Displayed}};
    return snapshot;
}

PartyQuestTransaction BuildAdmissionTransaction(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = 7;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildAdmissionSnapshot(aQuestId, aStage);
    return transaction;
}
} // namespace

TEST_CASE("Party quest admission blocks service and local-only observations conservatively", "[quest.party-state.admission]")
{
    const GameId ordinaryQuest(0, 0x010000);

    PartyQuestSyncFacts hiddenUntyped;
    hiddenUntyped.QuestType = 0;
    hiddenUntyped.HasStages = true;
    const auto hiddenDecision = PartyQuestAdmissionPolicy::Evaluate(ordinaryQuest, hiddenUntyped);
    REQUIRE(hiddenDecision.Status == PartyQuestAdmissionStatus::BlockedServiceCandidate);
    REQUIRE(hiddenDecision.Classification.Class == PartyQuestSyncClass::ServiceCandidate);

    PartyQuestSyncFacts noStages;
    noStages.QuestType = 4;
    noStages.HasStages = false;
    noStages.IsDisplayedInHud = true;
    noStages.HasDisplayName = true;
    const auto localDecision = PartyQuestAdmissionPolicy::Evaluate(ordinaryQuest, noStages);
    REQUIRE(localDecision.Status == PartyQuestAdmissionStatus::BlockedLocalOnly);
    REQUIRE(localDecision.Classification.Class == PartyQuestSyncClass::LocalOnly);
}

TEST_CASE("User-facing and gameplay quests remain provisional instead of runtime-verified", "[quest.party-state.admission]")
{
    const GameId ordinaryQuest(0, 0x020000);

    PartyQuestSyncFacts userFacingMisc;
    userFacingMisc.QuestType = 6;
    userFacingMisc.HasStages = true;
    userFacingMisc.HasDisplayName = true;
    const auto miscDecision = PartyQuestAdmissionPolicy::Evaluate(ordinaryQuest, userFacingMisc);
    REQUIRE(miscDecision.Status == PartyQuestAdmissionStatus::SharedProvisional);
    REQUIRE(miscDecision.IsAdmitted());
    REQUIRE(miscDecision.Classification.Reason == PartyQuestSyncReason::UserFacingMiscellaneous);

    PartyQuestSyncFacts gameplayControllerLike;
    gameplayControllerLike.QuestType = 9;
    gameplayControllerLike.HasStages = true;
    const auto gameplayDecision = PartyQuestAdmissionPolicy::Evaluate(ordinaryQuest, gameplayControllerLike);
    REQUIRE(gameplayDecision.Status == PartyQuestAdmissionStatus::SharedProvisional);
    REQUIRE(gameplayDecision.IsAdmitted());
    REQUIRE(gameplayDecision.Classification.Reason == PartyQuestSyncReason::GameplayQuestType);
}

TEST_CASE("Confirmed service identity overrides spoofed user-facing facts", "[quest.party-state.admission]")
{
    const GameId knownServiceQuest(0, 0x000C7919); // WIGreeting

    PartyQuestSyncFacts spoofedSharedFacts;
    spoofedSharedFacts.QuestType = 4;
    spoofedSharedFacts.HasStages = true;
    spoofedSharedFacts.IsDisplayedInHud = true;
    spoofedSharedFacts.HasDisplayName = true;

    const auto decision = PartyQuestAdmissionPolicy::Evaluate(knownServiceQuest, spoofedSharedFacts);
    REQUIRE(decision.Status == PartyQuestAdmissionStatus::BlockedConfirmedServiceQuest);
    REQUIRE_FALSE(decision.IsAdmitted());
    REQUIRE(PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(knownServiceQuest));
}

TEST_CASE("Legacy service quests are quarantined from canonical repair without rewriting history", "[quest.party-state.admission]")
{
    const GameId sharedQuest(0, 0x010100);
    const GameId serviceQuest(0, 0x000F9075); // CRHoldExpansion

    PartyQuestState historicalState;
    REQUIRE(historicalState.Apply(BuildAdmissionTransaction(1001, sharedQuest, 0, 10)).Status ==
        PartyQuestApplyStatus::Accepted);
    REQUIRE(historicalState.Apply(BuildAdmissionTransaction(1002, serviceQuest, 0, 20)).Status ==
        PartyQuestApplyStatus::Accepted);

    REQUIRE(historicalState.GetWorldRevision() == 2);
    REQUIRE(historicalState.GetQuestCount() == 2);
    REQUIRE(historicalState.GetJournal().size() == 2);
    REQUIRE(historicalState.FindQuest(serviceQuest) != nullptr);

    PartyQuestReplica freshReplica = PartyQuestReplica::FromCanonical(historicalState);
    REQUIRE(freshReplica.GetWorldRevision() == historicalState.GetWorldRevision());
    REQUIRE(freshReplica.FindQuest(sharedQuest) != nullptr);
    REQUIRE(freshReplica.FindQuest(serviceQuest) == nullptr);
    REQUIRE(freshReplica.GetQuestCount() == 1);

    const auto freshVerification = PartyQuestRepairPlanner::Build(historicalState, freshReplica.BuildReport());
    REQUIRE(freshVerification.Status == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(freshVerification.Items.empty());
    REQUIRE(freshVerification.RemovedQuestIds.empty());

    PartyQuestReplica oldReplica;
    oldReplica.ObserveLocalSnapshot(*historicalState.FindQuest(sharedQuest));
    oldReplica.ObserveLocalSnapshot(*historicalState.FindQuest(serviceQuest));
    oldReplica.SetObservedWorldRevision(historicalState.GetWorldRevision());

    const auto migrationPlan = PartyQuestRepairPlanner::Build(historicalState, oldReplica.BuildReport());
    const auto summary = PartyQuestRepairPlanner::Summarize(
        historicalState, oldReplica.BuildReport(), migrationPlan);

    REQUIRE(migrationPlan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(migrationPlan.Items.empty());
    REQUIRE(migrationPlan.RemovedQuestIds == std::vector<GameId>{serviceQuest});
    REQUIRE(summary.QuarantinedQuestRemovalCount == 1);
    REQUIRE(summary.ClientOnlyQuestCount == 0);

    REQUIRE(oldReplica.Apply(migrationPlan) == PartyQuestReplicaApplyStatus::Applied);
    REQUIRE(oldReplica.GetWorldRevision() == historicalState.GetWorldRevision());
    REQUIRE(oldReplica.FindQuest(sharedQuest) != nullptr);
    REQUIRE(oldReplica.FindQuest(serviceQuest) == nullptr);

    const auto postMigration = PartyQuestRepairPlanner::Build(historicalState, oldReplica.BuildReport());
    REQUIRE(postMigration.Status == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(postMigration.Items.empty());
    REQUIRE(postMigration.RemovedQuestIds.empty());

    // Migration is logical/quarantine-only: canonical history remains intact for
    // deterministic replay, transaction idempotency, checkpoints and recovery.
    REQUIRE(historicalState.GetWorldRevision() == 2);
    REQUIRE(historicalState.GetQuestCount() == 2);
    REQUIRE(historicalState.GetJournal().size() == 2);
}

TEST_CASE("Quarantine repair preserves unrelated client-only quests", "[quest.party-state.admission]")
{
    const GameId sharedQuest(0, 0x010200);
    const GameId serviceQuest(2, 0x00012F92); // DLC1ScrollHandlingChangeLoc
    const GameId personalQuest(99, 0x00DEAD);

    PartyQuestState historicalState;
    REQUIRE(historicalState.Apply(BuildAdmissionTransaction(2001, sharedQuest, 0, 10)).Status ==
        PartyQuestApplyStatus::Accepted);
    REQUIRE(historicalState.Apply(BuildAdmissionTransaction(2002, serviceQuest, 0, 20)).Status ==
        PartyQuestApplyStatus::Accepted);

    PartyQuestReplica oldReplica;
    oldReplica.ObserveLocalSnapshot(*historicalState.FindQuest(sharedQuest));
    oldReplica.ObserveLocalSnapshot(*historicalState.FindQuest(serviceQuest));
    QuestSnapshot personal = BuildAdmissionSnapshot(personalQuest, 30);
    personal.Revision = 1;
    oldReplica.ObserveLocalSnapshot(personal);
    oldReplica.SetObservedWorldRevision(historicalState.GetWorldRevision());

    const auto plan = PartyQuestRepairPlanner::Build(historicalState, oldReplica.BuildReport());
    REQUIRE(plan.RemovedQuestIds == std::vector<GameId>{serviceQuest});
    REQUIRE(oldReplica.Apply(plan) == PartyQuestReplicaApplyStatus::Applied);
    REQUIRE(oldReplica.FindQuest(serviceQuest) == nullptr);
    REQUIRE(oldReplica.FindQuest(personalQuest) != nullptr);

    const auto verification = PartyQuestRepairPlanner::Build(historicalState, oldReplica.BuildReport());
    REQUIRE(verification.Status == PartyQuestRepairPlanStatus::UpToDate);
}

TEST_CASE("Admission rejection can discard in-flight and queued client work without retry storm", "[quest.party-state.admission]")
{
    const GameId questId(0, 0x030000);
    const QuestSnapshot first = BuildAdmissionSnapshot(questId, 10);
    const QuestSnapshot latest = BuildAdmissionSnapshot(questId, 20);

    PartyQuestClientSubmissionQueue queue;
    REQUIRE(queue.MarkInFlight(9001, first));
    REQUIRE(queue.QueueLatest(latest) == PartyQuestClientSubmissionStatus::Queued);
    REQUIRE(queue.GetInFlightCount() == 1);
    REQUIRE(queue.GetQueuedCount() == 1);

    REQUIRE(queue.Discard(9001));
    REQUIRE(queue.GetInFlightCount() == 0);
    REQUIRE(queue.GetQueuedCount() == 0);

    PartyQuestReplica emptyReplica;
    REQUIRE(queue.TakeReady(emptyReplica).empty());
}
