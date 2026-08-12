#include <Structs/Skyrim/PartyQuestRuntimeMutationDispatch.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kDispatchCampaign{
    0x91A191A291A391A4ull,
    0x91B191B291B391B4ull};
const PartyQuestPlayerProfileId kDispatchPlayer{
    0x92A192A292A392A4ull,
    0x92B192B292B392B4ull};

PartyQuestRuntimeCompatibilityRequirement BuildRequirement(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 17;
    requirement.ResolvedRecordFingerprint = 0xA101A102A103A104ull;
    requirement.WinningOverrideFingerprint = 0xA201A202A203A204ull;
    requirement.ScriptFingerprint = 0xA301A302A303A304ull;
    requirement.NativeAdapterFingerprint = 0xA401A402A403A404ull;
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
    snapshot.CurrentStage = 60;
    snapshot.Revision = 7;
    snapshot.InitiatorPlayerId = 41;
    snapshot.CompletedStages = {10, 30, 60};
    snapshot.Objectives = {{60, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 3000 + aTransactionId;
    request.SidecarManifestFingerprint = 0xB101B102B103B104ull;
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
    return request;
}

PartyQuestRuntimeApplySession BuildSession()
{
    return PartyQuestRuntimeApplySession(
        kDispatchCampaign,
        kDispatchPlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
}

void PrepareCheckpoint(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    REQUIRE(aGuarded.Begin(acRequest).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                aSession,
                acRequest.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    const auto* active = aSession.GetCoordinator().GetActive();
    REQUIRE(active != nullptr);
    REQUIRE(active->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE(active->CheckpointCreated);
    REQUIRE_FALSE(active->RuntimeMutationMayHaveOccurred);
}
} // namespace

TEST_CASE("Runtime mutation dispatch revalidates compatibility immediately around durable arm",
    "[quest.party-state.runtime-dispatch]")
{
    const auto requirement = BuildRequirement(GameId(95, 0x8100));
    const auto facts = BuildFacts(requirement);
    const auto request = BuildRequest(26001, requirement);
    PartyQuestSaveGuard saveGuard;
    auto session = BuildSession();
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    PrepareCheckpoint(guarded, session, request);

    size_t observations{};
    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        requirement,
        [&](const GameId& acQuestId) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            REQUIRE(acQuestId == requirement.QuestId);
            ++observations;
            return facts;
        },
        [&](const PartyQuestRuntimeApplyRequest& acDispatched)
        {
            ++executions;
            REQUIRE(acDispatched.TransactionId == request.TransactionId);
            return true;
        });

    REQUIRE(result.Status == PartyQuestRuntimeMutationDispatchStatus::Dispatched);
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::Authorized);
    REQUIRE(result.MutationBarrierArmed);
    REQUIRE(result.MutationInvoked);
    REQUIRE(result.WasDispatched());
    REQUIRE(observations == 2);
    REQUIRE(executions == 1);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}

TEST_CASE("Stale compatibility before arm cannot publish mutation barrier or invoke executor",
    "[quest.party-state.runtime-dispatch]")
{
    const auto requirement = BuildRequirement(GameId(95, 0x8200));
    auto staleFacts = BuildFacts(requirement);
    ++staleFacts.WinningOverrideFingerprint;
    const auto request = BuildRequest(26002, requirement);
    PartyQuestSaveGuard saveGuard;
    auto session = BuildSession();
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    PrepareCheckpoint(guarded, session, request);

    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        requirement,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            return staleFacts;
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status == PartyQuestRuntimeMutationDispatchStatus::CompatibilityRejected);
    REQUIRE(result.CompatibilityStatus ==
        PartyQuestRuntimeCompatibilityStatus::WinningOverrideMismatch);
    REQUIRE_FALSE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE(executions == 0);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}

TEST_CASE("Compatibility change during durable arm fails closed behind recovery barrier",
    "[quest.party-state.runtime-dispatch]")
{
    const auto requirement = BuildRequirement(GameId(95, 0x8300));
    const auto currentFacts = BuildFacts(requirement);
    auto changedFacts = currentFacts;
    ++changedFacts.ScriptFingerprint;
    const auto request = BuildRequest(26003, requirement);
    PartyQuestSaveGuard saveGuard;
    auto session = BuildSession();
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    PrepareCheckpoint(guarded, session, request);

    size_t observations{};
    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        requirement,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            ++observations;
            return observations == 1 ? currentFacts : changedFacts;
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status == PartyQuestRuntimeMutationDispatchStatus::CompatibilityRejected);
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::ScriptMismatch);
    REQUIRE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE(observations == 2);
    REQUIRE(executions == 0);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(guarded.AbortBeforeMutation(request.TransactionId).Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
}

TEST_CASE("Dispatch compatibility must match exact authority already bound to transaction",
    "[quest.party-state.runtime-dispatch]")
{
    const auto requirement = BuildRequirement(GameId(95, 0x8400));
    const auto request = BuildRequest(26004, requirement);
    auto differentRequirement = requirement;
    ++differentRequirement.NativeAdapterFingerprint;
    const auto differentFacts = BuildFacts(differentRequirement);
    PartyQuestSaveGuard saveGuard;
    auto session = BuildSession();
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    PrepareCheckpoint(guarded, session, request);

    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        differentRequirement,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            return differentFacts;
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status ==
        PartyQuestRuntimeMutationDispatchStatus::CompatibilityAuthorityMismatch);
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::Authorized);
    REQUIRE_FALSE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE(executions == 0);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}

TEST_CASE("Physical guard loss after final observation blocks mutation callback",
    "[quest.party-state.runtime-dispatch]")
{
    const auto requirement = BuildRequirement(GameId(95, 0x8500));
    const auto facts = BuildFacts(requirement);
    const auto request = BuildRequest(26005, requirement);
    PartyQuestSaveGuard saveGuard;
    auto session = BuildSession();
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    PrepareCheckpoint(guarded, session, request);

    size_t observations{};
    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        guarded,
        request,
        requirement,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            ++observations;
            if (observations == 2)
                REQUIRE(saveGuard.Release(request.TransactionId));
            return facts;
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status == PartyQuestRuntimeMutationDispatchStatus::DispatchContextLost);
    REQUIRE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE(observations == 2);
    REQUIRE(executions == 0);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}
