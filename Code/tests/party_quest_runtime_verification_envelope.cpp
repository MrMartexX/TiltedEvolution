#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <catch2/catch.hpp>

#include <type_traits>

namespace
{
const PartyQuestCampaignId kCampaign{0x6101, 0x6102};
const PartyQuestPlayerProfileId kPlayer{0x6201, 0x6202};

struct Fixture
{
    PartyQuestReviewedTransition Reviewed;
    PartyQuestTransitionEvidence Evidence;
    PartyQuestRuntimeEnvelopeIdentity Identity;
    QuestSnapshot Source;
    QuestSnapshot Target;
    PartyQuestRuntimeEnvelopePreconditions Preconditions;
};

Fixture BuildFixture(bool aReadiness = true, bool aQuiescence = true)
{
    Fixture f;
    f.Reviewed.Identity.QuestId = GameId(7, 0x1234);
    f.Reviewed.Identity.SourceStage = 10;
    f.Reviewed.Identity.TargetStage = 20;
    f.Reviewed.Identity.PolicyVersion = 3;
    f.Reviewed.Identity.EnvironmentFingerprint = 11;
    f.Reviewed.Identity.TopologyFingerprint = 12;
    f.Reviewed.Identity.WinningOverrideFingerprint = 13;
    f.Reviewed.Identity.ScriptFingerprint = 14;
    f.Reviewed.AnalyzerSchemaVersion = 1;
    f.Reviewed.AnalyzerVersion = 2;
    f.Reviewed.AnalyzerProvenanceFingerprint = 15;
    f.Reviewed.ReviewerFingerprint = 16;
    f.Reviewed.RunningState = PartyQuestExpectedRunningState::Running;
    f.Reviewed.RequiredPriorStages = {10};
    f.Reviewed.RequiredObjectives = {10};
    f.Reviewed.PlayerSpecific = true;
    f.Reviewed.PartySpecific = true;
    f.Reviewed.RequiresReadiness = aReadiness;
    f.Reviewed.RequiresQuiescence = aQuiescence;
    f.Reviewed.RecoveryClass = PartyQuestRecoveryClass::DurableCheckpointRequired;

    f.Evidence.Identity = f.Reviewed.Identity;
    f.Evidence.AnalyzerSchemaVersion = f.Reviewed.AnalyzerSchemaVersion;
    f.Evidence.AnalyzerVersion = f.Reviewed.AnalyzerVersion;
    f.Evidence.AnalyzerProvenanceFingerprint = f.Reviewed.AnalyzerProvenanceFingerprint;
    f.Evidence.AuthoritativeRevision = 100;
    f.Evidence.OperationId = 200;
    f.Evidence.RunningState = f.Reviewed.RunningState;
    f.Evidence.PriorStages = f.Reviewed.RequiredPriorStages;
    f.Evidence.Objectives = f.Reviewed.RequiredObjectives;
    f.Evidence.HasPlayerSpecificPreconditions = true;
    f.Evidence.HasPartySpecificPreconditions = true;
    f.Evidence.SideEffectSurfaceKnown = true;
    f.Evidence.ReadinessEvidenceKnown = true;
    f.Evidence.RequiresReadiness = aReadiness;
    f.Evidence.QuiescencePolicyKnown = true;
    f.Evidence.RequiresQuiescence = aQuiescence;
    f.Evidence.PostconditionsKnown = true;
    f.Evidence.RecoveryClass = f.Reviewed.RecoveryClass;

    const auto admission = PartyQuestCompatibilityAdmissionPolicy::Evaluate(
        f.Reviewed, f.Evidence);
    REQUIRE(admission.IsEligible());

    f.Identity.CampaignId = kCampaign;
    f.Identity.PlayerProfileId = kPlayer;
    f.Identity.SessionId = 300;
    f.Identity.PartyId = 400;
    f.Identity.RuntimeGeneration = 500;
    f.Identity.TransactionId = 600;
    f.Identity.AuthoritativeRevision = f.Evidence.AuthoritativeRevision;
    f.Identity.OperationId = f.Evidence.OperationId;
    f.Identity.Transition = f.Reviewed.Identity;
    f.Identity.CompatibilityFingerprint = admission.TransitionFingerprint;

    f.Source.QuestId = f.Reviewed.Identity.QuestId;
    f.Source.Status = QuestSnapshotStatus::Running;
    f.Source.CurrentStage = 10;
    f.Source.Revision = 99;
    f.Source.CompletedStages = {10};
    f.Source.Canonicalize();
    f.Target = f.Source;
    f.Target.CurrentStage = 20;
    f.Target.Revision = 100;
    f.Target.CompletedStages = {10, 20};
    f.Target.Canonicalize();
    f.Identity.ExpectedCurrentSnapshotDigest = f.Source.ComputeDigest();
    f.Identity.ExpectedTargetSnapshotDigest = f.Target.ComputeDigest();

    f.Preconditions.CurrentIdentity = f.Identity;
    f.Preconditions.CurrentSnapshot = f.Source;
    f.Preconditions.ProcessOwnerCurrent = true;
    f.Preconditions.SessionCurrent = true;
    f.Preconditions.PartyCurrent = true;
    f.Preconditions.TransactionActive = true;
    f.Preconditions.RevisionCurrent = true;
    f.Preconditions.CompatibilityCacheCurrent = true;
    f.Preconditions.ReferenceEvidenceAvailable = true;
    f.Preconditions.ReferenceReady = true;
    f.Preconditions.ReferenceGeneration = f.Identity.RuntimeGeneration;
    f.Preconditions.PapyrusObserverAvailable = true;
    f.Preconditions.PapyrusQuiescent = true;
    f.Preconditions.CheckpointAuthorized = true;
    f.Preconditions.CheckpointTransactionId = f.Identity.TransactionId;
    f.Preconditions.CheckpointRevision = f.Identity.AuthoritativeRevision;
    return f;
}

PartyQuestRuntimeEnvelopeAuthorization Authorize(Fixture& f)
{
    auto result = PartyQuestRuntimeVerificationGate::AuthorizeEnvelope(
        f.Reviewed, f.Evidence, f.Identity, f.Target, f.Preconditions);
    REQUIRE(result.Outcome == PartyQuestRuntimeEnvelopeOutcome::Authorized);
    REQUIRE(result.Authorization.has_value());
    return std::move(*result.Authorization);
}
}

