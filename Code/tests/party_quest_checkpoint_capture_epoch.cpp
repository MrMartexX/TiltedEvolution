#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_runtime_safety_test_access.h>
#include <party_quest_pre_repair_checkpoint_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kEpochCampaign{0xE001E002E003E004ull, 0xE101E102E103E104ull};
const PartyQuestPlayerProfileId kEpochPlayer{0xE201E202E203E204ull, 0xE301E302E303E304ull};

PartyQuestRuntimeApplyRequest BuildEpochRequest(uint64_t aTransactionId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(91, static_cast<uint32_t>(0x5000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 3;
    snapshot.InitiatorPlayerId = 15;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 48000 + aTransactionId;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    return request;
}

PartyQuestRuntimeApplySession BuildEpochSession()
{
    return PartyQuestRuntimeApplySession(
        kEpochCampaign,
        kEpochPlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
}
} // namespace

TEST_CASE("Checkpoint capture epoch is unique and blocks mutation until checkpoint publication closes it", "[quest.party-state.capture-epoch]")
{
    auto session = BuildEpochSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildEpochRequest(27001);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto epoch = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(epoch.IsReady());
    REQUIRE(epoch.Epoch.GetEpochId() != 0);
    REQUIRE(epoch.Epoch.MatchesContext(
        request.TransactionId,
        request.TargetWorldRevision,
        request.SidecarManifestFingerprint));
    REQUIRE(guarded.IsCheckpointCaptureEpochActive(epoch.Epoch));

    const auto duplicate = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(duplicate.Status == PartyQuestCheckpointCaptureEpochStatus::AlreadyActive);
    REQUIRE_FALSE(duplicate.Epoch.IsVerified());

    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);

    const auto blocked = guarded.ArmRuntimeMutation(request.TransactionId);
    REQUIRE(blocked.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(blocked.GuardHeld);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);

    REQUIRE(guarded.CompleteCheckpointCaptureEpoch(epoch.Epoch));
    REQUIRE_FALSE(guarded.IsCheckpointCaptureEpochActive(epoch.Epoch));

    const auto armed = guarded.ArmRuntimeMutation(request.TransactionId);
    REQUIRE(armed.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}

TEST_CASE("Checkpoint capture epoch can be explicitly abandoned and recaptured before mutation", "[quest.party-state.capture-epoch]")
{
    auto session = BuildEpochSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildEpochRequest(27002);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto first = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(first.IsReady());
    REQUIRE(guarded.AbortCheckpointCaptureEpoch(first.Epoch));
    REQUIRE_FALSE(guarded.IsCheckpointCaptureEpochActive(first.Epoch));

    const auto second = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(second.IsReady());
    REQUIRE(second.Epoch.GetEpochId() != first.Epoch.GetEpochId());
    REQUIRE(guarded.IsCheckpointCaptureEpochActive(second.Epoch));
    REQUIRE_FALSE(guarded.CompleteCheckpointCaptureEpoch(second.Epoch));

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(aborted.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE_FALSE(saveGuard.IsActive());
    REQUIRE_FALSE(guarded.IsCheckpointCaptureEpochActive(second.Epoch));
}

TEST_CASE("Capture epoch cannot start without the exact guarded AwaitingCheckpoint state", "[quest.party-state.capture-epoch]")
{
    auto session = BuildEpochSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    REQUIRE(guarded.BeginCheckpointCaptureEpoch().Status ==
        PartyQuestCheckpointCaptureEpochStatus::InvalidRuntimeState);

    const auto request = BuildEpochRequest(27003);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(saveGuard.Release(request.TransactionId));

    const auto noGuard = guarded.BeginCheckpointCaptureEpoch();
    REQUIRE(noGuard.Status == PartyQuestCheckpointCaptureEpochStatus::GuardMismatch);
    REQUIRE_FALSE(noGuard.Epoch.IsVerified());
}

TEST_CASE("Expired checkpoint capture epoch loses publication authority", "[quest.party-state.capture-epoch]")
{
    const auto manifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    const auto epoch =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeExpiredEpoch(
            1,
            2,
            3,
            manifestFingerprint);

    REQUIRE(epoch.IsVerified());
    REQUIRE(epoch.IsExpired());
    REQUIRE_FALSE(epoch.MatchesContext(2, 3, manifestFingerprint));
}
