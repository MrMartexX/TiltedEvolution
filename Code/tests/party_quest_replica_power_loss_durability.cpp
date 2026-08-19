#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableSnapshot.h>
#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xD0D0D0D0D0D0D0D0ull,
    0x1111222233334444ull};
const PartyQuestPlayerProfileId kPlayer{
    0xA0A0A0A0A0A0A0A0ull,
    0x5555666677778888ull};

struct DurabilitySandbox
{
    std::filesystem::path Root;

    DurabilitySandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_replica_power_loss_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~DurabilitySandbox()
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

PartyQuestReplicaManifest BuildManifest(uint64_t aRevision)
{
    PartyQuestReplicaManifest manifest;
    manifest.CampaignId = kCampaign;
    manifest.PlayerProfileId = kPlayer;
    manifest.SnapshotType = PartyQuestReplicaSnapshotType::RevisionCheckpoint;
    manifest.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    manifest.CampaignWorldRevision = aRevision;
    manifest.Files.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        std::filesystem::path("saves") / "Hero.ess",
        3,
        0x1111 + aRevision});
    REQUIRE_FALSE(PartyQuestReplicaManifestStore::Encode(manifest).empty());
    return manifest;
}

PartyQuestReplicaRestoreJournalState BuildJournalState(
    const std::filesystem::path& acTransactionDirectory,
    PartyQuestReplicaRestoreJournalPhase aPhase,
    uint64_t aRevision)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = 0x9000 + aRevision;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = aRevision;
    state.Phase = aPhase;
    state.TransactionDirectory = acTransactionDirectory;

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath = acTransactionDirectory.parent_path() / "checkpoint" / "Hero.ess";
    operation.ReplicaDestinationPath = acTransactionDirectory.parent_path() / "replica" / "Hero.ess";
    operation.RollbackPath = acTransactionDirectory / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 3;
    operation.ExpectedRestoredDigest = 0xABCD + aRevision;
    operation.DestinationExisted = false;
    state.Operations.push_back(std::move(operation));

    REQUIRE_FALSE(PartyQuestReplicaRestoreJournalPersistence::Encode(state).empty());
    return state;
}

struct ManifestFault
{
    PartyQuestReplicaManifestPersistenceBoundary Boundary;
};

PartyQuestReplicaManifestPersistenceDirective FailManifestAt(
    PartyQuestReplicaManifestPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const ManifestFault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestReplicaManifestPersistenceDirective::FailClosed
        : PartyQuestReplicaManifestPersistenceDirective::Continue;
}

struct JournalFault
{
    PartyQuestReplicaRestoreJournalPersistenceBoundary Boundary;
};

PartyQuestReplicaRestoreJournalPersistenceDirective FailJournalAt(
    PartyQuestReplicaRestoreJournalPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const JournalFault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed
        : PartyQuestReplicaRestoreJournalPersistenceDirective::Continue;
}

std::filesystem::path WithSuffix(std::filesystem::path aPath, const char* apSuffix)
{
    aPath += apSuffix;
    return aPath;
}

PartyQuestCoopSavePaths BuildPaths(const DurabilitySandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildSoloFiles(
    const DurabilitySandbox& acSandbox)
{
    const auto solo = acSandbox.Root / "Solo";
    WriteText(solo / "Hero.ess", "POWER_LOSS_ESS");
    WriteText(solo / "Hero.skse", "POWER_LOSS_SKSE");

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

TEST_CASE(
    "stable storage promotes directory namespace only on a reviewed platform",
    "[quest.party-state.durability][directory]")
{
    DurabilitySandbox sandbox;
    const auto nested = sandbox.Root / "a" / "b" / "c";

#ifdef _WIN32
    REQUIRE_FALSE(PartyQuestStableStorage::HasDocumentedDurableDirectoryTreePrimitive());
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nested) ==
        PartyQuestStableStorageStatus::Unsupported);
    REQUIRE_FALSE(std::filesystem::exists(nested));
#else
    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableDirectoryTreePrimitive());
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nested) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(std::filesystem::is_directory(nested));
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nested) ==
        PartyQuestStableStorageStatus::Success);
