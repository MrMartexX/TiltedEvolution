#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kCleanupCampaign{0xDEADDEADDEADDEADull, 0x1111222233334444ull};
const PartyQuestPlayerProfileId kCleanupPlayer{0xABCDABCDABCDABCDull, 0x9999AAAABBBBCCCCull};

struct CleanupSandbox
{
    std::filesystem::path Root;

    CleanupSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_revision_cleanup_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~CleanupSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteCleanupFile(const std::filesystem::path& acPath, const std::string& acBytes)
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
} // namespace

TEST_CASE("Partial revision recovery revalidates every destination before cleanup", "[quest.party-state.revision-checkpoint][crash][confinement]")
{
    CleanupSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCleanupCampaign,
        kCleanupPlayer);
    REQUIRE(paths.has_value());

    WriteCleanupFile(paths->SavesDirectory / "Hero.ess", "CLEANUP_CONFINEMENT_ESS");
    WriteCleanupFile(paths->SavesDirectory / "Hero.skse", "CLEANUP_CONFINEMENT_SKSE");

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        paths->SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        paths->SavesDirectory / "Hero.skse",
        "Hero.skse");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());

    auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        910,
        {*ess, *skse});
    REQUIRE(plan.IsReady());
    REQUIRE(plan.Operations.size() == 2);

    // Simulate a crash after the first valid final rename.
    std::error_code ec;
    std::filesystem::create_directories(
        plan.Operations[0].DestinationPath.parent_path(),
        ec);
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::copy_file(
        plan.Operations[0].SourcePath,
        plan.Operations[0].DestinationPath,
        std::filesystem::copy_options::none,
        ec));
    REQUIRE_FALSE(ec);

    // Mutate a later operation after planning. ExecuteRevisionCheckpoint sees
    // the first DestinationExists before it can validate this operation. The
    // recovery path therefore must independently reject the escaped path and
    // must not delete either piece of evidence.
    const auto escaped = sandbox.Root / "outside.skse";
    REQUIRE(std::filesystem::copy_file(
        plan.Operations[1].SourcePath,
        escaped,
        std::filesystem::copy_options::none,
        ec));
    REQUIRE_FALSE(ec);
    plan.Operations[1].DestinationPath = escaped;

    const auto validPartialBefore = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        plan.Operations[0].DestinationPath);
    const auto escapedBefore = PartyQuestReplicaFileExecutor::ObserveRegularFile(escaped);
    REQUIRE(validPartialBefore.has_value());
    REQUIRE(escapedBefore.has_value());

    PartyQuestReplicaSnapshotManager manager(
        *paths,
        kCleanupCampaign,
        kCleanupPlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        910,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::FileVerificationFailed);

    const auto validPartialAfter = PartyQuestReplicaFileExecutor::ObserveRegularFile(
        plan.Operations[0].DestinationPath);
    const auto escapedAfter = PartyQuestReplicaFileExecutor::ObserveRegularFile(escaped);
    REQUIRE(validPartialAfter == validPartialBefore);
    REQUIRE(escapedAfter == escapedBefore);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            *paths,
            PartyQuestCheckpointKind::PreRepair,
            910)));
}
