#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kLeaseCampaign{
    0x5151515151515151ull,
    0x6161616161616161ull};
const PartyQuestPlayerProfileId kLeasePlayer{
    0x7171717171717171ull,
    0x8181818181818181ull};
const PartyQuestPlayerProfileId kOtherPlayer{
    0x9191919191919191ull,
    0xA1A1A1A1A1A1A1A1ull};

struct LeaseSandbox
{
    std::filesystem::path Root;

    LeaseSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_revision_workspace_lease_" +
             std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~LeaseSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildLeasePaths(
    const LeaseSandbox& acSandbox,
    const PartyQuestPlayerProfileId& acPlayer = kLeasePlayer)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kLeaseCampaign,
        acPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

void WriteLeaseFile(
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

PartyQuestReplicaCopyPlan BuildLeasePlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto source = acPaths.SavesDirectory / "Hero.ess";
    WriteLeaseFile(source, "WORKSPACE_LEASE_REVISION_ESS");
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

TEST_CASE("Standalone revision publication fails closed while exact workspace is leased", "[quest.party-state.revision-checkpoint][workspace-lease]")
{
    LeaseSandbox sandbox;
    const auto paths = BuildLeasePaths(sandbox);
    const uint64_t revision = 1300;
    const auto plan = BuildLeasePlan(paths, revision);

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(paths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

    PartyQuestReplicaSnapshotManager manager(
        paths,
        kLeaseCampaign,
        kLeasePlayer);
    const auto result = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        revision,
        plan);
    REQUIRE(result.Status == PartyQuestReplicaSnapshotStatus::WorkspaceBusy);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            revision)));
}

TEST_CASE("Pinned publication capability reuses one held lease without weakening exclusivity", "[quest.party-state.revision-checkpoint][workspace-lease]")
{
    LeaseSandbox sandbox;
    const auto paths = BuildLeasePaths(sandbox);
    const uint64_t revision = 1301;
    const auto plan = BuildLeasePlan(paths, revision);

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(paths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    auto capability = ownerLease.CreatePublicationCapability(
        paths,
        kLeaseCampaign,
        kLeasePlayer);
    REQUIRE(capability.IsVerified());
    REQUIRE(capability.Protects(paths, kLeaseCampaign, kLeasePlayer));

    PartyQuestReplicaWorkspaceLease competing;
    REQUIRE(competing.Acquire(paths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Busy);

    PartyQuestReplicaSnapshotManager manager(
        paths,
        kLeaseCampaign,
        kLeasePlayer);
    const auto published = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        revision,
        plan,
        capability);
    REQUIRE(published.Status == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(manager.ValidateRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                revision).IsReady());

    ownerLease.Release();
    REQUIRE_FALSE(ownerLease.IsHeld());
    REQUIRE(competing.Acquire(paths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Busy);

    capability = {};
    REQUIRE(competing.Acquire(paths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
}

TEST_CASE("Publication capability cannot authorize another player workspace", "[quest.party-state.revision-checkpoint][workspace-lease][confinement]")
{
    LeaseSandbox sandbox;
    const auto ownerPaths = BuildLeasePaths(sandbox);
    const auto otherPaths = BuildLeasePaths(sandbox, kOtherPlayer);
    const uint64_t revision = 1302;
    const auto otherPlan = BuildLeasePlan(otherPaths, revision);

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(ownerPaths, kLeaseCampaign, kLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = ownerLease.CreatePublicationCapability(
        ownerPaths,
        kLeaseCampaign,
        kLeasePlayer);
    REQUIRE(capability.IsVerified());
    REQUIRE_FALSE(capability.Protects(
        otherPaths,
        kLeaseCampaign,
        kOtherPlayer));

    PartyQuestReplicaSnapshotManager otherManager(
        otherPaths,
        kLeaseCampaign,
        kOtherPlayer);
    const auto result = otherManager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        revision,
        otherPlan,
        capability);
    REQUIRE(result.Status ==
        PartyQuestReplicaSnapshotStatus::WorkspaceLeaseFailure);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            otherPaths,
            PartyQuestCheckpointKind::PreRepair,
            revision)));
}
