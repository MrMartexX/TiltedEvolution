#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableSnapshot.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kDurableCapabilityCampaign{
    0xC1C2C3C4C5C6C7C8ull,
    0xD1D2D3D4D5D6D7D8ull};
const PartyQuestPlayerProfileId kDurableCapabilityPlayer{
    0xE1E2E3E4E5E6E7E8ull,
    0xF1F2F3F4F5F6F7F8ull};

struct DurableCapabilitySandbox
{
    std::filesystem::path Root;

    DurableCapabilitySandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_durable_capability_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~DurableCapabilitySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteCapabilityBytes(
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
} // namespace

TEST_CASE(
    "durable checkpoint promotion requires one exact live workspace authority",
    "[quest.party-state.replica-snapshot][durability][workspace-capability]")
{
    constexpr uint64_t revision = 4101;

    DurableCapabilitySandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kDurableCapabilityCampaign,
        kDurableCapabilityPlayer);
    REQUIRE(paths.has_value());

    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteCapabilityBytes(liveSave, "DURABLE_CAPABILITY_SOURCE");
    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        liveSave,
        "Hero.ess");
    REQUIRE(spec.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        revision,
        {*spec});
    REQUIRE(plan.IsReady());

    PartyQuestReplicaSnapshotManager manager(
        *paths,
        kDurableCapabilityCampaign,
        kDurableCapabilityPlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                revision,
                plan).IsReady());

    const PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto missing =
        PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpointAuthorized(
            *paths,
            kDurableCapabilityCampaign,
            kDurableCapabilityPlayer,
            PartyQuestCheckpointKind::PreRepair,
            revision,
            missingCapability);
    REQUIRE(missing.Status ==
        PartyQuestReplicaDurableSnapshotStatus::WorkspaceCapabilityRequired);

    PartyQuestReplicaWorkspaceLease competingLease;
    {
        PartyQuestReplicaWorkspaceLease ownerLease;
        REQUIRE(ownerLease.Acquire(
                    *paths,
                    kDurableCapabilityCampaign,
                    kDurableCapabilityPlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

        auto capability = ownerLease.CreatePublicationCapability(
            *paths,
            kDurableCapabilityCampaign,
            kDurableCapabilityPlayer);
        REQUIRE(capability.IsVerified());
        REQUIRE(capability.Protects(
            *paths,
            kDurableCapabilityCampaign,
            kDurableCapabilityPlayer));

        REQUIRE(competingLease.Acquire(
                    *paths,
                    kDurableCapabilityCampaign,
                    kDurableCapabilityPlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Busy);

        const auto standaloneWhileOwned =
            PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
                *paths,
                kDurableCapabilityCampaign,
                kDurableCapabilityPlayer,
                PartyQuestCheckpointKind::PreRepair,
                revision);
        REQUIRE(standaloneWhileOwned.Status ==
            PartyQuestReplicaDurableSnapshotStatus::WorkspaceBusy);

        ownerLease.Release();
        REQUIRE_FALSE(ownerLease.IsHeld());

        // The capability owns the same native lease state, so releasing the
        // originating wrapper cannot open a publication race.
        REQUIRE(competingLease.Acquire(
                    *paths,
                    kDurableCapabilityCampaign,
                    kDurableCapabilityPlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Busy);
        REQUIRE(capability.Protects(
            *paths,
            kDurableCapabilityCampaign,
            kDurableCapabilityPlayer));

        const auto promoted =
            PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpointAuthorized(
                *paths,
                kDurableCapabilityCampaign,
                kDurableCapabilityPlayer,
                PartyQuestCheckpointKind::PreRepair,
                revision,
                capability);
#ifdef _WIN32
        REQUIRE(promoted.Status ==
            PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform);
        REQUIRE(promoted.ManifestStatus ==
            PartyQuestReplicaManifestPersistenceStatus::PowerLossDurabilityUnsupported);
#else
        REQUIRE(promoted.IsPromoted());
        REQUIRE(promoted.ManifestStatus ==
            PartyQuestReplicaManifestPersistenceStatus::Success);
        REQUIRE(promoted.VerificationStatus ==
            PartyQuestReplicaManifestVerificationStatus::Verified);

        const auto repeated =
            PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpointAuthorized(
                *paths,
                kDurableCapabilityCampaign,
                kDurableCapabilityPlayer,
                PartyQuestCheckpointKind::PreRepair,
                revision,
                capability);
        REQUIRE(repeated.IsPromoted());
#endif

        const PartyQuestCampaignId wrongCampaign{
            kDurableCapabilityCampaign.High ^ 1ull,
            kDurableCapabilityCampaign.Low};
        const auto wrongAuthority =
            PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpointAuthorized(
                *paths,
                wrongCampaign,
                kDurableCapabilityPlayer,
                PartyQuestCheckpointKind::PreRepair,
                revision,
                capability);
        REQUIRE(wrongAuthority.Status ==
            PartyQuestReplicaDurableSnapshotStatus::WorkspaceCapabilityRequired);
    }

    REQUIRE(competingLease.Acquire(
                *paths,
                kDurableCapabilityCampaign,
                kDurableCapabilityPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    REQUIRE(competingLease.IsHeld());

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
