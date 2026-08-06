#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kConsistencyCampaign{
    0xCA110001CA110002ull,
    0xCA110003CA110004ull};
const PartyQuestPlayerProfileId kConsistencyPlayer{
    0xCA220001CA220002ull,
    0xCA220003CA220004ull};

PartyQuestCheckpointSidecarRequirement BuildConsistencyRequirement()
{
    PartyQuestCheckpointSidecarRequirement requirement;
    requirement.CapabilityId = 0x434F484552454E54ull;
    requirement.SchemaVersion = 4;
    requirement.ProviderFingerprint = 0xABCD0001ABCD0002ull;
    requirement.RestoreAdapterFingerprint = 0xABCD0003ABCD0004ull;
    requirement.Mode = PartyQuestCheckpointSidecarRequirementMode::Required;
    return requirement;
}

PartyQuestCheckpointSidecarFacts BuildConsistencyFacts(
    PartyQuestCheckpointSidecarCaptureConsistency aConsistency)
{
    const auto requirement = BuildConsistencyRequirement();
    PartyQuestCheckpointSidecarFacts facts;
    facts.CapabilityId = requirement.CapabilityId;
    facts.SchemaVersion = requirement.SchemaVersion;
    facts.ProviderFingerprint = requirement.ProviderFingerprint;
    facts.RestoreAdapterFingerprint = requirement.RestoreAdapterFingerprint;
    facts.CaptureConsistency = aConsistency;
    facts.CaptureAvailable = true;
    facts.RestoreAvailable = true;
    return facts;
}

PartyQuestRuntimeApplyRequest BuildConsistencyRequest(
    uint64_t aTransactionId,
    const PartyQuestCheckpointSidecarManifest& acManifest)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(92, static_cast<uint32_t>(0x6100 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 2;
    snapshot.InitiatorPlayerId = 21;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 59000 + aTransactionId;
    request.SidecarManifestFingerprint = acManifest.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    return request;
}
} // namespace

TEST_CASE("Sidecar provider explicitly declares its capture consistency contract", "[quest.party-state.sidecar-consistency]")
{
    const auto requirement = BuildConsistencyRequirement();

    SECTION("legacy unspecified capability remains diagnostic-only")
    {
        const auto facts = BuildConsistencyFacts(
            PartyQuestCheckpointSidecarCaptureConsistency::Unspecified);
        const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
            requirement,
            &facts);
        REQUIRE(decision.IsAuthorized());
        REQUIRE(decision.Authorization.GetCaptureConsistency() ==
            PartyQuestCheckpointSidecarCaptureConsistency::Unspecified);
        REQUIRE_FALSE(decision.Authorization.SupportsCoherentCapture());
        REQUIRE_FALSE(decision.Authorization.RequiresEpochFreeze());
    }

    SECTION("atomic snapshot is production-coherent")
    {
        const auto facts = BuildConsistencyFacts(
            PartyQuestCheckpointSidecarCaptureConsistency::AtomicSnapshot);
        const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
            requirement,
            &facts);
        REQUIRE(decision.IsAuthorized());
        REQUIRE(decision.Authorization.GetCaptureConsistency() ==
            PartyQuestCheckpointSidecarCaptureConsistency::AtomicSnapshot);
        REQUIRE(decision.Authorization.SupportsCoherentCapture());
        REQUIRE_FALSE(decision.Authorization.RequiresEpochFreeze());
    }

    SECTION("freeze-required provider is recognized but fail-closed until lease orchestration exists")
    {
        const auto facts = BuildConsistencyFacts(
            PartyQuestCheckpointSidecarCaptureConsistency::FrozenUntilEpochRelease);
        const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
            requirement,
            &facts);
        REQUIRE(decision.IsAuthorized());
        REQUIRE(decision.Authorization.GetCaptureConsistency() ==
            PartyQuestCheckpointSidecarCaptureConsistency::FrozenUntilEpochRelease);
        REQUIRE_FALSE(decision.Authorization.SupportsCoherentCapture());
        REQUIRE(decision.Authorization.RequiresEpochFreeze());
    }
}

TEST_CASE("Unknown sidecar consistency mode is invalid provider evidence", "[quest.party-state.sidecar-consistency]")
{
    const auto requirement = BuildConsistencyRequirement();
    auto facts = BuildConsistencyFacts(
        PartyQuestCheckpointSidecarCaptureConsistency::AtomicSnapshot);
    facts.CaptureConsistency =
        static_cast<PartyQuestCheckpointSidecarCaptureConsistency>(0xFF);

    REQUIRE_FALSE(PartyQuestCheckpointSidecarPolicy::IsValidFacts(facts));
    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        requirement,
        &facts);
    REQUIRE(decision.Status == PartyQuestCheckpointSidecarStatus::InvalidFacts);
    REQUIRE_FALSE(decision.IsAuthorized());
}

TEST_CASE("Production epoch collector rejects sidecar consistency without a complete production capture contract", "[quest.party-state.sidecar-consistency][capture-epoch]")
{
    PartyQuestCheckpointSidecarManifest manifest;
    const auto requirement = BuildConsistencyRequirement();
    REQUIRE(manifest.AddRequirement(requirement));

    PartyQuestRuntimeApplySession session(
        kConsistencyCampaign,
        kConsistencyPlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        });
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildConsistencyRequest(28001, manifest);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto epochResult = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(epochResult.IsReady());
    const auto epoch = epochResult.Epoch;

    for (const auto unsupported : {
             PartyQuestCheckpointSidecarCaptureConsistency::Unspecified,
             PartyQuestCheckpointSidecarCaptureConsistency::FrozenUntilEpochRelease})
    {
        const auto facts = BuildConsistencyFacts(unsupported);
        const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
            requirement,
            &facts);
        REQUIRE(decision.IsAuthorized());
        REQUIRE_FALSE(decision.Authorization.SupportsCoherentCapture());

        PartyQuestCheckpointSidecarCapture capture;
        capture.Authorization = decision.Authorization;
        capture.CaptureEpochId = epoch.GetEpochId();
        capture.TransactionId = epoch.GetTransactionId();
        capture.TargetWorldRevision = epoch.GetTargetWorldRevision();
        capture.MirrorRelativeFiles = {"Capability_434F484552454E54/state.bin"};

        const PartyQuestCoopSavePaths invalidPaths;
        const auto result = PartyQuestCheckpointSidecarMirrorCollector::Collect(
            invalidPaths,
            manifest,
            epoch,
            {capture});
        REQUIRE(result.Status ==
            PartyQuestCheckpointSidecarMirrorStatus::CaptureConsistencyUnavailable);
        REQUIRE(result.FailedCapabilityId == requirement.CapabilityId);
        REQUIRE(guarded.IsCheckpointCaptureEpochActive(epoch));
    }
}
