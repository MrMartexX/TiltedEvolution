#include <Structs/Skyrim/PartyQuestReplicaRestore.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kRevisionCampaign{0xAAAABBBBCCCCDDDDull, 0x1111222233334444ull};
const PartyQuestPlayerProfileId kRevisionPlayer{0x5555666677778888ull, 0x9999AAAABBBBCCCCull};

struct RevisionSandbox
{
    std::filesystem::path Root;

    RevisionSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_revision_checkpoint_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RevisionSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRevisionFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

PartyQuestCoopSavePaths BuildRevisionPaths(const RevisionSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kRevisionCampaign,
        kRevisionPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestReplicaCopyPlan ImportRevisionReplica(
    const RevisionSandbox& acSandbox,
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaSnapshotManager& aManager)
{
    const auto solo = acSandbox.Root / "Solo";
    WriteRevisionFile(solo / "Hero.ess", "REVISION_CHECKPOINT_ESS");
    WriteRevisionFile(solo / "Hero.skse", "REVISION_CHECKPOINT_SKSE");
    WriteRevisionFile(solo / "Plugin" / "Hero.dat", "REVISION_CHECKPOINT_SIDECAR");

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        solo / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        solo / "Hero.skse",
        "Hero.skse");
    const auto sidecar = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        solo / "Plugin" / "Hero.dat",
        "Plugin/Hero.dat");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(
        acPaths,
        {*ess, *skse, *sidecar});
    REQUIRE(plan.IsReady());
    REQUIRE(aManager.EnsureImportedReplica(700, plan).IsReady());
    return plan;
}

std::vector<PartyQuestReplicaFileSpec> InspectCurrentRevisionReplica(
    const PartyQuestCoopSavePaths& acPaths)
{
    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        acPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        acPaths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    const auto sidecar = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        acPaths.SidecarsDirectory / "external" / "Plugin" / "Hero.dat",
        "Plugin/Hero.dat");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());
    return {*ess, *skse, *sidecar};
}
} // namespace

TEST_CASE("Revision checkpoints publish immutable snapshots without overwriting an older revision", "[quest.party-state.revision-checkpoint]")
{
    RevisionSandbox sandbox;
    const auto paths = BuildRevisionPaths(sandbox);
    PartyQuestReplicaSnapshotManager manager(paths, kRevisionCampaign, kRevisionPlayer);
    ImportRevisionReplica(sandbox, paths, manager);

    const auto replicaFiles = InspectCurrentRevisionReplica(paths);
    const auto revision700 = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        700,
        replicaFiles);
    REQUIRE(revision700.IsReady());

    const auto root700 = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        700);
    for (const auto& operation : revision700.Operations)
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(root700, operation.DestinationPath));

    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                700,
                revision700).Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                700).Status == PartyQuestReplicaSnapshotStatus::Ready);

    const auto revision701 = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        701,
        replicaFiles);
    REQUIRE(revision701.IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                701,
                revision701).Status == PartyQuestReplicaSnapshotStatus::Ready);

    const auto root701 = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        701);
    REQUIRE(root700 != root701);
    REQUIRE(std::filesystem::exists(root700 / "saves" / "Hero.ess"));
    REQUIRE(std::filesystem::exists(root701 / "saves" / "Hero.ess"));
    REQUIRE(std::filesystem::exists(
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            700)));
    REQUIRE(std::filesystem::exists(
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            701)));
}

TEST_CASE("Revision checkpoint manifest round-trips and restore planner selects its exact revision root", "[quest.party-state.revision-checkpoint]")
{
    RevisionSandbox sandbox;
    const auto paths = BuildRevisionPaths(sandbox);
    PartyQuestReplicaSnapshotManager manager(paths, kRevisionCampaign, kRevisionPlayer);
    ImportRevisionReplica(sandbox, paths, manager);

    const auto files = InspectCurrentRevisionReplica(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::LastKnownGood,
        710,
        files);
    REQUIRE(plan.IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::LastKnownGood,
                710,
                plan).IsReady());

    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        paths,
        PartyQuestCheckpointKind::LastKnownGood,
        710);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    REQUIRE(loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::RevisionCheckpoint);
    REQUIRE(loaded.Manifest->CampaignWorldRevision == 710);

    const auto encoded = PartyQuestReplicaManifestStore::Encode(*loaded.Manifest);
    const auto decoded = PartyQuestReplicaManifestStore::Decode(encoded);
    REQUIRE(decoded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(decoded.Manifest == loaded.Manifest);

    const auto restore = PartyQuestReplicaRestorePlanner::Build(
        paths,
        kRevisionCampaign,
        kRevisionPlayer,
        *loaded.Manifest);
    REQUIRE(restore.IsReady());
    REQUIRE(restore.CampaignWorldRevision == 710);

    const auto revisionRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::LastKnownGood,
        710);
    for (const auto& operation : restore.Operations)
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(revisionRoot, operation.CheckpointSourcePath));
}

TEST_CASE("Revision checkpoint zero revision fails closed", "[quest.party-state.revision-checkpoint]")
{
    RevisionSandbox sandbox;
    const auto paths = BuildRevisionPaths(sandbox);
    PartyQuestReplicaSnapshotManager manager(paths, kRevisionCampaign, kRevisionPlayer);
    ImportRevisionReplica(sandbox, paths, manager);

    const auto files = InspectCurrentRevisionReplica(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        0,
        files);
    REQUIRE(plan.Status == PartyQuestReplicaCopyPlanStatus::InvalidWorldRevision);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                0,
                plan).Status == PartyQuestReplicaSnapshotStatus::InvalidPlan);
}

TEST_CASE("Corrupting one revision does not invalidate a distinct immutable checkpoint", "[quest.party-state.revision-checkpoint]")
{
    RevisionSandbox sandbox;
    const auto paths = BuildRevisionPaths(sandbox);
    PartyQuestReplicaSnapshotManager manager(paths, kRevisionCampaign, kRevisionPlayer);
    ImportRevisionReplica(sandbox, paths, manager);

    const auto files = InspectCurrentRevisionReplica(paths);
    const auto first = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        720,
        files);
    const auto second = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        721,
        files);
    REQUIRE(first.IsReady());
    REQUIRE(second.IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(PartyQuestCheckpointKind::PreRepair, 720, first).IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(PartyQuestCheckpointKind::PreRepair, 721, second).IsReady());

    const auto root720 = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        720);
    WriteRevisionFile(root720 / "saves" / "Hero.ess", "CORRUPTED_OLD_REVISION");

    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                720).Status == PartyQuestReplicaSnapshotStatus::FileVerificationFailed);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                721).Status == PartyQuestReplicaSnapshotStatus::Ready);
}
