#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kManifestCampaign{0x1011121314151617ull, 0x18191A1B1C1D1E1Full};
const PartyQuestCampaignId kManifestOtherCampaign{0x9999, 0x8888};
const PartyQuestPlayerProfileId kManifestPlayer{0x2122232425262728ull, 0x292A2B2C2D2E2F30ull};
const PartyQuestPlayerProfileId kManifestOtherPlayer{0x7777, 0x6666};

struct ManifestSandbox
{
    std::filesystem::path Root;

    ManifestSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_replica_manifest_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~ManifestSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteManifestTestFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

bool WriteManifestBytes(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::error_code ec;
    if (!acPath.parent_path().empty())
        std::filesystem::create_directories(acPath.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(acBytes.data()), static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    return file.good();
}

PartyQuestCoopSavePaths BuildManifestPaths(const ManifestSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kManifestCampaign,
        kManifestPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildManifestSoloFiles(const ManifestSandbox& acSandbox)
{
    const auto solo = acSandbox.Root / "Solo";
    WriteManifestTestFile(solo / "Hero.ess", "MANIFEST_ESS_BYTES");
    WriteManifestTestFile(solo / "Hero.skse", "MANIFEST_SKSE_BYTES");
    WriteManifestTestFile(solo / "Plugin" / "Hero.dat", "MANIFEST_SIDECAR_BYTES");

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
    return {*ess, *skse, *sidecar};
}

PartyQuestReplicaManifest BuildReadyImportManifest(
    ManifestSandbox& aSandbox,
    PartyQuestCoopSavePaths& aPaths,
    PartyQuestReplicaCopyPlan& aPlan,
    uint64_t aWorldRevision)
{
    aPaths = BuildManifestPaths(aSandbox);
    const auto files = BuildManifestSoloFiles(aSandbox);
    aPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(aPaths, files);
    REQUIRE(aPlan.IsReady());
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(aPaths, aPlan).IsSuccess());

    const auto manifest = PartyQuestReplicaManifestStore::BuildImportManifest(
        aPaths,
        kManifestCampaign,
        kManifestPlayer,
        aWorldRevision,
        aPlan);
    REQUIRE(manifest.has_value());
    return *manifest;
}
} // namespace

TEST_CASE("Replica completion manifest is deterministic durable and verifies imported bytes", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan plan;
    const auto manifest = BuildReadyImportManifest(sandbox, paths, plan, 401);

    REQUIRE(manifest.SnapshotType == PartyQuestReplicaSnapshotType::ImportedReplica);
    REQUIRE(manifest.CampaignWorldRevision == 401);
    REQUIRE(manifest.Files.size() == 3);
    REQUIRE(manifest.Files[0].RelativePath.is_relative());

    const auto first = PartyQuestReplicaManifestStore::Encode(manifest);
    const auto second = PartyQuestReplicaManifestStore::Encode(manifest);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);

    const auto decoded = PartyQuestReplicaManifestStore::Decode(first);
    REQUIRE(decoded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(decoded.Manifest.has_value());
    REQUIRE(*decoded.Manifest == manifest);

    const auto path = PartyQuestReplicaManifestStore::GetImportManifestPath(paths);
    REQUIRE(PartyQuestReplicaManifestStore::SaveAtomically(path, manifest) ==
        PartyQuestReplicaManifestPersistenceStatus::Success);
    const auto loaded = PartyQuestReplicaManifestStore::Load(path);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    REQUIRE(*loaded.Manifest == manifest);

    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths,
                kManifestCampaign,
                kManifestPlayer,
                manifest) ==
        PartyQuestReplicaManifestVerificationStatus::Verified);
}

TEST_CASE("Replica manifest verification binds campaign player and final file bytes", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan plan;
    const auto manifest = BuildReadyImportManifest(sandbox, paths, plan, 402);

    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths,
                kManifestOtherCampaign,
                kManifestPlayer,
                manifest) ==
        PartyQuestReplicaManifestVerificationStatus::InvalidIdentity);
    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths,
                kManifestCampaign,
                kManifestOtherPlayer,
                manifest) ==
        PartyQuestReplicaManifestVerificationStatus::InvalidIdentity);

    WriteManifestTestFile(paths.SavesDirectory / "Hero.ess", "TAMPERED_AFTER_MANIFEST");
    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths,
                kManifestCampaign,
                kManifestPlayer,
                manifest) ==
        PartyQuestReplicaManifestVerificationStatus::MissingOrChangedFile);
}

