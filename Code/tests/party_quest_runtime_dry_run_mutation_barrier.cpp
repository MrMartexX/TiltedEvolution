#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kDryRunCampaign{
    0xA0A1A2A3A4A5A6A7ull,
    0xB0B1B2B3B4B5B6B7ull};
const PartyQuestPlayerProfileId kDryRunPlayer{
    0xC0C1C2C3C4C5C6C7ull,
    0xD0D1D2D3D4D5D6D7ull};

PartyQuestRuntimeSafetyProfile BuildDryRunProfile(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 19;
    requirement.ResolvedRecordFingerprint = 0x1111222233334444ull;
    requirement.WinningOverrideFingerprint = 0x2222333344445555ull;
    requirement.ScriptFingerprint = 0x3333444455556666ull;
    requirement.NativeAdapterFingerprint = 0x4444555566667777ull;
    requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        requirement,
        facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestRuntimeApplyRequest BuildProductionDryRunRequest()
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(119, 0x1200);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 4;
    snapshot.InitiatorPlayerId = 7;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(
        snapshot.QuestId,
        facts);
    REQUIRE(admission.IsAdmitted());

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = 912001;
    request.TargetWorldRevision = 9120;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        admission,
        snapshot,
        BuildDryRunProfile(snapshot.QuestId));
    REQUIRE(request.Plan.Safety.Status ==
        PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(request.Plan.DryRunOnly);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(request.Plan.MutationAuthorization.Matches(
        request.CanonicalSnapshot,
        request.Plan.Actions,
        true));
    return request;
}
} // namespace

TEST_CASE("Production BuildApplyPlan cannot arm runtime mutation while DryRunOnly", "[quest.party-state.runtime-apply.session][mutation-authority][dry-run]")
{
    const auto request = BuildProductionDryRunRequest();
    PartyQuestRuntimeApplySession session(
        kDryRunCampaign,
        kDryRunPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    // Low-level test access models the durable checkpoint bit only. Even with
    // that bit present, the real production dry-run plan must not mint the
    // process-local executable authority required by ArmRuntimeMutation().
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);

    REQUIRE(session.ArmRuntimeMutation(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::InvalidState);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE_FALSE(
        session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}
