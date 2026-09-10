#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <party_quest_runtime_recovery_coordinator_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{0x7172737475767778ull, 0x8182838485868788ull};
const PartyQuestPlayerProfileId kPlayer{0x9192939495969798ull, 0xA1A2A3A4A5A6A7A8ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_recovery_domain_" + std::to_string(nonce));
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

void WriteBytes(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(
        reinterpret_cast<const char*>(acBytes.data()),
        static_cast<std::streamsize>(acBytes.size()));
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
    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
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
    active.QuestId = GameId(51, 0x1000);
    active.CanonicalDigest = 0xCCBBAA9988776655ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions,
        active.CanonicalDigest,
        0x71001001);
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

PartyQuestReplicaRestoreJournalState PrepareJournalState(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision,
    uint64_t aRestoreId)
{
    const auto plan = LoadRestorePlan(acPaths, aWorldRevision);
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        acPaths, plan, aRestoreId);
    REQUIRE(prepared.IsReady());
    REQUIRE(prepared.State.has_value());
    return *prepared.State;
}

void RequireConflictPreservesBarrierAndLiveBytes(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const std::filesystem::path& acJournalPath,
    const std::string& acExpectedLive)
{
    const auto result = PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
        aSession, acPaths);
    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(result.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
    REQUIRE(aSession.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadText(acPaths.SavesDirectory / "Hero.ess") == acExpectedLive);
    REQUIRE(std::filesystem::exists(acJournalPath));
}
} // namespace

TEST_CASE(
    "runtime crash recovery refuses strong journal before legacy executor",
    "[quest.party-state.runtime-recovery][restore-journal-domain][strong]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t revision = 1710;
    constexpr uint64_t transactionId = 27101;
    PublishCheckpoint(paths, revision, "PRE_REPAIR_DOMAIN_1710");
    WriteText(paths.SavesDirectory / "Hero.ess", "LIVE_MUTATED_DOMAIN_1710");

    const auto state = PrepareJournalState(paths, revision, transactionId);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    std::error_code ec;
    std::filesystem::create_directories(state.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto strong =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(journalPath);
    REQUIRE(strong.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(strong.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable);

    auto session = BuildBlockedSession(transactionId, revision);
    RequireConflictPreservesBarrierAndLiveBytes(
        session,
        paths,
        journalPath,
        "LIVE_MUTATED_DOMAIN_1710");
}

TEST_CASE(
    "runtime crash recovery refuses ambiguous v2 journal without adopting it",
    "[quest.party-state.runtime-recovery][restore-journal-domain][ambiguous]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t revision = 1720;
    constexpr uint64_t transactionId = 27102;
    PublishCheckpoint(paths, revision, "PRE_REPAIR_DOMAIN_1720");
    WriteText(paths.SavesDirectory / "Hero.ess", "LIVE_MUTATED_DOMAIN_1720");

    const auto state = PrepareJournalState(paths, revision, transactionId);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    auto bytes = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE(bytes.size() > 10);
    REQUIRE(bytes[8] == 3);
    bytes[8] = 2;
    bytes[9] = 0;
    WriteBytes(journalPath, bytes);

    const auto decoded = PartyQuestReplicaRestoreJournalPersistence::Decode(bytes);
    REQUIRE(decoded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(decoded.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding);

    auto session = BuildBlockedSession(transactionId, revision);
    RequireConflictPreservesBarrierAndLiveBytes(
        session,
        paths,
        journalPath,
        "LIVE_MUTATED_DOMAIN_1720");
}
