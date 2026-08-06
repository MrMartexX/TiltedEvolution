#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestRuntimeCompatibilityRequirement BuildMutationRequirement(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 9;
    requirement.ResolvedRecordFingerprint = 0x9011901290139014ull;
    requirement.WinningOverrideFingerprint = 0x9021902290239024ull;
    requirement.ScriptFingerprint = 0x9031903290339034ull;
    requirement.NativeAdapterFingerprint = 0x9041904290439044ull;
    return requirement;
}

PartyQuestRuntimeCompatibilityFacts BuildMutationFacts(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = acRequirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = acRequirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = acRequirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = acRequirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = acRequirement.NativeAdapterFingerprint;
    return facts;
}

PartyQuestAdmissionDecision BuildMutationAdmission(GameId aQuestId)
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(admission.IsAdmitted());
    return admission;
}

QuestSnapshot BuildMutationSnapshot(GameId aQuestId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 5;
    snapshot.InitiatorPlayerId = 31;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}
} // namespace

TEST_CASE("Runtime compatibility profile is quest-scoped mutation authority", "[quest.party-state.mutation-authorization]")
{
    const GameId firstQuest(93, 0x7100);
    const GameId secondQuest(93, 0x7200);
    const auto requirement = BuildMutationRequirement(firstQuest);
    const auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        requirement,
        BuildMutationFacts(requirement));

    REQUIRE(compatibility.IsAuthorized());
    REQUIRE(compatibility.SafetyProfile.HasVerifiedNativeAdapter());
    REQUIRE(compatibility.SafetyProfile.IsVerifiedFor(firstQuest));
    REQUIRE_FALSE(compatibility.SafetyProfile.IsVerifiedFor(secondQuest));
    REQUIRE(compatibility.SafetyProfile.GetCompatibilityFingerprint() != 0);

    const auto firstSnapshot = BuildMutationSnapshot(firstQuest);
    const auto firstPlan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildMutationAdmission(firstQuest),
        firstSnapshot,
        compatibility.SafetyProfile);
    REQUIRE(firstPlan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(firstPlan.MutationAuthorization.IsVerified());
    REQUIRE(firstPlan.MutationAuthorization.Matches(
        firstSnapshot,
        firstPlan.Actions,
        firstPlan.DryRunOnly));

    const auto secondSnapshot = BuildMutationSnapshot(secondQuest);
    const auto secondPlan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildMutationAdmission(secondQuest),
        secondSnapshot,
        compatibility.SafetyProfile);
    REQUIRE(secondPlan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE_FALSE(secondPlan.MutationAuthorization.IsVerified());
}

TEST_CASE("Runtime mutation authorization is bound to the exact issued plan", "[quest.party-state.mutation-authorization]")
{
    const GameId questId(94, 0x7300);
    const auto requirement = BuildMutationRequirement(questId);
    const auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        requirement,
        BuildMutationFacts(requirement));
    REQUIRE(compatibility.IsAuthorized());

    auto snapshot = BuildMutationSnapshot(questId);
    const auto plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildMutationAdmission(questId),
        snapshot,
        compatibility.SafetyProfile);
    REQUIRE(plan.MutationAuthorization.IsVerified());

    REQUIRE_FALSE(plan.MutationAuthorization.Matches(
        snapshot,
        plan.Actions | PartyQuestApplyAction::WaitForWorldTargets,
        plan.DryRunOnly));
    REQUIRE_FALSE(plan.MutationAuthorization.Matches(
        snapshot,
        plan.Actions,
        !plan.DryRunOnly));

    snapshot.CurrentStage = 40;
    snapshot.CompletedStages.push_back(40);
    snapshot.Canonicalize();
    REQUIRE_FALSE(plan.MutationAuthorization.Matches(
        snapshot,
        plan.Actions,
        plan.DryRunOnly));
}
