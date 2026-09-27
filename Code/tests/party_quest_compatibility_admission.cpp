#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>

#include <catch2/catch.hpp>

#include <array>

namespace
{
PartyQuestReviewedTransition Reviewed()
{
    PartyQuestReviewedTransition value;
    value.Identity = {{0x123, 2}, 10, 20, 4, 11, 12, 13, 14};
    value.AnalyzerSchemaVersion = 1;
    value.AnalyzerVersion = 7;
    value.AnalyzerProvenanceFingerprint = 15;
    value.ReviewerFingerprint = 16;
    value.RunningState = PartyQuestExpectedRunningState::Running;
    value.RequiredPriorStages = {0, 10};
    value.RequiredObjectives = {5};
    value.RequiresReadiness = true;
    value.RequiresQuiescence = true;
    value.RecoveryClass = PartyQuestRecoveryClass::DurableCheckpointRequired;
    return value;
}

PartyQuestTransitionEvidence Evidence(const PartyQuestReviewedTransition& reviewed)
{
    PartyQuestTransitionEvidence value;
    value.Identity = reviewed.Identity;
    value.AnalyzerSchemaVersion = reviewed.AnalyzerSchemaVersion;
    value.AnalyzerVersion = reviewed.AnalyzerVersion;
    value.AnalyzerProvenanceFingerprint = reviewed.AnalyzerProvenanceFingerprint;
    value.AuthoritativeRevision = 9;
    value.OperationId = 10;
    value.RunningState = reviewed.RunningState;
    value.PriorStages = reviewed.RequiredPriorStages;
    value.Objectives = reviewed.RequiredObjectives;
    value.HasPlayerSpecificPreconditions = reviewed.PlayerSpecific;
    value.HasPartySpecificPreconditions = reviewed.PartySpecific;
    value.SideEffectSurfaceKnown = true;
    value.ReadinessEvidenceKnown = true;
    value.RequiresReadiness = reviewed.RequiresReadiness;
    value.QuiescencePolicyKnown = true;
    value.RequiresQuiescence = reviewed.RequiresQuiescence;
    value.PostconditionsKnown = true;
    value.RecoveryClass = reviewed.RecoveryClass;
    return value;
}
}

TEST_CASE("Only exact reviewed transition is eligible without granting mutation authority", "[quest.party-state][compatibility-admission]")
{
    const auto reviewed = Reviewed();
    const auto decision = PartyQuestCompatibilityAdmissionPolicy::Evaluate(
        reviewed, Evidence(reviewed));
    REQUIRE(decision.IsEligible());
    REQUIRE(decision.StructuralEligibility);
    REQUIRE(decision.EnvironmentCompatible);
    REQUIRE_FALSE(decision.AuthorizationGranted);
    REQUIRE(decision.TransitionFingerprint != 0);
}

TEST_CASE("Compatibility admission rejects every forbidden side-effect category", "[quest.party-state][compatibility-admission]")
{
    const auto reviewed = Reviewed();
    auto expectUnsupported = [&](auto mutate)
    {
        auto evidence = Evidence(reviewed);
        mutate(evidence);
        REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
            PartyQuestTransitionAdmissionStatus::Unsupported);
    };
    expectUnsupported([](auto& v) { v.SceneCount = 1; });
    expectUnsupported([](auto& v) { v.AliasCount = 1; });
    expectUnsupported([](auto& v) { v.CreatedReferenceCount = 1; });
    expectUnsupported([](auto& v) { v.HasPlayerAlias = true; });
    expectUnsupported([](auto& v) { v.HasInventoryMutation = true; });
    expectUnsupported([](auto& v) { v.HasQuestObjectMutation = true; });
    expectUnsupported([](auto& v) { v.HasWorldMutation = true; });
    expectUnsupported([](auto& v) { v.HasAliasMutation = true; });
    expectUnsupported([](auto& v) { v.HasUnboundedScriptEffects = true; });
    expectUnsupported([](auto& v) { v.HasIntermediateStage = true; });
}

TEST_CASE("Unknown evidence never becomes safe", "[quest.party-state][compatibility-admission]")
{
    const auto reviewed = Reviewed();
    auto evidence = Evidence(reviewed);
    evidence.ReadinessEvidenceKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::NeedsReview);
    evidence = Evidence(reviewed);
    evidence.SideEffectSurfaceKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::NeedsReview);
    evidence = Evidence(reviewed);
    evidence.QuiescencePolicyKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::NeedsReview);
    evidence = Evidence(reviewed);
    evidence.PostconditionsKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::NeedsReview);
}

