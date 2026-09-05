#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kSnapshotCampaign{0xCAFECAFECAFECAFEull, 0x1111222233334444ull};
const PartyQuestPlayerProfileId kSnapshotPlayer{0xABCDABCDABCDABCDull, 0x5555666677778888ull};

struct SnapshotSandbox
{
    std::filesystem::path Root;

    SnapshotSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_snapshot_manager_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~SnapshotSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteSnapshotFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

PartyQuestCoopSavePaths BuildSnapshotPaths(const SnapshotSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kSnapshotCampaign,
        kSnapshotPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildSnapshotSoloFiles(const SnapshotSandbox& acSandbox)
{
    const auto solo = acSandbox.Root / "Solo";
    WriteSnapshotFile(solo / "Hero.ess", "SNAPSHOT_MANAGER_ESS");
    WriteSnapshotFile(solo / "Hero.skse", "SNAPSHOT_MANAGER_SKSE");

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        solo / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        solo / "Hero.skse",
        "Hero.skse");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    return {*ess, *skse};
}
} // namespace

TEST_CASE("Snapshot manager publishes import only after durable manifest verification", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto files = BuildSnapshotSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    PartyQuestReplicaSnapshotManager manager(paths, kSnapshotCampaign, kSnapshotPlayer);
    const auto created = manager.EnsureImportedReplica(500, plan);
    REQUIRE(created.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(created.CopyStatus == PartyQuestReplicaExecutionStatus::Success);
    REQUIRE(created.ManifestStatus == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(created.VerificationStatus == PartyQuestReplicaManifestVerificationStatus::Verified);
    REQUIRE_FALSE(created.AdoptedVerifiedFiles);

    REQUIRE(std::filesystem::exists(PartyQuestReplicaManifestStore::GetImportManifestPath(paths)));
    REQUIRE(manager.ValidateImportedReplica().Status == PartyQuestReplicaSnapshotStatus::Ready);

    const auto duplicate = manager.EnsureImportedReplica(500, plan);
    REQUIRE(duplicate.Status == PartyQuestReplicaSnapshotStatus::AlreadyReady);
}

TEST_CASE("Snapshot manager adopts exact orphaned files after crash before manifest write", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto files = BuildSnapshotSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    // Models: files were fully published, then the process died before the
    // durable completion manifest could be created.
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan).IsSuccess());
    REQUIRE_FALSE(std::filesystem::exists(PartyQuestReplicaManifestStore::GetImportManifestPath(paths)));

    PartyQuestReplicaSnapshotManager manager(paths, kSnapshotCampaign, kSnapshotPlayer);
    const auto recovered = manager.EnsureImportedReplica(510, plan);
    REQUIRE(recovered.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(recovered.AdoptedVerifiedFiles);
    REQUIRE(recovered.ManifestStatus == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(manager.ValidateImportedReplica().IsReady());
}

TEST_CASE("Snapshot manager never adopts a partial orphaned multi-file import", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto files = BuildSnapshotSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    std::error_code ec;
    std::filesystem::create_directories(paths.SavesDirectory, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::copy_file(
        files[0].SourcePath,
        paths.SavesDirectory / "Hero.ess",
        std::filesystem::copy_options::none,
        ec));
    REQUIRE_FALSE(ec);

    PartyQuestReplicaSnapshotManager manager(paths, kSnapshotCampaign, kSnapshotPlayer);
    const auto result = manager.EnsureImportedReplica(520, plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::FileVerificationFailed);
    REQUIRE_FALSE(result.AdoptedVerifiedFiles);
    REQUIRE_FALSE(std::filesystem::exists(PartyQuestReplicaManifestStore::GetImportManifestPath(paths)));
}

TEST_CASE("Snapshot manager makes checkpoint bytes durable behind their own manifest", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto soloFiles = BuildSnapshotSoloFiles(sandbox);
    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, soloFiles);
    REQUIRE(importPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(paths, kSnapshotCampaign, kSnapshotPlayer);
    REQUIRE(manager.EnsureImportedReplica(530, importPlan).IsReady());

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        paths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        paths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        {*ess, *skse});
    REQUIRE(checkpointPlan.IsReady());

    const auto checkpoint = manager.EnsureCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        530,
        checkpointPlan);
    REQUIRE(checkpoint.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(manager.ValidateCheckpoint(PartyQuestCheckpointKind::PreRepair).Status ==
        PartyQuestReplicaSnapshotStatus::Ready);

    const auto duplicate = manager.EnsureCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        530,
        checkpointPlan);
    REQUIRE(duplicate.Status == PartyQuestReplicaSnapshotStatus::AlreadyReady);
}

TEST_CASE("Snapshot manager surfaces older manifest backup as explicit recovery", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto files = BuildSnapshotSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    PartyQuestReplicaSnapshotManager manager(paths, kSnapshotCampaign, kSnapshotPlayer);
    REQUIRE(manager.EnsureImportedReplica(540, plan).IsReady());

    const auto manifestPath = PartyQuestReplicaManifestStore::GetImportManifestPath(paths);
    auto backupPath = manifestPath;
    backupPath += ".bak";
    std::error_code ec;
    std::filesystem::rename(manifestPath, backupPath, ec);
    REQUIRE_FALSE(ec);

    const auto validation = manager.ValidateImportedReplica();
    REQUIRE(validation.Status == PartyQuestReplicaSnapshotStatus::ManifestRecoveryRequired);
    REQUIRE(validation.ManifestStatus == PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired);
}

TEST_CASE("Snapshot manager rejects wrong stable identity before touching files", "[quest.party-state.replica-snapshot]")
{
    SnapshotSandbox sandbox;
    const auto paths = BuildSnapshotPaths(sandbox);
    const auto files = BuildSnapshotSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    PartyQuestReplicaSnapshotManager invalidCampaign(paths, {}, kSnapshotPlayer);
    REQUIRE(invalidCampaign.EnsureImportedReplica(550, plan).Status ==
        PartyQuestReplicaSnapshotStatus::InvalidIdentity);
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "Hero.ess"));
}
