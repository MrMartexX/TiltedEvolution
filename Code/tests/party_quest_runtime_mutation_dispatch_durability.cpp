#include <Structs/Skyrim/PartyQuestRuntimeMutationDispatch.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xAB11AB12AB13AB14ull,
    0xAB21AB22AB23AB24ull};
const PartyQuestPlayerProfileId kPlayer{
    0xAC11AC12AC13AC14ull,
    0xAC21AC22AC23AC24ull};

PartyQuestRuntimeCompatibilityRequirement BuildRequirement(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 31;
    requirement.ResolvedRecordFingerprint = 0xB101B102B103B104ull;
    requirement.WinningOverrideFingerprint = 0xB201B202B203B204ull;
    requirement.ScriptFingerprint = 0xB301B302B303B304ull;
    requirement.NativeAdapterFingerprint = 0xB401B402B403B404ull;
    requirement.AdapterMutationComponents = PartyQuestVerificationComponent::QuestSnapshot;
    return requirement;
}

PartyQuestRuntimeCompatibilityFacts BuildFacts(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = acRequirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = acRequirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = acRequirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = acRequirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = acRequirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = acRequirement.AdapterMutationComponents;
    return facts;
}

PartyQuestRuntimeApplyRequest BuildRequest(
    uint64_t aTransactionId,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    const auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        acRequirement,
        BuildFacts(acRequirement));
    REQUIRE(compatibility.IsAuthorized());

    QuestSnapshot snapshot;
    snapshot.QuestId = acRequirement.QuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 70;
    snapshot.Revision = 8;
    snapshot.InitiatorPlayerId = 51;
    snapshot.CompletedStages = {10, 40, 70};
    snapshot.Objectives = {{70, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 4100 + aTransactionId;
    request.SidecarManifestFingerprint = 0xC101C102C103C104ull;
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlanWithCompatibilityFingerprint(
        request.Plan,
        snapshot,
        compatibility.SafetyProfile.GetCompatibilityFingerprint());
    REQUIRE_FALSE(request.Plan.DryRunOnly);
    return request;
}
}

TEST_CASE(
    "runtime mutation dispatch refuses process-crash durability before observation or arm",
    "[quest.party-state.runtime-dispatch][durability]")
{
    const auto requirement = BuildRequirement(GameId(96, 0x9100));
    const auto request = BuildRequest(26101, requirement);
    const auto facts = BuildFacts(requirement);

    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGenerationFence generationFence;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);

    size_t observations{};
    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        requirement,
        generationFence,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            ++observations;
            return facts;
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status == PartyQuestRuntimeMutationDispatchStatus::ArmFailed);
    REQUIRE(result.ArmResult.Status == PartyQuestRuntimeGuardStatus::InsufficientDurability);
    REQUIRE_FALSE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE(observations == 0);
    REQUIRE(executions == 0);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(saveGuard.IsActive());
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
}