TEST_CASE("Runtime environment and analyzer provenance are exact", "[quest.party-state][compatibility-admission]")
{
    const auto reviewed = Reviewed();
    auto evidence = Evidence(reviewed);
    ++evidence.Identity.ScriptFingerprint;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::Unsupported);
    evidence = Evidence(reviewed);
    ++evidence.Identity.WinningOverrideFingerprint;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
    evidence = Evidence(reviewed);
    ++evidence.AnalyzerVersion;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status ==
        PartyQuestTransitionAdmissionStatus::NeedsReview);
    evidence = Evidence(reviewed);
    ++evidence.Identity.PolicyVersion;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
}

TEST_CASE("Player party state and reviewed preconditions must match exactly", "[quest.party-state][compatibility-admission]")
{
    auto reviewed = Reviewed();
    reviewed.PlayerSpecific = true;
    reviewed.PartySpecific = true;
    auto evidence = Evidence(reviewed);
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
    evidence.HasPlayerSpecificPreconditions = false;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
    evidence = Evidence(reviewed);
    evidence.PriorStages.push_back(15);
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
    evidence = Evidence(reviewed);
    evidence.Objectives.clear();
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
}

TEST_CASE("Revision duplicate and replay classification is deterministic", "[quest.party-state][compatibility-admission][replay]")
{
    const auto reviewed = Reviewed();
    const auto evidence = Evidence(reviewed);
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 10, 11).Status ==
        PartyQuestTransitionAdmissionStatus::StaleRevision);
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 9, 10).Status ==
        PartyQuestTransitionAdmissionStatus::Duplicate);
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 9, 99).Status ==
        PartyQuestTransitionAdmissionStatus::Retired);
}

TEST_CASE("Malformed oversized and backward reviewed contracts fail closed", "[quest.party-state][compatibility-admission]")
{
    auto reviewed = Reviewed();
    reviewed.Identity.TargetStage = reviewed.Identity.SourceStage;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(
        reviewed, Evidence(reviewed)).IsEligible());
    reviewed = Reviewed();
    reviewed.RequiredPriorStages = {10, 10};
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(
        reviewed, Evidence(reviewed)).IsEligible());
    reviewed = Reviewed();
    reviewed.RequiredObjectives.resize(PartyQuestTransitionEvidence::MaxCollectionEntries + 1);
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(
        reviewed, Evidence(reviewed)).IsEligible());
}

TEST_CASE("Reviewed manifest publication is atomic and rejects duplicate transition identity", "[quest.party-state][compatibility-admission][publication]")
{
    PartyQuestReviewedTransitionManifest manifest;
    const auto first = Reviewed();
    REQUIRE(manifest.Publish(std::span(&first, 1)));
    REQUIRE(manifest.Size() == 1);
    REQUIRE(manifest.Find(first.Identity) != nullptr);

    const std::array duplicates{first, first};
    REQUIRE_FALSE(manifest.Publish(duplicates));
    REQUIRE(manifest.Size() == 1);
    REQUIRE(manifest.Find(first.Identity) != nullptr);

    auto invalid = first;
    invalid.ReviewerFingerprint = 0;
    REQUIRE_FALSE(manifest.Publish(std::span(&invalid, 1)));
    REQUIRE(manifest.Size() == 1);
}

TEST_CASE("Transition fingerprints are deterministic and domain bound", "[quest.party-state][compatibility-admission][identity]")
{
    const auto reviewed = Reviewed();
    const auto first = PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, Evidence(reviewed));
    const auto second = PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, Evidence(reviewed));
    REQUIRE(first.TransitionFingerprint == second.TransitionFingerprint);
    auto changed = reviewed;
    ++changed.ReviewerFingerprint;
    REQUIRE(first.TransitionFingerprint !=
        PartyQuestCompatibilityAdmissionPolicy::Evaluate(changed, Evidence(changed)).TransitionFingerprint);
}

TEST_CASE("Manifest equality defends against internal hash collisions", "[quest.party-state][compatibility-admission][identity]")
{
    const auto first = Reviewed();
    auto second = first;
    // KeyHash intentionally buckets these together; full identity equality must
    // still preserve both reviewed contracts.
    ++second.Identity.ScriptFingerprint;
    const std::array entries{first, second};
    PartyQuestReviewedTransitionManifest manifest;
    REQUIRE(manifest.Publish(entries));
    REQUIRE(manifest.Size() == 2);
    REQUIRE(manifest.Find(first.Identity) != nullptr);
    REQUIRE(manifest.Find(second.Identity) != nullptr);
}