TEST_CASE("Checkpoint completion manifest uses checkpoint-relative paths and verifies snapshot", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan importPlan;
    const auto importManifest = BuildReadyImportManifest(sandbox, paths, importPlan, 410);
    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths, kManifestCampaign, kManifestPlayer, importManifest) ==
        PartyQuestReplicaManifestVerificationStatus::Verified);

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        paths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        paths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    const auto sidecar = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        paths.SidecarsDirectory / "external" / "Plugin" / "Hero.dat",
        "Plugin/Hero.dat");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::LastKnownGood,
        {*ess, *skse, *sidecar});
    REQUIRE(checkpointPlan.IsReady());
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
                paths,
                PartyQuestCheckpointKind::LastKnownGood,
                checkpointPlan).IsSuccess());

    const auto checkpointManifest = PartyQuestReplicaManifestStore::BuildCheckpointManifest(
        paths,
        kManifestCampaign,
        kManifestPlayer,
        PartyQuestCheckpointKind::LastKnownGood,
        410,
        checkpointPlan);
    REQUIRE(checkpointManifest.has_value());
    REQUIRE(checkpointManifest->SnapshotType == PartyQuestReplicaSnapshotType::Checkpoint);
    REQUIRE(checkpointManifest->CheckpointKind == PartyQuestCheckpointKind::LastKnownGood);
    REQUIRE(checkpointManifest->Files[0].RelativePath.is_relative());

    REQUIRE(PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                paths,
                kManifestCampaign,
                kManifestPlayer,
                *checkpointManifest) ==
        PartyQuestReplicaManifestVerificationStatus::Verified);
}

TEST_CASE("Replica manifest checksum truncation and path escape are rejected", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan plan;
    auto manifest = BuildReadyImportManifest(sandbox, paths, plan, 420);
    const auto encoded = PartyQuestReplicaManifestStore::Encode(manifest);
    REQUIRE(encoded.size() > 32);

    auto corrupted = encoded;
    corrupted[24] ^= 0x5A;
    REQUIRE(PartyQuestReplicaManifestStore::Decode(corrupted).Status ==
        PartyQuestReplicaManifestPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestReplicaManifestStore::Decode(truncated).Status ==
        PartyQuestReplicaManifestPersistenceStatus::Truncated);

    manifest.Files[0].RelativePath = "../escape.ess";
    REQUIRE(PartyQuestReplicaManifestStore::Encode(manifest).empty());
}

TEST_CASE("Interrupted manifest replacement prefers valid temporary state over older backup", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan plan;
    const auto firstManifest = BuildReadyImportManifest(sandbox, paths, plan, 430);
    auto secondManifest = firstManifest;
    secondManifest.CampaignWorldRevision = 431;

    const auto manifestPath = PartyQuestReplicaManifestStore::GetImportManifestPath(paths);
    REQUIRE(PartyQuestReplicaManifestStore::SaveAtomically(manifestPath, firstManifest) ==
        PartyQuestReplicaManifestPersistenceStatus::Success);

    auto temporaryPath = manifestPath;
    temporaryPath += ".tmp";
    REQUIRE(WriteManifestBytes(temporaryPath, PartyQuestReplicaManifestStore::Encode(secondManifest)));

    auto backupPath = manifestPath;
    backupPath += ".bak";
    std::error_code ec;
    std::filesystem::rename(manifestPath, backupPath, ec);
    REQUIRE_FALSE(ec);

    const auto recovered = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(recovered.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(recovered.UsedTemporary);
    REQUIRE_FALSE(recovered.UsedBackup);
    REQUIRE(recovered.Manifest.has_value());
    REQUIRE(recovered.Manifest->CampaignWorldRevision == 431);
}

TEST_CASE("Older manifest backup is explicit recovery input rather than current truth", "[quest.party-state.replica-manifest]")
{
    ManifestSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    PartyQuestReplicaCopyPlan plan;
    const auto manifest = BuildReadyImportManifest(sandbox, paths, plan, 440);
    const auto manifestPath = PartyQuestReplicaManifestStore::GetImportManifestPath(paths);
    REQUIRE(PartyQuestReplicaManifestStore::SaveAtomically(manifestPath, manifest) ==
        PartyQuestReplicaManifestPersistenceStatus::Success);

    auto backupPath = manifestPath;
    backupPath += ".bak";
    std::error_code ec;
    std::filesystem::rename(manifestPath, backupPath, ec);
    REQUIRE_FALSE(ec);

    const auto recovered = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(recovered.Status == PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired);
    REQUIRE(recovered.UsedBackup);
    REQUIRE(recovered.Manifest.has_value());
    REQUIRE(*recovered.Manifest == manifest);
}
