#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCrashCampaign{0xC0DEC0DEC0DEC0DEull, 0x1111222233334444ull};
const PartyQuestPlayerProfileId kCrashPlayer{0xABCDABCDABCDABCDull, 0x5555666677778888ull};

struct CrashSandbox
{
    std::filesystem::path Root;

    CrashSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_revision_crash_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~CrashSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteCrashFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

PartyQuestCoopSavePaths BuildCrashPaths(const CrashSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCrashCampaign,
        kCrashPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildReplicaFiles(
    const PartyQuestCoopSavePaths& acPaths)
{
    WriteCrashFile(acPaths.SavesDirectory / "Hero.ess", "CRASH_RECOVERY_ESS");
    WriteCrashFile(acPaths.SavesDirectory / "Hero.skse", "CRASH_RECOVERY_SKSE");
    WriteCrashFile(
        acPaths.SidecarsDirectory / "external" / "Plugin" / "Hero.dat",
        "CRASH_RECOVERY_SIDECAR");

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

void PublishExactOperation(const PartyQuestReplicaCopyOperation& acOperation)
{
    std::error_code ec;
    std::filesystem::create_directories(acOperation.DestinationPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::copy_file(
        acOperation.SourcePath,
        acOperation.DestinationPath,
        std::filesystem::copy_options::none,
        ec));
    REQUIRE_FALSE(ec);
}
} // namespace

TEST_CASE("Revision checkpoint recovers an exact partial publication after crash between final renames", "[quest.party-state.revision-checkpoint][crash]")
{
    CrashSandbox sandbox;
    const auto paths = BuildCrashPaths(sandbox);
    const auto files = BuildReplicaFiles(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        900,
        files);
    REQUIRE(plan.IsReady());
    REQUIRE(plan.Operations.size() >= 3);

    // Fault injection: model a hard crash after one final rename. No manifest
    // exists, one expected destination is exact, and the rest are absent.
    PublishExactOperation(plan.Operations[0]);
    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        900);
    REQUIRE_FALSE(std::filesystem::exists(manifestPath));

    PartyQuestReplicaSnapshotManager manager(paths, kCrashCampaign, kCrashPlayer);
    const auto recovered = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        900,
        plan);
    REQUIRE(recovered.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE_FALSE(recovered.AdoptedVerifiedFiles);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                900).Status == PartyQuestReplicaSnapshotStatus::Ready);

    for (const auto& operation : plan.Operations)
    {
        const auto observed = PartyQuestReplicaFileExecutor::ObserveRegularFile(
            operation.DestinationPath);
        REQUIRE(observed.has_value());
        REQUIRE(observed->Size == operation.ExpectedSize);
        REQUIRE(observed->Digest == operation.ExpectedDigest);
    }
}

TEST_CASE("Revision checkpoint recovery remains restart-safe after a later partial publication", "[quest.party-state.revision-checkpoint][crash]")
{
    CrashSandbox sandbox;
    const auto paths = BuildCrashPaths(sandbox);
    const auto files = BuildReplicaFiles(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        901,
        files);
    REQUIRE(plan.IsReady());
    REQUIRE(plan.Operations.size() >= 3);

    // Fault injection at a later boundary: two of three final renames succeeded
    // before process death. Recovery must classify the exact subset, remove it,
    // and safely replay the immutable copy plan.
    PublishExactOperation(plan.Operations[0]);
    PublishExactOperation(plan.Operations[1]);

    PartyQuestReplicaSnapshotManager manager(paths, kCrashCampaign, kCrashPlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                901,
                plan).Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                901).IsReady());
}

TEST_CASE("Revision checkpoint never cleans or adopts mismatching partial publication evidence", "[quest.party-state.revision-checkpoint][crash]")
{
    CrashSandbox sandbox;
    const auto paths = BuildCrashPaths(sandbox);
    const auto files = BuildReplicaFiles(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        902,
        files);
    REQUIRE(plan.IsReady());

    const auto& operation = plan.Operations[0];
    WriteCrashFile(operation.DestinationPath, "CONFLICTING_PARTIAL_CHECKPOINT_BYTES");
    const auto conflictingBefore = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        operation.DestinationPath);
    REQUIRE(conflictingBefore.has_value());
    REQUIRE(conflictingBefore->Digest != operation.ExpectedDigest);

    PartyQuestReplicaSnapshotManager manager(paths, kCrashCampaign, kCrashPlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        902,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::FileVerificationFailed);
    REQUIRE_FALSE(result.AdoptedVerifiedFiles);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            902)));

    const auto conflictingAfter = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        operation.DestinationPath);
    REQUIRE(conflictingAfter.has_value());
    REQUIRE(*conflictingAfter == *conflictingBefore);
}

TEST_CASE("Revision checkpoint adopts a complete exact orphan after crash before manifest commit", "[quest.party-state.revision-checkpoint][crash]")
{
    CrashSandbox sandbox;
    const auto paths = BuildCrashPaths(sandbox);
    const auto files = BuildReplicaFiles(paths);
    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        903,
        files);
    REQUIRE(plan.IsReady());

    // Fault injection: every final rename completed but the process died before
    // the manifest commit point. Existing behavior should adopt exact bytes.
    for (const auto& operation : plan.Operations)
        PublishExactOperation(operation);

    PartyQuestReplicaSnapshotManager manager(paths, kCrashCampaign, kCrashPlayer);
    const auto recovered = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        903,
        plan);
    REQUIRE(recovered.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(recovered.AdoptedVerifiedFiles);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                903).IsReady());
}
