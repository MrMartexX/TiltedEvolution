#include <Structs/Skyrim/PartyQuestReviewedQuestProfiles.h>
#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestTransitionEvidence MakeValidEvidence(const PartyQuestReviewedTransition& acReviewed, uint64_t aAuthoritativeRevision = 1, uint64_t aOperationId = 1)
{
    PartyQuestTransitionEvidence evidence;
    evidence.Identity = acReviewed.Identity;
    evidence.AnalyzerSchemaVersion = acReviewed.AnalyzerSchemaVersion;
    evidence.AnalyzerVersion = acReviewed.AnalyzerVersion;
    evidence.AnalyzerProvenanceFingerprint = acReviewed.AnalyzerProvenanceFingerprint;
    evidence.AuthoritativeRevision = aAuthoritativeRevision;
    evidence.OperationId = aOperationId;
    evidence.RunningState = acReviewed.RunningState;
    evidence.PriorStages = acReviewed.RequiredPriorStages;
    evidence.Objectives = acReviewed.RequiredObjectives;
    evidence.HasPlayerSpecificPreconditions = acReviewed.PlayerSpecific;
    evidence.HasPartySpecificPreconditions = acReviewed.PartySpecific;
    evidence.SideEffectSurfaceKnown = true;
    evidence.ReadinessEvidenceKnown = true;
    evidence.RequiresReadiness = acReviewed.RequiresReadiness;
    evidence.QuiescencePolicyKnown = true;
    evidence.RequiresQuiescence = acReviewed.RequiresQuiescence;
    evidence.PostconditionsKnown = true;
    evidence.RecoveryClass = acReviewed.RecoveryClass;
    return evidence;
}

PartyQuestRuntimeCompatibilityFacts MakeMatchingFacts(const PartyQuestRuntimeCompatibilityRequirement& acReq)
{
    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = acReq.ProfileVersion;
    facts.ResolvedRecordFingerprint = acReq.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = acReq.WinningOverrideFingerprint;
    facts.ScriptFingerprint = acReq.ScriptFingerprint;
    facts.NativeAdapterFingerprint = acReq.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = acReq.AdapterMutationComponents;
    return facts;
}
} // namespace

TEST_CASE("Exact match authorizes reviewed production profiles", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    REQUIRE(manifest.GetRequirementCount() == 2);

    const auto transitionManifest = PartyQuestReviewedQuestProfiles::BuildReviewedTransitionManifest();
    REQUIRE(transitionManifest.Size() == 2);

    // 1. dunNorthwatchKeepQST
    const GameId northwatchId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId);
    REQUIRE(PartyQuestReviewedQuestProfiles::HasReviewedProfile(northwatchId));

    const auto* pNorthwatchReq = manifest.FindRequirement(northwatchId);
    REQUIRE(pNorthwatchReq != nullptr);
    REQUIRE(pNorthwatchReq->ProfileVersion == PartyQuestReviewedQuestProfiles::ProfileVersion);
    REQUIRE(pNorthwatchReq->ResolvedRecordFingerprint == PartyQuestReviewedQuestProfiles::NorthwatchResolvedRecordFingerprint);
    REQUIRE(pNorthwatchReq->WinningOverrideFingerprint == PartyQuestReviewedQuestProfiles::NorthwatchWinningOverrideFingerprint);
    REQUIRE(pNorthwatchReq->ScriptFingerprint == PartyQuestReviewedQuestProfiles::ReferenceScriptEnvironment);
    REQUIRE(pNorthwatchReq->NativeAdapterFingerprint == PartyQuestReviewedQuestProfiles::NativeAdapterFingerprint);

    const auto northwatchDecision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pNorthwatchReq, MakeMatchingFacts(*pNorthwatchReq));
    REQUIRE(northwatchDecision.IsAuthorized());
    REQUIRE(northwatchDecision.Status == PartyQuestRuntimeCompatibilityStatus::Authorized);

    // 2. dunKarthspireRedoubtQST
    const GameId karthspireId(PartyQuestReviewedQuestProfiles::KarthspireModId, PartyQuestReviewedQuestProfiles::KarthspireBaseId);
    REQUIRE(PartyQuestReviewedQuestProfiles::HasReviewedProfile(karthspireId));

    const auto* pKarthspireReq = manifest.FindRequirement(karthspireId);
    REQUIRE(pKarthspireReq != nullptr);
    REQUIRE(pKarthspireReq->ProfileVersion == PartyQuestReviewedQuestProfiles::ProfileVersion);
    REQUIRE(pKarthspireReq->ResolvedRecordFingerprint == PartyQuestReviewedQuestProfiles::KarthspireResolvedRecordFingerprint);
    REQUIRE(pKarthspireReq->WinningOverrideFingerprint == PartyQuestReviewedQuestProfiles::KarthspireWinningOverrideFingerprint);

    const auto karthspireDecision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pKarthspireReq, MakeMatchingFacts(*pKarthspireReq));
    REQUIRE(karthspireDecision.IsAuthorized());
    REQUIRE(karthspireDecision.Status == PartyQuestRuntimeCompatibilityStatus::Authorized);

    // Unreviewed quest fails closed
    const GameId unreviewedId(0, 0x12345);
    REQUIRE_FALSE(PartyQuestReviewedQuestProfiles::HasReviewedProfile(unreviewedId));
    REQUIRE(manifest.FindRequirement(unreviewedId) == nullptr);
    REQUIRE(manifest.Evaluate(unreviewedId, MakeMatchingFacts(*pNorthwatchReq)).Status == PartyQuestRuntimeCompatibilityStatus::UnknownQuest);
}

