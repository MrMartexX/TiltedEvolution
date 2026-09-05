#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kRecoveryAuthorityCampaign{
    0x5101510251035104ull,
    0x5201520252035204ull};
const PartyQuestPlayerProfileId kRecoveryAuthorityPlayer{
    0x5301530253035304ull,
    0x5401540254035404ull};

PartyQuestRuntimeApplyRequest BuildRecoveryAuthorityRequest(uint64_t aTransactionId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(120, static_cast<uint32_t>(0x2000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 5;
    snapshot.InitiatorPlayerId = 9;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 9200 + aTransactionId;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE_FALSE(request.Plan.DryRunOnly);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

PartyQuestRuntimeApplySession BuildRecoveryAuthoritySession()
{
    return PartyQuestRuntimeApplySession(
        kRecoveryAuthorityCampaign,
        kRecoveryAuthorityPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
}

void AdvanceToPossibleMutation(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    REQUIRE(aSession.Begin(acRequest) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                aSession,
                acRequest.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::ArmRuntimeMutation(
                aSession,
                acRequest.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(aSession.GetCoordinator().GetActive() != nullptr);
    REQUIRE(aSession.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}
} // namespace

TEST_CASE("Transaction id cannot directly clear a live post-mutation recovery barrier", "[quest.party-state.runtime-recovery][authority]")
{
    constexpr uint64_t transactionId = 93001;
    const auto request = BuildRecoveryAuthorityRequest(transactionId);

    SECTION("direct runtime session completion fails closed")
    {
        auto session = BuildRecoveryAuthoritySession();
        AdvanceToPossibleMutation(session, request);

        REQUIRE(session.CompleteLiveCheckpointRestore(transactionId) ==
            PartyQuestRuntimeDurableTransitionStatus::InvalidState);
        REQUIRE(session.GetCoordinator().GetActive() != nullptr);
        REQUIRE(session.GetCoordinator().GetActive()->TransactionId == transactionId);
        REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    }

    SECTION("legacy guarded shortcut cannot substitute SaveGuard for restored bytes")
    {
        auto session = BuildRecoveryAuthoritySession();
        PartyQuestSaveGuard saveGuard;
        PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

        REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
        REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                    session,
                    transactionId) ==
            PartyQuestRuntimeDurableTransitionStatus::Applied);
        REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
            PartyQuestRuntimeGuardStatus::Ready);
        REQUIRE(saveGuard.GetTransactionId() == transactionId);

        const auto rejected = guarded.CompleteLiveCheckpointRestore(transactionId);
        REQUIRE(rejected.Status == PartyQuestRuntimeGuardStatus::InvalidState);
        REQUIRE(rejected.TransitionStatus ==
            PartyQuestRuntimeDurableTransitionStatus::InvalidState);
        REQUIRE(rejected.GuardHeld);
        REQUIRE(saveGuard.GetTransactionId() == transactionId);
        REQUIRE(session.GetCoordinator().GetActive() != nullptr);
        REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
        REQUIRE(saveGuard.Release(transactionId));
    }
}

TEST_CASE("Transaction id cannot directly clear a persisted crash recovery barrier", "[quest.party-state.runtime-recovery][authority]")
{
    constexpr uint64_t transactionId = 93002;
    const auto request = BuildRecoveryAuthorityRequest(transactionId);

    PartyQuestRuntimeApplyCoordinator crashed;
    REQUIRE(crashed.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(crashed.MarkCheckpointCreated(transactionId));
    REQUIRE(crashed.MarkApplyDispatched(transactionId));
    const auto recoveryState = crashed.ExportRecoveryState(
        kRecoveryAuthorityCampaign,
        kRecoveryAuthorityPlayer);

    auto session = BuildRecoveryAuthoritySession();
    REQUIRE(session.RestoreRecoveryState(recoveryState) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);

    REQUIRE(session.CompleteCrashCheckpointRestore(transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::InvalidState);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);
    REQUIRE(session.GetCoordinator().GetRecoveryRecord()->TransactionId == transactionId);
}
