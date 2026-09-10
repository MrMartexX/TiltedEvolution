#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kRetentionCampaign{
    0xABCDEFABCDEFABCDull,
    0x1111222233334444ull};
const PartyQuestPlayerProfileId kRetentionPlayer{
    0x5555666677778888ull,
    0x9999AAAABBBBCCCCull};

struct RetentionSandbox
{
    std::filesystem::path Root;

    RetentionSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_revision_retention_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RetentionSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRetentionFile(
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

PartyQuestCoopSavePaths BuildRetentionPaths(const RetentionSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kRetentionCampaign,
        kRetentionPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestReplicaCopyPlan BuildRetentionPlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto source = acPaths.SavesDirectory / "Hero.ess";
    WriteRetentionFile(source, "REVISION_RETENTION_ESS");
    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        source,
        "Hero.ess");
    REQUIRE(ess.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        acPaths,
        PartyQuestCheckpointKind::PreRepair,
        aWorldRevision,
        {*ess});
    REQUIRE(plan.IsReady());
    return plan;
}
} // namespace

TEST_CASE("Revision checkpoint retained-byte policy accepts the exact boundary and rejects one byte beyond it", "[quest.party-state.revision-checkpoint][resource]")
{
    const uint64_t limit =
        PartyQuestDurableResourcePolicy::MaxRevisionCheckpointRetainedBytesPerKind;
    REQUIRE(limit > 1);

    REQUIRE(PartyQuestDurableResourcePolicy::CanRetainRevisionCheckpointBytes(
        limit,
        0));
    REQUIRE(PartyQuestDurableResourcePolicy::CanRetainRevisionCheckpointBytes(
        limit - 1,
        1));
    REQUIRE_FALSE(PartyQuestDurableResourcePolicy::CanRetainRevisionCheckpointBytes(
        limit,
        1));
    REQUIRE_FALSE(PartyQuestDurableResourcePolicy::CanRetainRevisionCheckpointBytes(
        1,
        limit));
}

TEST_CASE("Revision checkpoint admission rejects a non-canonical revision directory before publication", "[quest.party-state.revision-checkpoint][resource][confinement]")
{
    RetentionSandbox sandbox;
    const auto paths = BuildRetentionPaths(sandbox);
    const uint64_t targetRevision = 1200;
    const auto plan = BuildRetentionPlan(paths, targetRevision);

    const auto kindRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair);
    std::error_code ec;
    std::filesystem::create_directories(
        kindRoot / "Revision_000000000000000a",
        ec);
    REQUIRE_FALSE(ec);

    PartyQuestReplicaSnapshotManager manager(
        paths,
        kRetentionCampaign,
        kRetentionPlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        targetRevision,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::ManifestInvalid);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            targetRevision)));
}

TEST_CASE("Revision checkpoint admission rejects symlinked retained revision content before publication", "[quest.party-state.revision-checkpoint][resource][confinement]")
{
    RetentionSandbox sandbox;
    const auto paths = BuildRetentionPaths(sandbox);
    const uint64_t targetRevision = 1201;
    const auto plan = BuildRetentionPlan(paths, targetRevision);

    const auto retainedRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        1199);
    const auto external = sandbox.Root / "external_retained_bytes";
    WriteRetentionFile(external / "outside.dat", "EXTERNAL_BYTES");

    std::error_code ec;
    std::filesystem::create_directories(retainedRoot, ec);
    REQUIRE_FALSE(ec);
    std::filesystem::create_directory_symlink(
        external,
        retainedRoot / "redirected",
        ec);
#ifdef _WIN32
    if (ec)
    {
        WARN("Directory symlink creation unavailable on this Windows runner: " << ec.message());
        return;
    }
#else
    REQUIRE_FALSE(ec);
#endif

    PartyQuestReplicaSnapshotManager manager(
        paths,
        kRetentionCampaign,
        kRetentionPlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        targetRevision,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::ManifestInvalid);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            targetRevision)));
    REQUIRE(std::filesystem::exists(external / "outside.dat"));
}

TEST_CASE("Revision checkpoint admission preserves legacy kind-root artifacts while bounding revision retention", "[quest.party-state.revision-checkpoint][resource]")
{
    RetentionSandbox sandbox;
    const auto paths = BuildRetentionPaths(sandbox);
    const uint64_t targetRevision = 1202;
    const auto plan = BuildRetentionPlan(paths, targetRevision);

    const auto kindRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair);
    std::error_code ec;
    std::filesystem::create_directories(kindRoot / "saves", ec);
    REQUIRE_FALSE(ec);
    std::filesystem::create_directories(kindRoot / "sidecars", ec);
    REQUIRE_FALSE(ec);
    WriteRetentionFile(kindRoot / "manifest.bin.bak", "LEGACY_ARCHIVE_EVIDENCE");

    PartyQuestReplicaSnapshotManager manager(
        paths,
        kRetentionCampaign,
        kRetentionPlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        targetRevision,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                targetRevision).IsReady());
    REQUIRE(std::filesystem::exists(kindRoot / "manifest.bin.bak"));
}