TEST_CASE("Exact match admits reviewed transitions without native mutation authority", "[quest.party-state][reviewed-profiles]")
{
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    REQUIRE(transitions.size() == 2);

    for (const auto& reviewed : transitions)
    {
        const auto evidence = MakeValidEvidence(reviewed, 1, 1);
        const auto decision = PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence);
        REQUIRE(decision.IsEligible());
        REQUIRE(decision.StructuralEligibility);
        REQUIRE(decision.EnvironmentCompatible);
        REQUIRE_FALSE(decision.AuthorizationGranted);
        REQUIRE(decision.TransitionFingerprint != 0);

        const auto* found = PartyQuestReviewedQuestProfiles::FindReviewedTransition(reviewed.Identity);
        REQUIRE(found != nullptr);
        REQUIRE(*found == reviewed);
    }
}

TEST_CASE("Source-target boundary enforces forward single-edge transitions", "[quest.party-state][reviewed-profiles]")
{
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    const auto& northwatch = transitions[0];

    // Source >= Target
    auto invalidContract = northwatch;
    invalidContract.Identity.SourceStage = 100;
    invalidContract.Identity.TargetStage = 0;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(invalidContract, MakeValidEvidence(invalidContract)).Status == PartyQuestTransitionAdmissionStatus::Unsupported);

    invalidContract.Identity.SourceStage = 100;
    invalidContract.Identity.TargetStage = 100;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(invalidContract, MakeValidEvidence(invalidContract)).Status == PartyQuestTransitionAdmissionStatus::Unsupported);

    // Multistage intermediate jump
    auto evidence = MakeValidEvidence(northwatch);
    evidence.HasIntermediateStage = true;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(northwatch, evidence).Status == PartyQuestTransitionAdmissionStatus::Unsupported);

    // Mismatched target boundary
    evidence = MakeValidEvidence(northwatch);
    evidence.Identity.TargetStage = 200;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(northwatch, evidence).Status == PartyQuestTransitionAdmissionStatus::Unsupported);
}

TEST_CASE("Preconditions and postconditions require complete verified evidence", "[quest.party-state][reviewed-profiles]")
{
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    const auto& reviewed = transitions[0];

    // Missing side effect surface
    auto evidence = MakeValidEvidence(reviewed);
    evidence.SideEffectSurfaceKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status == PartyQuestTransitionAdmissionStatus::NeedsReview);

    // Missing postconditions
    evidence = MakeValidEvidence(reviewed);
    evidence.PostconditionsKnown = false;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status == PartyQuestTransitionAdmissionStatus::NeedsReview);

    // Prior stages mismatch
    evidence = MakeValidEvidence(reviewed);
    evidence.PriorStages = {10};
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status == PartyQuestTransitionAdmissionStatus::Unsupported);

    // Unexpected running state
    evidence = MakeValidEvidence(reviewed);
    evidence.RunningState = PartyQuestExpectedRunningState::Stopped;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status == PartyQuestTransitionAdmissionStatus::Unsupported);
}