#endif

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "replica manifest durable rotation preserves exact recovery authority",
    "[quest.party-state.replica-manifest][durability][fault]")
{
    DurabilitySandbox sandbox;
    const auto directory = sandbox.Root / "manifest";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    REQUIRE_FALSE(ec);
    const auto path = directory / "manifest.bin";

    const auto first = BuildManifest(1001);
    const auto second = BuildManifest(1002);
    REQUIRE(PartyQuestReplicaManifestStore::SavePowerLossDurably(path, first) ==
        PartyQuestReplicaManifestPersistenceStatus::Success);

    ManifestFault rotateFault{
        PartyQuestReplicaManifestPersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestReplicaManifestStore::SavePowerLossDurably(
                path,
                second,
                {FailManifestAt, &rotateFault}) ==
        PartyQuestReplicaManifestPersistenceStatus::IoError);

    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".tmp")));

    const auto recovered = PartyQuestReplicaManifestStore::Load(path);
    REQUIRE(recovered.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(recovered.Manifest.has_value());
    REQUIRE(*recovered.Manifest == second);
    REQUIRE(recovered.UsedTemporary);

    REQUIRE(PartyQuestReplicaManifestStore::SavePowerLossDurably(path, second) ==
        PartyQuestReplicaManifestPersistenceStatus::Success);
    const auto durable = PartyQuestReplicaManifestStore::Load(path);
    REQUIRE(durable.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(durable.Manifest.has_value());
    REQUIRE(*durable.Manifest == second);
    REQUIRE_FALSE(durable.UsedTemporary);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "restore journal durable phases preserve newer staged recovery evidence",
    "[quest.party-state.replica-restore][durability][fault]")
{
    DurabilitySandbox sandbox;
    const auto transaction = sandbox.Root / "restore" / "Transaction_1";
    std::error_code ec;
    std::filesystem::create_directories(transaction / "rollback", ec);
    REQUIRE_FALSE(ec);
    const auto path = transaction / "journal.bin";

    const auto prepared = BuildJournalState(
        transaction,
        PartyQuestReplicaRestoreJournalPhase::Prepared,
        2001);
    auto mutationStarted = prepared;
    mutationStarted.Phase = PartyQuestReplicaRestoreJournalPhase::MutationStarted;
    REQUIRE_FALSE(PartyQuestReplicaRestoreJournalPersistence::Encode(mutationStarted).empty());

    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                path,
                prepared) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    JournalFault rotateFault{
        PartyQuestReplicaRestoreJournalPersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                path,
                mutationStarted,
                {FailJournalAt, &rotateFault}) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::IoError);

    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".tmp")));

    const auto recovered =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(path);
    REQUIRE(recovered.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(*recovered.State == mutationStarted);
    REQUIRE(recovered.UsedTemporary);
    REQUIRE(recovered.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*recovered.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "revision checkpoint promotion orders data before durable manifest authority",
    "[quest.party-state.replica-snapshot][durability][publication]")
{
    DurabilitySandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    const auto soloFiles = BuildSoloFiles(sandbox);
    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, soloFiles);
    REQUIRE(importPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(paths, kCampaign, kPlayer);
    REQUIRE(manager.EnsureImportedReplica(3001, importPlan).IsReady());

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

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        3001,
        {*ess, *skse});
    REQUIRE(checkpointPlan.IsReady());

    const auto checkpoint = manager.EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind::PreRepair,
        3001,
        checkpointPlan);
    REQUIRE(checkpoint.IsReady());

    const auto promotion = PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
        paths,
        kCampaign,
        kPlayer,
        PartyQuestCheckpointKind::PreRepair,
        3001);
#ifdef _WIN32
    REQUIRE(promotion.Status ==
        PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform);
    REQUIRE(promotion.ManifestStatus ==
        PartyQuestReplicaManifestPersistenceStatus::PowerLossDurabilityUnsupported);
#else
    REQUIRE(promotion.IsPromoted());
    REQUIRE(promotion.ManifestStatus ==
        PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(promotion.VerificationStatus ==
        PartyQuestReplicaManifestVerificationStatus::Verified);

    const auto repeated = PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
        paths,
        kCampaign,
        kPlayer,
        PartyQuestCheckpointKind::PreRepair,
        3001);
    REQUIRE(repeated.IsPromoted());
#endif

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
