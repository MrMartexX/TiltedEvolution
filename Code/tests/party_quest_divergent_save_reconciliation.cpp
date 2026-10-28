#include <Structs/Skyrim/PartyQuestReconciliation.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <catch2/catch.hpp>

namespace
{
constexpr PartyQuestCampaignId kCampaignA{0x1111222233334444ull, 0x5555666677778888ull};
constexpr PartyQuestCampaignId kCampaignB{0x9999AAAABBBBCCCCull, 0xDDDDEEEEFFFF0000ull};
constexpr uint64_t kTestEnvironmentFingerprint = 0xAA55BB66CC77DD88ull;

QuestSnapshot MakeCanonical(uint16_t aStage, uint64_t aRevision = 100)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(33, 0x5000);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = aRevision;
    snapshot.InitiatorPlayerId = 1;
    snapshot.CompletedStages = {0, 10};
    if (aStage >= 20)
        snapshot.CompletedStages.push_back(20);
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}, {20, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

QuestSnapshot MakeLocal(uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(33, 0x5000);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = 1;
    snapshot.InitiatorPlayerId = 1;
    snapshot.CompletedStages = {0, 10};
    if (aStage >= 20)
        snapshot.CompletedStages.push_back(20);
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}, {20, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestReviewedTransition MakeReviewed(uint16_t aSourceStage = 10, uint16_t aTargetStage = 20)
{
    PartyQuestReviewedTransition transition;
    transition.Identity.QuestId = GameId(33, 0x5000);
    transition.Identity.SourceStage = aSourceStage;
    transition.Identity.TargetStage = aTargetStage;
    transition.Identity.PolicyVersion = 1;
    transition.Identity.EnvironmentFingerprint = kTestEnvironmentFingerprint;
    transition.Identity.TopologyFingerprint = 0x1234ull;
    transition.Identity.WinningOverrideFingerprint = 0x5678ull;
    transition.Identity.ScriptFingerprint = 0x90ABull;
    transition.AnalyzerSchemaVersion = 1;
    transition.AnalyzerVersion = 7;
    transition.AnalyzerProvenanceFingerprint = 0xFEFEull;
    transition.ReviewerFingerprint = 0xCAFEull;
    transition.RunningState = PartyQuestExpectedRunningState::Running;
    transition.RequiredPriorStages = {10};
    transition.RequiredObjectives = {10};
    transition.RequiresQuiescence = true;
    transition.RecoveryClass = PartyQuestRecoveryClass::DurableCheckpointRequired;
    return transition;
}

PartyQuestReconciliationInput MakeInput(uint16_t aLocalStage, uint16_t aCanonicalStage)
{
    PartyQuestReconciliationInput input;
    input.CampaignId = kCampaignA;
    input.TransactionId = 99001;
    input.TargetWorldRevision = 200;
    input.RuntimeGeneration = PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    input.CanonicalSnapshot = MakeCanonical(aCanonicalStage, 200);
    input.LocalSnapshot = MakeLocal(aLocalStage);
    input.ReviewedTransition = MakeReviewed(10, 20);
    input.ObservedEnvironmentFingerprint = kTestEnvironmentFingerprint;
    input.LastCommittedWorldRevision = 150;
    return input;
}
} // namespace

TEST_CASE("Reconciliation planner classifies exact equivalent state as NoOpEquivalent", "[quest.party-state.reconciliation][exact]")
{
    auto input = MakeInput(20, 20);
    const auto result = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(result.Classification == PartyQuestReconciliationClassification::ExactEquivalent);
    REQUIRE(result.Outcome == PartyQuestReconciliationOutcome::NoOpEquivalent);
    REQUIRE(result.IsAdmissible());
    REQUIRE_FALSE(result.CandidateRequest.has_value());
}

TEST_CASE("Reconciliation planner produces admissible candidate when locally behind on one reviewed edge", "[quest.party-state.reconciliation][behind]")
{
    auto input = MakeInput(10, 20);
    const auto result = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(result.Classification == PartyQuestReconciliationClassification::LocallyBehindReviewedEdge);
    REQUIRE(result.Outcome == PartyQuestReconciliationOutcome::ApplyReviewedTransition);
    REQUIRE(result.IsAdmissible());
    REQUIRE(result.CandidateRequest.has_value());
    REQUIRE(result.CandidateRequest->Plan.DryRunOnly);
    REQUIRE(result.CandidateRequest->TransactionId == input.TransactionId);
    REQUIRE(result.CandidateRequest->TargetWorldRevision == input.TargetWorldRevision);
    REQUIRE(result.CandidateRequest->CanonicalSnapshot.QuestId == input.CanonicalSnapshot.QuestId);
    REQUIRE(result.CandidateRequest->Plan.MutationAuthorization.IsVerified());
}

TEST_CASE("Reconciliation planner confirms locally ahead without modifying server authority", "[quest.party-state.reconciliation][ahead]")
{
    auto input = MakeInput(30, 20);
    input.LocalSnapshot->CompletedStages = {0, 10, 20, 30};
    input.LocalSnapshot->Canonicalize();

    const auto result = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(result.Classification == PartyQuestReconciliationClassification::LocallyAheadCompatible);
    REQUIRE(result.Outcome == PartyQuestReconciliationOutcome::ConfirmLocallyAhead);
    REQUIRE(result.IsAdmissible());
    REQUIRE_FALSE(result.CandidateRequest.has_value());
}

TEST_CASE("Reconciliation planner fails closed on unreviewed multi-stage gap", "[quest.party-state.reconciliation][gap]")
{
    // Local at 0, canonical at 20, but reviewed edge is 10->20 (gap without 0->10)
    auto input = MakeInput(0, 20);
    const auto result = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(result.Classification == PartyQuestReconciliationClassification::DivergentBranchOrState);
    REQUIRE(result.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);
    REQUIRE_FALSE(result.IsAdmissible());
    REQUIRE_FALSE(result.CandidateRequest.has_value());
}

TEST_CASE("Reconciliation planner rejects alternate branch with same stage", "[quest.party-state.reconciliation][alternate-branch]")
{
    auto input = MakeInput(20, 20);
    // Alternate branch: different completed stages
    input.LocalSnapshot->CompletedStages = {0, 15, 20};
    input.LocalSnapshot->Canonicalize();

    const auto result = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(result.Classification == PartyQuestReconciliationClassification::DivergentBranchOrState);
    REQUIRE(result.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);
    REQUIRE_FALSE(result.IsAdmissible());
}

TEST_CASE("Reconciliation planner rejects objective mismatch and active unreviewed dependencies", "[quest.party-state.reconciliation][dependencies]")
{
    // Objective mismatch on required objective
    auto input = MakeInput(10, 20);
    input.LocalSnapshot->Objectives = {{20, QuestObjectiveState::Displayed}}; // Missing required objective 10
    input.LocalSnapshot->Canonicalize();

    const auto resultObj = PartyQuestReconciliationPlanner::Plan(input);
    REQUIRE(resultObj.Classification == PartyQuestReconciliationClassification::DivergentBranchOrState);
    REQUIRE(resultObj.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);

    // Active unreviewed reference alias dependency
    auto inputAlias = MakeInput(10, 20);
    inputAlias.LocalSnapshot->ReferenceAliases = {{1, GameId(33, 0x6600)}};
    inputAlias.ReviewedTransition->RequiresReadiness = false; // Transition does not allow aliases

    const auto resultAlias = PartyQuestReconciliationPlanner::Plan(inputAlias);
    REQUIRE(resultAlias.Classification == PartyQuestReconciliationClassification::DivergentBranchOrState);
    REQUIRE(resultAlias.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);
}

TEST_CASE("Reconciliation planner handles missing or stopped quest states fail-closed", "[quest.party-state.reconciliation][missing]")
{
    // Missing local snapshot
    auto inputMissing = MakeInput(10, 20);
    inputMissing.LocalSnapshot = std::nullopt;
    const auto resultMissing = PartyQuestReconciliationPlanner::Plan(inputMissing);
    REQUIRE(resultMissing.Classification == PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped);
    REQUIRE(resultMissing.Outcome == PartyQuestReconciliationOutcome::MissingModOrQuest);

    // Local quest stopped / failed while canonical running
    auto inputStopped = MakeInput(10, 20);
    inputStopped.LocalSnapshot->Status = QuestSnapshotStatus::Stopped;
    const auto resultStopped = PartyQuestReconciliationPlanner::Plan(inputStopped);
    REQUIRE(resultStopped.Classification == PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped);
    REQUIRE(resultStopped.Outcome == PartyQuestReconciliationOutcome::MissingModOrQuest);

    // Local quest inactive and unreviewed
    auto inputInactive = MakeInput(0, 20);
    inputInactive.LocalSnapshot->Status = QuestSnapshotStatus::Inactive;
    const auto resultInactive = PartyQuestReconciliationPlanner::Plan(inputInactive);
    REQUIRE(resultInactive.Classification == PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped);
    REQUIRE(resultInactive.Outcome == PartyQuestReconciliationOutcome::MissingModOrQuest);
}

TEST_CASE("Reconciliation planner enforces environment and mod identity matching", "[quest.party-state.reconciliation][environment]")
{
    // Environment fingerprint mismatch
    auto inputEnv = MakeInput(10, 20);
    inputEnv.ObservedEnvironmentFingerprint = 0xBADF00Dull; // Mismatched

    const auto resultEnv = PartyQuestReconciliationPlanner::Plan(inputEnv);
    REQUIRE(resultEnv.Classification == PartyQuestReconciliationClassification::EnvironmentMismatch);
    REQUIRE(resultEnv.Outcome == PartyQuestReconciliationOutcome::EnvironmentRejected);

    // Quest ID mismatch between transition and canonical
    auto inputId = MakeInput(10, 20);
    inputId.ReviewedTransition->Identity.QuestId = GameId(44, 0x7777);

    const auto resultId = PartyQuestReconciliationPlanner::Plan(inputId);
    REQUIRE(resultId.Classification == PartyQuestReconciliationClassification::EnvironmentMismatch);
    REQUIRE(resultId.Outcome == PartyQuestReconciliationOutcome::EnvironmentRejected);
}

TEST_CASE("Reconciliation planner distinguishes RetryAfterFreshSnapshot from RecoveryRequired", "[quest.party-state.reconciliation][retry-vs-recovery]")
{
    // Generation mismatch -> RetryAfterFreshSnapshot
    auto inputGen = MakeInput(10, 20);
    inputGen.RuntimeGeneration = 0;
    const auto resultGen = PartyQuestReconciliationPlanner::Plan(inputGen);
    REQUIRE(resultGen.Classification == PartyQuestReconciliationClassification::InsufficientOrUnstableObservation);
    REQUIRE(resultGen.Outcome == PartyQuestReconciliationOutcome::RetryAfterFreshSnapshot);
    REQUIRE(resultGen.RequiresRetry());
    REQUIRE_FALSE(resultGen.RequiresRecovery());

    // Recovery blocked or prior pending -> RecoveryRequired
    auto inputRec = MakeInput(10, 20);
    inputRec.PriorTransactionPending = true;
    const auto resultRec = PartyQuestReconciliationPlanner::Plan(inputRec);
    REQUIRE(resultRec.Classification == PartyQuestReconciliationClassification::RecoveryRequired);
    REQUIRE(resultRec.Outcome == PartyQuestReconciliationOutcome::RecoveryRequired);
    REQUIRE(resultRec.RequiresRecovery());
    REQUIRE_FALSE(resultRec.RequiresRetry());
}

TEST_CASE("Reconciliation planner enforces N+1 transaction retirement and ordering rules", "[quest.party-state.reconciliation][ordering]")
{
    // Transaction already retired
    auto inputRetired = MakeInput(10, 20);
    inputRetired.RetiredTransactions = {99001};
    const auto resultRetired = PartyQuestReconciliationPlanner::Plan(inputRetired);
    REQUIRE(resultRetired.Classification == PartyQuestReconciliationClassification::ExactEquivalent);
    REQUIRE(resultRetired.Outcome == PartyQuestReconciliationOutcome::NoOpEquivalent);

    // Stale revision retired
    auto inputStale = MakeInput(10, 20);
    inputStale.TargetWorldRevision = 100;
    inputStale.LastCommittedWorldRevision = 150; // Newer revision committed
    const auto resultStale = PartyQuestReconciliationPlanner::Plan(inputStale);
    REQUIRE(resultStale.Classification == PartyQuestReconciliationClassification::DivergentBranchOrState);
    REQUIRE(resultStale.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);
}

TEST_CASE("Reconciliation planner enforces campaign isolation on identical quest and Form IDs", "[quest.party-state.reconciliation][isolation]")
{
    auto inputA = MakeInput(10, 20);
    inputA.CampaignId = kCampaignA;

    auto inputB = MakeInput(10, 20);
    inputB.CampaignId = kCampaignB;

    // Both produce valid results for their respective campaigns
    const auto resultA = PartyQuestReconciliationPlanner::Plan(inputA);
    const auto resultB = PartyQuestReconciliationPlanner::Plan(inputB);

    REQUIRE(resultA.IsAdmissible());
    REQUIRE(resultB.IsAdmissible());

    // Candidate requests carry their own distinct canonical digests and identities
    REQUIRE(resultA.CandidateRequest->TargetWorldRevision == inputA.TargetWorldRevision);
    REQUIRE(resultB.CandidateRequest->TargetWorldRevision == inputB.TargetWorldRevision);

    // Invalid campaign ID fails closed
    auto inputInvalid = MakeInput(10, 20);
    inputInvalid.CampaignId = PartyQuestCampaignId{};
    const auto resultInvalid = PartyQuestReconciliationPlanner::Plan(inputInvalid);
    REQUIRE(resultInvalid.Outcome == PartyQuestReconciliationOutcome::UnsupportedDivergence);
}

TEST_CASE("Reconciliation planner produces deterministic repeated planning", "[quest.party-state.reconciliation][determinism]")
{
    const auto input = MakeInput(10, 20);
    const auto first = PartyQuestReconciliationPlanner::Plan(input);
    const auto second = PartyQuestReconciliationPlanner::Plan(input);

    REQUIRE(first.Classification == second.Classification);
    REQUIRE(first.Outcome == second.Outcome);
    REQUIRE(first.CandidateRequest.has_value() == second.CandidateRequest.has_value());
    REQUIRE(first.CandidateRequest->Plan.MutationAuthorization.GetCompatibilityFingerprint() == second.CandidateRequest->Plan.MutationAuthorization.GetCompatibilityFingerprint());
    REQUIRE(first.CandidateRequest->CanonicalSnapshot.ComputeDigest() == second.CandidateRequest->CanonicalSnapshot.ComputeDigest());
}
