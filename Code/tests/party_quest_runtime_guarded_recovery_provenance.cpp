#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kCampaign{0xA1A2A3A4A5A6A7A8ull, 0xB1B2B3B4B5B6B7B8ull};
const PartyQuestPlayerProfileId kPlayer{0xC1C2C3C4C5C6C7C8ull, 0xD1D2D3D4D5D6D7D8ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_guarded_recovery_provenance_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~Sandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildPaths(const Sandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());
    REQUIRE(PartyQuestCoopSaveLayout::Matches(*paths, kCampaign, kPlayer));
    return *paths;
}

PartyQuestRuntimeRecoveryState BuildPostMutationState(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(88, 0x3300);
    active.CanonicalDigest = 0x1122334455667788ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions,
        active.CanonicalDigest,
        0x88003300);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.Active = active;
    return state;
}

PartyQuestRuntimeApplyRequest BuildRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(88, 0x3400);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 12;
    snapshot.InitiatorPlayerId = 23;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}
} // namespace

TEST_CASE(
    "guard-busy crash recovery does not invent a restore identity",
    "[quest.party-state.runtime-recovery][runtime-guard][provenance][crash]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 28311;
    constexpr uint64_t worldRevision = 1960;
    constexpr uint64_t occupyingTransactionId = 98311;

    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE(session.RestoreRecoveryState(
                BuildPostMutationState(transactionId, worldRevision)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());

    PartyQuestSaveGuard saveGuard;
    REQUIRE(saveGuard.Acquire(occupyingTransactionId) ==
        PartyQuestSaveGuardAcquireStatus::Acquired);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    const auto result = guarded.ResolveCrashRecovery(paths);
    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::SaveGuardBusy);
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.TargetWorldRevision == worldRevision);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);
    REQUIRE(result.RestoreId == 0);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(saveGuard.GetTransactionId() == occupyingTransactionId);
}

TEST_CASE(
    "rejected local-guard live recovery does not invent a restore identity",
    "[quest.party-state.runtime-recovery][runtime-guard][provenance][live-recovery]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 28312;
    constexpr uint64_t worldRevision = 1970;

    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    const auto request = BuildRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(saveGuard.GetTransactionId() == transactionId);

    const auto result = guarded.ResolveLiveRecovery(paths);
    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState);
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.TargetWorldRevision == worldRevision);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);
    REQUIRE(result.RestoreId == 0);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->TransactionId == transactionId);
    REQUIRE(saveGuard.GetTransactionId() == transactionId);
}