static_assert(!std::is_copy_constructible_v<PartyQuestRuntimeEnvelopeAuthorization>);
static_assert(!std::is_copy_assignable_v<PartyQuestRuntimeEnvelopeAuthorization>);

TEST_CASE("Complete verification envelope commits only after exact stable postconditions",
    "[quest.party-state][verification-envelope]")
{
    auto f = BuildFixture();
    auto authorization = Authorize(f);
    PartyQuestRuntimeEnvelopePostconditions post;
    post.Snapshot = f.Target;
    post.StableSamples = 2;
    const auto result = PartyQuestRuntimeVerificationGate::CompleteEnvelope(
        std::move(authorization), f.Identity, post, true, true);
    REQUIRE(result.Outcome == PartyQuestRuntimeEnvelopeOutcome::Verified);
    REQUIRE(result.Disposition == PartyQuestRuntimeEnvelopeDisposition::Commit);

    const auto replay = PartyQuestRuntimeVerificationGate::CompleteEnvelope(
        std::move(authorization), f.Identity, post, true, true);
    REQUIRE(replay.Outcome == PartyQuestRuntimeEnvelopeOutcome::RejectedStale);
    REQUIRE(replay.Disposition == PartyQuestRuntimeEnvelopeDisposition::NoOp);
}

TEST_CASE("Every runtime identity mismatch rejects before authorization",
    "[quest.party-state][verification-envelope][identity]")
{
    auto f = BuildFixture();
    const auto baseline = f.Preconditions.CurrentSnapshot;
    SECTION("campaign") { f.Preconditions.CurrentIdentity.CampaignId = {9, 9}; }
    SECTION("player") { f.Preconditions.CurrentIdentity.PlayerProfileId = {9, 9}; }
    SECTION("session") { ++f.Preconditions.CurrentIdentity.SessionId; }
    SECTION("party") { ++f.Preconditions.CurrentIdentity.PartyId; }
    SECTION("generation") { ++f.Preconditions.CurrentIdentity.RuntimeGeneration; }
    SECTION("transaction") { ++f.Preconditions.CurrentIdentity.TransactionId; }
    SECTION("revision") { ++f.Preconditions.CurrentIdentity.AuthoritativeRevision; }
    SECTION("operation") { ++f.Preconditions.CurrentIdentity.OperationId; }
    SECTION("profile") { ++f.Preconditions.CurrentIdentity.CompatibilityFingerprint; }
    const auto result = PartyQuestRuntimeVerificationGate::AuthorizeEnvelope(
        f.Reviewed, f.Evidence, f.Identity, f.Target, f.Preconditions);
    REQUIRE(result.Outcome == PartyQuestRuntimeEnvelopeOutcome::RejectedStale);
    REQUIRE_FALSE(result.Authorization.has_value());
    REQUIRE(f.Preconditions.CurrentSnapshot == baseline);
}

