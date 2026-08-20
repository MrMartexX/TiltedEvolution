#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <party_quest_runtime_recovery_coordinator_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{0x7273747576777879ull, 0x8283848586878889ull};
const PartyQuestPlayerProfileId kPlayer{0x9293949596979899ull, 0xA2A3A4A5A6A7A8A9ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_attempt_domain_conflict_" +
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

PartyQuestCoopSavePaths BuildPaths(const Sandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns", kCampaign, kPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

void PublishCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision,
    const std::string& acBytes)
{
    WriteText(acPaths.SavesDirectory / "Hero.ess", acBytes);
    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        acPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    REQUIRE(spec.has_value());
    const auto copyPlan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        acPaths,
        PartyQuestCheckpointKind::PreRepair,
        aWorldRevision,
        {*spec});
    REQUIRE(copyPlan.IsReady());
    PartyQuestReplicaSnapshotManager manager(acPaths, kCampaign, kPlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                aWorldRevision,
                copyPlan).IsReady());
}

PartyQuestReplicaRestorePlan LoadRestorePlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        acPaths, kCampaign, kPlayer, *loaded.Manifest);
    REQUIRE(plan.IsReady());
    return plan;
}

PartyQuestRuntimeRecoveryState BuildRecoveryState(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(52, 0x1000);
    active.CanonicalDigest = 0xDCBBAA9988776655ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions,
        active.CanonicalDigest,
        0x72001001);
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

PartyQuestRuntimeApplySession BuildBlockedSession(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE(session.RestoreRecoveryState(
                BuildRecoveryState(aTransactionId, aWorldRevision)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    return session;
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
    uint64_t aWorldRevision,
    uint64_t aTransactionId)
{
    const auto plan = LoadRestorePlan(acPaths, aWorldRevision);
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        acPaths,
        plan,
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
    "corrupt strong attempt evidence cannot downgrade recovery to legacy journal",
    "[quest.party-state.runtime-recovery][restore-journal-domain][restore-attempt][conflict]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t revision = 1730;
    constexpr uint64_t transactionId = 27103;
    const std::string liveBytes = "LIVE_MUTATED_DOMAIN_1730";

    PublishCheckpoint(paths, revision, "PRE_REPAIR_DOMAIN_1730");
    WriteText(paths.SavesDirectory / "Hero.ess", liveBytes);

    const auto strongAttempt = InitializeStrongAttempt(paths, transactionId);
#ifdef _WIN32
    REQUIRE(strongAttempt.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(strongAttempt.IsUsable());
    REQUIRE(strongAttempt.State.has_value());
    REQUIRE(strongAttempt.State->CurrentRestoreId != 0);
    REQUIRE(strongAttempt.State->CurrentRestoreId != transactionId);
    REQUIRE(std::filesystem::exists(strongAttempt.StatePath));

    const auto legacyJournalPath =
        PersistLegacyJournal(paths, revision, transactionId);
    REQUIRE(std::filesystem::exists(legacyJournalPath));

    // The persisted strong mapping is authoritative local recovery evidence.
    // Corrupt it after a valid v3 journal exists: recovery must not reinterpret
    // the transaction-id directory as permission to downgrade to legacy.
    WriteText(strongAttempt.StatePath, "CORRUPT_STRONG_ATTEMPT_MAPPING");
    const auto corruptLoad = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(corruptLoad.Status == PartyQuestRuntimeRestoreAttemptStatus::InvalidData);

    auto session = BuildBlockedSession(transactionId, revision);
    const auto result =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
            session,
            paths);

    REQUIRE(result.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(result.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::InvalidData);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == liveBytes);

    // Neither evidence source may be rewritten or consumed on conflict.
    REQUIRE(ReadText(strongAttempt.StatePath) ==
        "CORRUPT_STRONG_ATTEMPT_MAPPING");
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
    "simultaneous valid strong and legacy recovery domains fail closed",
    "[quest.party-state.runtime-recovery][restore-journal-domain][restore-attempt][dual-valid][conflict]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t revision = 1740;
    constexpr uint64_t transactionId = 27104;
    const std::string liveBytes = "LIVE_MUTATED_DOMAIN_1740";

    PublishCheckpoint(paths, revision, "PRE_REPAIR_DOMAIN_1740");
    WriteText(paths.SavesDirectory / "Hero.ess", liveBytes);

    const auto strongAttempt = InitializeStrongAttempt(paths, transactionId);
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
        PersistLegacyJournal(paths, revision, transactionId);
    REQUIRE(std::filesystem::exists(legacyJournalPath));
    REQUIRE(legacyJournalPath.lexically_normal() !=
        strongAttempt.JournalPath.lexically_normal());

    auto session = BuildBlockedSession(transactionId, revision);
    const auto result =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
            session,
            paths);

    REQUIRE(result.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(result.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable);
    REQUIRE(result.RestoreId == strongState.CurrentRestoreId);
    REQUIRE(result.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == liveBytes);

    // Conflict detection must not create a strong journal or consume either
    // valid evidence source. Both domains remain inspectable for diagnosis.
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));
    const auto strongAfter = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths,
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
