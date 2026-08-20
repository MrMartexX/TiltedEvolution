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
const PartyQuestCampaignId kCampaign{0x737475767778797Aull, 0x838485868788898Aull};
const PartyQuestPlayerProfileId kPlayer{0x939495969798999Aull, 0xA3A4A5A6A7A8A9AAull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_live_attempt_domain_conflict_" +
             std::to_string(nonce));
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

void WriteText(const std::filesystem::path& acPath, const std::string& acText)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(acText.data(), static_cast<std::streamsize>(acText.size()));
    file.flush();
    REQUIRE(file.good());
}

std::string ReadText(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestRuntimeApplyRequest BuildRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(100, 0x2A00);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 10;
    snapshot.InitiatorPlayerId = 22;
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

PartyQuestReplicaRestorePlan PublishCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto liveSave = acPaths.SavesDirectory / "Hero.ess";
    WriteText(liveSave, "PRE_REPAIR_LIVE_DOMAIN_CONFLICT");
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

    PartyQuestReplicaSnapshotManager manager(acPaths, kCampaign, kPlayer);
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
        kCampaign,
        kPlayer,
        *loaded.Manifest);
    REQUIRE(restorePlan.IsReady());
    return restorePlan;
}

PartyQuestRuntimeRestoreAttemptResult InitializeStrongAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId)
{
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(acPaths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability =
        lease.CreatePublicationCapability(acPaths, kCampaign, kPlayer);
    REQUIRE(capability.Protects(acPaths, kCampaign, kPlayer));
    return PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        acPaths,
        kCampaign,
        kPlayer,
        aTransactionId,
        capability);
}

std::filesystem::path PersistLegacyJournal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aTransactionId)
{
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        acPaths,
        acPlan,
        aTransactionId);
    REQUIRE(prepared.IsReady());
    REQUIRE(prepared.State.has_value());

    const auto journalPath =
        PartyQuestReplicaRestoreJournal::GetJournalPath(*prepared.State);
    std::error_code ec;
    std::filesystem::create_directories(
        prepared.State->TransactionDirectory,
        ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                *prepared.State) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto loaded =
        PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    return journalPath;
}
} // namespace

TEST_CASE(
    "corrupt strong attempt evidence cannot downgrade live recovery to legacy journal",
    "[quest.party-state.runtime-recovery][live-recovery][restore-journal-domain][restore-attempt][conflict]")
{
    Sandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 28102;
    constexpr uint64_t worldRevision = 1900;
    const auto restorePlan = PublishCheckpoint(*paths, worldRevision);
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    const std::string liveBytes = "MUTATED_LIVE_DOMAIN_CONFLICT";
    WriteText(liveSave, liveBytes);

    const auto strongAttempt = InitializeStrongAttempt(*paths, transactionId);
#ifdef _WIN32
    REQUIRE(strongAttempt.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(strongAttempt.IsUsable());
    REQUIRE(strongAttempt.State.has_value());
    REQUIRE(strongAttempt.State->CurrentRestoreId != 0);
    REQUIRE(strongAttempt.State->CurrentRestoreId != transactionId);

    const auto legacyJournalPath =
        PersistLegacyJournal(*paths, restorePlan, transactionId);
    REQUIRE(std::filesystem::exists(legacyJournalPath));

    WriteText(strongAttempt.StatePath, "CORRUPT_LIVE_STRONG_ATTEMPT_MAPPING");
    const auto corruptLoad = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(corruptLoad.Status == PartyQuestRuntimeRestoreAttemptStatus::InvalidData);

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kCampaign,
        kPlayer,
        *paths);
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();

    const auto request = BuildRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto result = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(result.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(result.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::InvalidData);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);

    // Conflict must not clear or weaken the live runtime safety envelope.
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(ReadText(liveSave) == liveBytes);

    // Neither conflicting evidence source is adopted or rewritten.
    REQUIRE(ReadText(strongAttempt.StatePath) ==
        "CORRUPT_LIVE_STRONG_ATTEMPT_MAPPING");
    const auto legacyAfter =
        PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
    REQUIRE(legacyAfter.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacyAfter.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    REQUIRE(legacyAfter.State.has_value());
    REQUIRE(legacyAfter.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::Prepared);
#endif
}

TEST_CASE(
    "simultaneous valid strong and legacy live recovery domains fail closed",
    "[quest.party-state.runtime-recovery][live-recovery][restore-journal-domain][restore-attempt][dual-valid][conflict]")
{
    Sandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 28103;
    constexpr uint64_t worldRevision = 1910;
    const auto restorePlan = PublishCheckpoint(*paths, worldRevision);
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    const std::string liveBytes = "MUTATED_LIVE_DUAL_VALID_DOMAIN_CONFLICT";
    WriteText(liveSave, liveBytes);

    const auto strongAttempt = InitializeStrongAttempt(*paths, transactionId);
#ifdef _WIN32
    REQUIRE(strongAttempt.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(strongAttempt.IsUsable());
    REQUIRE(strongAttempt.State.has_value());
    const auto strongState = *strongAttempt.State;
    REQUIRE(strongState.CurrentRestoreId != 0);
    REQUIRE(strongState.CurrentRestoreId != transactionId);
    REQUIRE(std::filesystem::exists(strongAttempt.StatePath));
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    const auto legacyJournalPath =
        PersistLegacyJournal(*paths, restorePlan, transactionId);
    REQUIRE(std::filesystem::exists(legacyJournalPath));
    REQUIRE(legacyJournalPath.lexically_normal() !=
        strongAttempt.JournalPath.lexically_normal());

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kCampaign,
        kPlayer,
        *paths);
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();

    const auto request = BuildRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto result = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(result.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.TargetWorldRevision == worldRevision);
    REQUIRE(result.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(result.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);

    // No restore executor may run and the physical process safety envelope must
    // remain held when two independently valid durability domains conflict.
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->TransactionId == transactionId);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(ReadText(liveSave) == liveBytes);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    // Conflict detection must preserve both evidence sources exactly so a later
    // authority/provenance fix can resolve the ambiguity without information loss.
    const auto strongAfter = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(strongAfter.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(strongAfter.State.has_value());
    REQUIRE(*strongAfter.State == strongState);

    const auto legacyAfter =
        PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
    REQUIRE(legacyAfter.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacyAfter.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    REQUIRE(legacyAfter.State.has_value());
    REQUIRE(legacyAfter.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::Prepared);
#endif
}