TEST_CASE("Duplicate and replay retirement is strictly enforced", "[quest.party-state][reviewed-profiles]")
{
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    const auto& reviewed = transitions[0];

    // Normal evaluation at revision 10, op 5
    const auto evidence = MakeValidEvidence(reviewed, 10, 5);
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 5, 1).IsEligible());

    // Duplicate replay: same revision and same op
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 10, 5).Status == PartyQuestTransitionAdmissionStatus::Duplicate);

    // Retired revision: same revision but different op
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 10, 4).Status == PartyQuestTransitionAdmissionStatus::Retired);

    // Stale revision: older revision
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence, 15, 1).Status == PartyQuestTransitionAdmissionStatus::StaleRevision);
}

TEST_CASE("Changed plugin, script, or load order fails closed", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    const auto* pReq = manifest.FindRequirement(GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId));
    REQUIRE(pReq != nullptr);

    // Mismatched winning override (load order / override change)
    auto facts = MakeMatchingFacts(*pReq);
    facts.WinningOverrideFingerprint ^= 0xDEADBEEF;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::WinningOverrideMismatch);

    // Mismatched script environment
    facts = MakeMatchingFacts(*pReq);
    facts.ScriptFingerprint ^= 0xFEEDFACE;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::ScriptMismatch);

    // In transition admission:
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    const auto& reviewed = transitions[0];
    auto evidence = MakeValidEvidence(reviewed);
    evidence.Identity.WinningOverrideFingerprint ^= 0x1234;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());

    evidence = MakeValidEvidence(reviewed);
    evidence.Identity.ScriptFingerprint ^= 0x5678;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).IsEligible());
}

TEST_CASE("Wrong runtime, binary, or native adapter fails closed", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    const auto* pReq = manifest.FindRequirement(GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId));
    REQUIRE(pReq != nullptr);

    // Profile version mismatch
    auto facts = MakeMatchingFacts(*pReq);
    facts.ProfileVersion = 999;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::ProfileVersionMismatch);

    // Native adapter mismatch
    facts = MakeMatchingFacts(*pReq);
    facts.NativeAdapterFingerprint = 0xBADF146E;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::NativeAdapterMismatch);

    // Adapter mutation coverage wider than QuestSnapshot fails closed
    facts = MakeMatchingFacts(*pReq);
    facts.AdapterMutationComponents = PartyQuestVerificationComponent::Aliases;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::AdapterMutationCoverageMismatch);

    // Analyzer version mismatch in transition
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    auto evidence = MakeValidEvidence(transitions[0]);
    evidence.AnalyzerVersion = 99;
    REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(transitions[0], evidence).Status == PartyQuestTransitionAdmissionStatus::NeedsReview);
}

TEST_CASE("Same quest ID from another mapping fails closed", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();

    // ModId 1 instead of 0
    const GameId mappedAnotherMod(1, PartyQuestReviewedQuestProfiles::NorthwatchBaseId);
    REQUIRE(manifest.FindRequirement(mappedAnotherMod) == nullptr);
    REQUIRE_FALSE(PartyQuestReviewedQuestProfiles::HasReviewedProfile(mappedAnotherMod));

    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    auto evidence = MakeValidEvidence(transitions[0]);
    evidence.Identity.QuestId = mappedAnotherMod;
    REQUIRE_FALSE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(transitions[0], evidence).IsEligible());
}

TEST_CASE("Missing environment cache fails closed", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    const auto* pReq = manifest.FindRequirement(GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId));
    REQUIRE(pReq != nullptr);

    // Zero script fingerprint
    auto facts = MakeMatchingFacts(*pReq);
    facts.ScriptFingerprint = 0;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::InvalidClientFacts);

    // Zero winning override fingerprint
    facts = MakeMatchingFacts(*pReq);
    facts.WinningOverrideFingerprint = 0;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::InvalidClientFacts);

    // Zero resolved record fingerprint
    facts = MakeMatchingFacts(*pReq);
    facts.ResolvedRecordFingerprint = 0;
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pReq, facts).Status == PartyQuestRuntimeCompatibilityStatus::InvalidClientFacts);
}