TEST_CASE("Readiness never overrides stale authority and observers fail closed",
    "[quest.party-state][verification-envelope][evidence]")
{
    auto f = BuildFixture();
    SECTION("ready but stale") { ++f.Preconditions.CurrentIdentity.RuntimeGeneration; }
    SECTION("wrong readiness generation") { ++f.Preconditions.ReferenceGeneration; }
    SECTION("readiness observer absent") { f.Preconditions.ReferenceEvidenceAvailable = false; }
    SECTION("papyrus observer absent") { f.Preconditions.PapyrusObserverAvailable = false; }
    SECTION("papyrus busy") { f.Preconditions.PapyrusQuiescent = false; }
    SECTION("checkpoint mismatch") { ++f.Preconditions.CheckpointRevision; }
    const auto result = PartyQuestRuntimeVerificationGate::AuthorizeEnvelope(
        f.Reviewed, f.Evidence, f.Identity, f.Target, f.Preconditions);
    REQUIRE_FALSE(result.Authorization.has_value());
    REQUIRE(result.Outcome != PartyQuestRuntimeEnvelopeOutcome::Verified);
}

TEST_CASE("Forbidden surfaces and pre-snapshot mismatch are rejected",
    "[quest.party-state][verification-envelope][preconditions]")
{
    auto f = BuildFixture();
    SECTION("alias") { f.Evidence.HasAliasMutation = true; }
    SECTION("scene") { f.Evidence.SceneCount = 1; }
    SECTION("inventory") { f.Evidence.HasInventoryMutation = true; }
    SECTION("quest object") { f.Evidence.HasQuestObjectMutation = true; }
    SECTION("world") { f.Evidence.HasWorldMutation = true; }
    SECTION("source stage") { ++f.Preconditions.CurrentSnapshot.CurrentStage; }
    const auto result = PartyQuestRuntimeVerificationGate::AuthorizeEnvelope(
        f.Reviewed, f.Evidence, f.Identity, f.Target, f.Preconditions);
    REQUIRE_FALSE(result.Authorization.has_value());
    REQUIRE(result.Disposition == PartyQuestRuntimeEnvelopeDisposition::NoOp);
}

TEST_CASE("Every post-dispatch failure requires recovery and never commits",
    "[quest.party-state][verification-envelope][outcomes]")
{
    auto run = [](auto mutate) {
        auto f = BuildFixture();
        auto authorization = Authorize(f);
        PartyQuestRuntimeEnvelopePostconditions post;
        post.Snapshot = f.Target;
        post.StableSamples = 2;
        bool returned = true;
        bool timeout = false;
        mutate(f, post, returned, timeout);
        const auto result = PartyQuestRuntimeVerificationGate::CompleteEnvelope(
            std::move(authorization), f.Identity, post, true, returned, timeout);
        REQUIRE(result.Outcome != PartyQuestRuntimeEnvelopeOutcome::Verified);
        REQUIRE(result.Disposition == PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired);
    };
    SECTION("native false") { run([](auto&, auto&, bool& ok, bool&) { ok = false; }); }
    SECTION("timeout") { run([](auto&, auto&, bool&, bool& timeout) { timeout = true; }); }
    SECTION("observer unavailable") { run([](auto&, auto& p, bool&, bool&) { p.Snapshot.reset(); }); }
    SECTION("single sample") { run([](auto&, auto& p, bool&, bool&) { p.StableSamples = 1; }); }
    SECTION("stage mismatch") { run([](auto&, auto& p, bool&, bool&) { ++p.Snapshot->CurrentStage; }); }
    SECTION("alias delta") { run([](auto&, auto& p, bool&, bool&) { p.AliasDelta = true; }); }
    SECTION("scene delta") { run([](auto&, auto& p, bool&, bool&) { p.SceneDelta = true; }); }
    SECTION("inventory delta") { run([](auto&, auto& p, bool&, bool&) { p.InventoryDelta = true; }); }
    SECTION("quest object delta") { run([](auto&, auto& p, bool&, bool&) { p.QuestObjectDelta = true; }); }
    SECTION("world delta") { run([](auto&, auto& p, bool&, bool&) { p.WorldDelta = true; }); }
    SECTION("lifecycle invalidation") { run([](auto& f, auto&, bool&, bool&) { ++f.Identity.RuntimeGeneration; }); }
}

TEST_CASE("Cancellation before dispatch is a deterministic no-op",
    "[quest.party-state][verification-envelope][state-machine]")
{
    auto f = BuildFixture();
    auto authorization = Authorize(f);
    PartyQuestRuntimeEnvelopePostconditions post;
    const auto result = PartyQuestRuntimeVerificationGate::CompleteEnvelope(
        std::move(authorization), f.Identity, post, false, false);
    REQUIRE(result.Outcome == PartyQuestRuntimeEnvelopeOutcome::MutationFailed);
    REQUIRE(result.Disposition == PartyQuestRuntimeEnvelopeDisposition::NoOp);
}
