#include <Structs/Skyrim/PartyQuestReplicaRestore.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kRestoreCampaign{0x1111000011110000ull, 0x2222000022220000ull};
const PartyQuestPlayerProfileId kRestorePlayer{0x3333000033330000ull, 0x4444000044440000ull};

struct RestoreSandbox
{
    std::filesystem::path Root;

    RestoreSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_plan_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RestoreSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRestoreFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

struct ReadyRestoreFixture
{
    PartyQuestCoopSavePaths Paths;
    PartyQuestReplicaManifest ImportManifest;
    PartyQuestReplicaManifest CheckpointManifest;
};

ReadyRestoreFixture BuildReadyRestoreFixture(RestoreSandbox& aSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        aSandbox.Root / "CoopCampaigns",
        kRestoreCampaign,
        kRestorePlayer);
    REQUIRE(paths.has_value());

    const auto solo = aSandbox.Root / "Solo";
    WriteRestoreFile(solo / "Hero.ess", "RESTORE_PLAN_ESS");
    WriteRestoreFile(solo / "Hero.skse", "RESTORE_PLAN_SKSE");
    WriteRestoreFile(solo / "Plugin" / "Hero.json", "RESTORE_PLAN_EXTERNAL");

    const auto soloEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave, solo / "Hero.ess", "Hero.ess");
    const auto soloSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave, solo / "Hero.skse", "Hero.skse");
    const auto soloExternal = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar, solo / "Plugin" / "Hero.json", "Plugin/Hero.json");
    REQUIRE(soloEss.has_value());
    REQUIRE(soloSkse.has_value());
    REQUIRE(soloExternal.has_value());

    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(
        *paths, {*soloEss, *soloSkse, *soloExternal});
    REQUIRE(importPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(*paths, kRestoreCampaign, kRestorePlayer);
    REQUIRE(manager.EnsureImportedReplica(600, importPlan).IsReady());

    const auto importLoad = PartyQuestReplicaManifestStore::Load(
        PartyQuestReplicaManifestStore::GetImportManifestPath(*paths));
    REQUIRE(importLoad.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(importLoad.Manifest.has_value());

    const auto replicaEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave, paths->SavesDirectory / "Hero.ess", "Hero.ess");
    const auto replicaSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave, paths->SavesDirectory / "Hero.skse", "Hero.skse");
    const auto replicaExternal = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        paths->SidecarsDirectory / "external" / "Plugin" / "Hero.json",
        "Plugin/Hero.json");
    REQUIRE(replicaEss.has_value());
    REQUIRE(replicaSkse.has_value());
    REQUIRE(replicaExternal.has_value());

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        {*replicaEss, *replicaSkse, *replicaExternal});
    REQUIRE(checkpointPlan.IsReady());
    REQUIRE(manager.EnsureCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                600,
                checkpointPlan).IsReady());

    const auto checkpointLoad = PartyQuestReplicaManifestStore::Load(
        PartyQuestReplicaManifestStore::GetCheckpointManifestPath(
            *paths,
            PartyQuestCheckpointKind::PreRepair));
    REQUIRE(checkpointLoad.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(checkpointLoad.Manifest.has_value());

    return {*paths, *importLoad.Manifest, *checkpointLoad.Manifest};
}
} // namespace

TEST_CASE("Verified checkpoint restore plan targets only current co-op replica files", "[quest.party-state.replica-restore]")
{
    RestoreSandbox sandbox;
    const auto fixture = BuildReadyRestoreFixture(sandbox);

    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        fixture.Paths,
        kRestoreCampaign,
        kRestorePlayer,
        fixture.CheckpointManifest);
    REQUIRE(plan.Status == PartyQuestReplicaRestorePlanStatus::Ready);
    REQUIRE(plan.CampaignWorldRevision == 600);
    REQUIRE(plan.CheckpointKind == PartyQuestCheckpointKind::PreRepair);
    REQUIRE(plan.Operations.size() == 3);

    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        fixture.Paths,
        PartyQuestCheckpointKind::PreRepair);
    for (const auto& operation : plan.Operations)
    {
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(
            checkpointRoot,
            operation.CheckpointSourcePath));
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(
            fixture.Paths.PlayerDirectory,
            operation.ReplicaDestinationPath));
        REQUIRE_FALSE(PartyQuestReplicaFilePlanner::IsContainedBy(
            fixture.Paths.CheckpointsDirectory,
            operation.ReplicaDestinationPath));
        REQUIRE(operation.ReplicaDestinationPath != fixture.Paths.RuntimeApplySidecar);
        REQUIRE(operation.ExpectedDigest != 0);
    }
}

TEST_CASE("Restore planning requires a checkpoint manifest rather than import metadata", "[quest.party-state.replica-restore]")
{
    RestoreSandbox sandbox;
    const auto fixture = BuildReadyRestoreFixture(sandbox);

    REQUIRE(PartyQuestReplicaRestorePlanner::Build(
                fixture.Paths,
                kRestoreCampaign,
                kRestorePlayer,
                fixture.ImportManifest).Status ==
        PartyQuestReplicaRestorePlanStatus::NotCheckpointManifest);
}

TEST_CASE("Restore planning is bound to stable campaign and player identity", "[quest.party-state.replica-restore]")
{
    RestoreSandbox sandbox;
    const auto fixture = BuildReadyRestoreFixture(sandbox);

    REQUIRE(PartyQuestReplicaRestorePlanner::Build(
                fixture.Paths,
                PartyQuestCampaignId{0x99, 0x98},
                kRestorePlayer,
                fixture.CheckpointManifest).Status ==
        PartyQuestReplicaRestorePlanStatus::InvalidIdentity);
    REQUIRE(PartyQuestReplicaRestorePlanner::Build(
                fixture.Paths,
                kRestoreCampaign,
                PartyQuestPlayerProfileId{0x97, 0x96},
                fixture.CheckpointManifest).Status ==
        PartyQuestReplicaRestorePlanStatus::InvalidIdentity);
}

TEST_CASE("Changed checkpoint bytes block restore planning before any overwrite is possible", "[quest.party-state.replica-restore]")
{
    RestoreSandbox sandbox;
    const auto fixture = BuildReadyRestoreFixture(sandbox);

    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        fixture.Paths,
        PartyQuestCheckpointKind::PreRepair);
    WriteRestoreFile(checkpointRoot / "saves" / "Hero.ess", "CORRUPTED_CHECKPOINT");

    REQUIRE(PartyQuestReplicaRestorePlanner::Build(
                fixture.Paths,
                kRestoreCampaign,
                kRestorePlayer,
                fixture.CheckpointManifest).Status ==
        PartyQuestReplicaRestorePlanStatus::CheckpointVerificationFailed);
}