TEST_CASE("Unsupported adjacent edges cannot borrow authorization", "[quest.party-state][reviewed-profiles]")
{
    // dunNorthwatchKeepQST only reviewed 0 -> 100
    PartyQuestTransitionIdentity unreviewedEdge;
    unreviewedEdge.QuestId = GameId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId);
    unreviewedEdge.SourceStage = 100;
    unreviewedEdge.TargetStage = 200;
    REQUIRE(PartyQuestReviewedQuestProfiles::FindReviewedTransition(unreviewedEdge) == nullptr);

    // dunKarthspireRedoubtQST only reviewed 10 -> 100
    unreviewedEdge.QuestId = GameId(PartyQuestReviewedQuestProfiles::KarthspireModId, PartyQuestReviewedQuestProfiles::KarthspireBaseId);
    unreviewedEdge.SourceStage = 0;
    unreviewedEdge.TargetStage = 10;
    REQUIRE(PartyQuestReviewedQuestProfiles::FindReviewedTransition(unreviewedEdge) == nullptr);
}

TEST_CASE("Forbidden side-effect evidence rejects every mutation category", "[quest.party-state][reviewed-profiles]")
{
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();
    const auto& reviewed = transitions[0];

    auto expectUnsupported = [&](auto mutate)
    {
        auto evidence = MakeValidEvidence(reviewed);
        mutate(evidence);
        REQUIRE(PartyQuestCompatibilityAdmissionPolicy::Evaluate(reviewed, evidence).Status == PartyQuestTransitionAdmissionStatus::Unsupported);
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
}

TEST_CASE("Manifest publication is strictly all-or-nothing", "[quest.party-state][reviewed-profiles]")
{
    PartyQuestReviewedTransitionManifest manifest;
    const auto transitions = PartyQuestReviewedQuestProfiles::GetReviewedTransitions();

    // Valid publishing succeeds
    REQUIRE(manifest.Publish(transitions));
    REQUIRE(manifest.Size() == 2);

    // Failed publication leaves existing published registry unchanged (strong atomicity)
    std::vector<PartyQuestReviewedTransition> duplicates{transitions[0], transitions[0]};
    REQUIRE_FALSE(manifest.Publish(duplicates));
    REQUIRE(manifest.Size() == 2); // Unchanged on failure!

    // Publishing invalid entry also preserves existing valid registry
    auto invalidEntry = transitions[0];
    invalidEntry.Identity.SourceStage = 100;
    invalidEntry.Identity.TargetStage = 0;
    std::vector<PartyQuestReviewedTransition> withInvalid{transitions[1], invalidEntry};
    REQUIRE_FALSE(manifest.Publish(withInvalid));
    REQUIRE(manifest.Size() == 2);

    // Fresh manifest attempting to publish invalid entries remains empty
    PartyQuestReviewedTransitionManifest freshManifest;
    REQUIRE_FALSE(freshManifest.Publish(duplicates));
    REQUIRE(freshManifest.Size() == 0);

    // Duplicate requirement in PartyQuestRuntimeCompatibilityManifest fails closed
    PartyQuestRuntimeCompatibilityManifest compatManifest;
    const auto requirements = PartyQuestReviewedQuestProfiles::GetReviewedRequirements();
    REQUIRE(compatManifest.AddRequirement(requirements[0]));
    REQUIRE_FALSE(compatManifest.AddRequirement(requirements[0])); // Duplicate rejected!
}

TEST_CASE("Manifest publication produces deterministic fingerprints", "[quest.party-state][reviewed-profiles]")
{
    const auto manifest1 = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    const auto manifest2 = PartyQuestReviewedQuestProfiles::BuildReviewedManifest();
    REQUIRE(manifest1.GetRequirementCount() == manifest2.GetRequirementCount());

    const auto transManifest1 = PartyQuestReviewedQuestProfiles::BuildReviewedTransitionManifest();
    const auto transManifest2 = PartyQuestReviewedQuestProfiles::BuildReviewedTransitionManifest();
    REQUIRE(transManifest1.Size() == transManifest2.Size());

    for (const auto& trans : PartyQuestReviewedQuestProfiles::GetReviewedTransitions())
    {
        const auto* found1 = transManifest1.Find(trans.Identity);
        const auto* found2 = transManifest2.Find(trans.Identity);
        REQUIRE(found1 != nullptr);
        REQUIRE(found2 != nullptr);
        REQUIRE(*found1 == *found2);
    }
}
