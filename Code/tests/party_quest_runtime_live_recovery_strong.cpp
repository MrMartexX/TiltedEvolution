#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_process_owner_test_support.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kStrongLiveCampaign{
    0x7312731273127312ull,
    0x8413841384138413ull};
const PartyQuestPlayerProfileId kStrongLivePlayer{
    0x9514951495149514ull,
    0xA615A615A615A615ull};

struct StrongLiveSandbox
{
    std::filesystem::path Root;

    StrongLiveSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_strong_live_recovery_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~StrongLiveSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteStrongLiveBytes(
    const std::filesystem::path& acPath,
    const std::string& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(acBytes.data(), static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}

std::string ReadStrongLiveBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestRuntimeApplyRequest BuildStrongLiveRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(99, 0x2900);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 9;
    snapshot.InitiatorPlayerId = 21;
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

PartyQuestReplicaRestorePlan PublishStrongLiveCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto liveSave = acPaths.SavesDirectory / "Hero.ess";
    WriteStrongLiveBytes(liveSave, "PRE_REPAIR_STRONG_LIVE");
    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        liveSave,
        "Hero.ess");
    REQUIRE(spec.has_value());
    const auto checkpointPlan =
        PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision,
            {*spec});
    REQUIRE(checkpointPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(
        acPaths,
        kStrongLiveCampaign,
        kStrongLivePlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                aWorldRevision,
                checkpointPlan).IsReady());

    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    const auto restorePlan = PartyQuestReplicaRestorePlanner::Build(
        acPaths,
        kStrongLiveCampaign,
        kStrongLivePlayer,
        *loaded.Manifest);
    REQUIRE(restorePlan.IsReady());
    return restorePlan;
}
} // namespace

TEST_CASE(
    "Strong live rollback advances one attempt while process guard remains held",
    "[quest.party-state.runtime-recovery][live-recovery][durability][retry]")
{
    StrongLiveSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kStrongLiveCampaign,
        kStrongLivePlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 28101;
    constexpr uint64_t worldRevision = 1890;
    const auto restorePlan = PublishStrongLiveCheckpoint(*paths, worldRevision);
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteStrongLiveBytes(liveSave, "MUTATED_STRONG_LIVE");

#ifdef _WIN32
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(*paths, kStrongLiveCampaign, kStrongLivePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        *paths,
        kStrongLiveCampaign,
        kStrongLivePlayer);
    const auto unsupported =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            *paths,
            kStrongLiveCampaign,
            kStrongLivePlayer,
            transactionId,
            capability);
    REQUIRE(unsupported.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    uint64_t firstRestoreId{};
    {
        PartyQuestReplicaWorkspaceLease lease;
        REQUIRE(lease.Acquire(*paths, kStrongLiveCampaign, kStrongLivePlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
        const auto capability = lease.CreatePublicationCapability(
            *paths,
            kStrongLiveCampaign,
            kStrongLivePlayer);
        REQUIRE(capability.Protects(
            *paths,
            kStrongLiveCampaign,
            kStrongLivePlayer));
        const auto attempt =
            PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
                *paths,
                kStrongLiveCampaign,
                kStrongLivePlayer,
                transactionId,
                capability);
        REQUIRE(attempt.IsUsable());
        REQUIRE(attempt.State.has_value());
        firstRestoreId = attempt.State->CurrentRestoreId;
    }

    const auto prepared = PartyQuestReplicaDurableRestorePreparation::Prepare(
        *paths,
        restorePlan,
        firstRestoreId);
    REQUIRE(prepared.IsBackupsReady());
    REQUIRE(prepared.State.has_value());

    auto mutationStarted = *prepared.State;
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(mutationStarted) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                prepared.JournalPath,
                mutationStarted) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kStrongLiveCampaign,
        kStrongLivePlayer,
        *paths);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();

    const auto request = BuildStrongLiveRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto rollback = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(rollback.Status ==
        PartyQuestRuntimeRecoveryStatus::RollbackRecoveredRetryRequired);
    REQUIRE(rollback.TransactionId == transactionId);
    REQUIRE(rollback.RestoreId == firstRestoreId);
    REQUIRE(rollback.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable);
    REQUIRE(rollback.DurableRestoreStatus ==
        PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(rollback.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(ReadStrongLiveBytes(liveSave) == "MUTATED_STRONG_LIVE");

    const auto terminal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            prepared.JournalPath);
    REQUIRE(terminal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(terminal.State.has_value());
    REQUIRE(terminal.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::RolledBack);

    const auto advanced = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kStrongLiveCampaign,
        kStrongLivePlayer,
        transactionId);
    REQUIRE(advanced.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(advanced.State.has_value());
    REQUIRE(advanced.State->CurrentOrdinal == 1);
    REQUIRE(advanced.State->LastRolledBackRestoreId == firstRestoreId);
    REQUIRE(advanced.State->CurrentRestoreId != firstRestoreId);
    const uint64_t secondRestoreId = advanced.State->CurrentRestoreId;

    const auto retried = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(retried.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(retried.TransactionId == transactionId);
    REQUIRE(retried.RestoreId == secondRestoreId);
    REQUIRE(retried.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable);
    REQUIRE(retried.DurableRestoreStatus ==
        PartyQuestReplicaDurableRestoreStatus::Success);
    REQUIRE(ReadStrongLiveBytes(liveSave) == "PRE_REPAIR_STRONG_LIVE");
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(processGuard.IsActive());

    const auto oldTerminal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            prepared.JournalPath);
    REQUIRE(oldTerminal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(oldTerminal.State.has_value());
    REQUIRE(oldTerminal.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(std::filesystem::exists(oldTerminal.State->TransactionDirectory));
#endif
}