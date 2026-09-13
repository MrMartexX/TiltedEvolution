#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kExecutorCampaign{0x1234567890ABCDEFull, 0x0FEDCBA098765432ull};
const PartyQuestPlayerProfileId kExecutorPlayer{0xAAAABBBBCCCCDDDDull, 0x1111222233334444ull};

struct TempReplicaSandbox
{
    std::filesystem::path Root;

    TempReplicaSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_replica_executor_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~TempReplicaSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteFile(const std::filesystem::path& acPath, const std::string& acBytes)
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

PartyQuestCoopSavePaths BuildExecutorPaths(const TempReplicaSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kExecutorCampaign,
        kExecutorPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildInspectedSoloFiles(
    const TempReplicaSandbox& acSandbox)
{
    const auto soloRoot = acSandbox.Root / "SoloSaves";
    WriteFile(soloRoot / "Hero.ess", "TESV_SAVE_PAYLOAD_1234567890");
    WriteFile(soloRoot / "Hero.skse", "SKSE_COSAVE_PAYLOAD");
    WriteFile(soloRoot / "NetImmerse" / "Hero.json", "{\"raceMenu\":true}\n");

    std::vector<PartyQuestReplicaFileSpec> files;
    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        soloRoot / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        soloRoot / "Hero.skse",
        "Hero.skse");
    const auto sidecar = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        soloRoot / "NetImmerse" / "Hero.json",
        "NetImmerse/Hero.json");

    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());
    REQUIRE(ess->Digest != 0);
    REQUIRE(skse->Digest != 0);
    REQUIRE(sidecar->Digest != 0);

    files.push_back(*ess);
    files.push_back(*skse);
    files.push_back(*sidecar);
    return files;
}

struct ExpiringExecutionClock
{
    size_t Calls{};
};

uint64_t ExpireAfterAdmission(void* apContext) noexcept
{
    auto& clock = *static_cast<ExpiringExecutionClock*>(apContext);
    ++clock.Calls;
    return clock.Calls == 1
        ? 1
        : PartyQuestReplicaResourcePolicy::MaxExecutionNanoseconds + 1;
}
} // namespace

TEST_CASE("Verified import copies solo save ecosystem only into isolated co-op replica", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    const auto beforeEss = PartyQuestReplicaFileExecutor::ObserveRegularFile(files[0].SourcePath);
    REQUIRE(beforeEss.has_value());

    const auto executed = PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan);
    REQUIRE(executed.Status == PartyQuestReplicaExecutionStatus::Success);
    REQUIRE(executed.CompletedOperations == plan.Operations.size());
    REQUIRE(PartyQuestReplicaFileExecutor::VerifyImport(paths, plan).IsSuccess());

    REQUIRE(std::filesystem::exists(paths.SavesDirectory / "Hero.ess"));
    REQUIRE(std::filesystem::exists(paths.SavesDirectory / "Hero.skse"));
    REQUIRE(std::filesystem::exists(paths.SidecarsDirectory / "external" / "NetImmerse" / "Hero.json"));

    const auto afterEss = PartyQuestReplicaFileExecutor::ObserveRegularFile(files[0].SourcePath);
    REQUIRE(afterEss.has_value());
    REQUIRE(*afterEss == *beforeEss);

    // Create-only semantics make an accidental second import fail rather than
    // overwrite an existing co-op replica.
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan).Status ==
        PartyQuestReplicaExecutionStatus::DestinationExists);
}

TEST_CASE("Import fails closed when a source changes after planning", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    auto files = BuildInspectedSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    WriteFile(files[0].SourcePath, "TESV_SAVE_PAYLOAD_CHANGED_AFTER_PLAN");

    const auto executed = PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan);
    REQUIRE(executed.Status == PartyQuestReplicaExecutionStatus::SourceChanged);
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "Hero.ess"));
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "Hero.skse"));
}

TEST_CASE("Import executor independently rejects a source from inside the co-op player tree", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    WriteFile(paths.MetadataDirectory / "NotSolo.ess", "SHOULD_NOT_BE_IMPORTED_AS_SOLO");

    const auto inspected = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        paths.MetadataDirectory / "NotSolo.ess",
        "NotSolo.ess");
    REQUIRE(inspected.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, {*inspected});
    REQUIRE(plan.IsReady());
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan).Status ==
        PartyQuestReplicaExecutionStatus::ImportSourceInsidePlayerRoot);
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "NotSolo.ess"));
}

TEST_CASE("Checkpoint executor copies only current replica files into the selected checkpoint", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto soloFiles = BuildInspectedSoloFiles(sandbox);
    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, soloFiles);
    REQUIRE(importPlan.IsReady());
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, importPlan).IsSuccess());

    std::vector<PartyQuestReplicaFileSpec> replicaFiles;
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
        paths.SidecarsDirectory / "external" / "NetImmerse" / "Hero.json",
        "NetImmerse/Hero.json");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());
    replicaFiles = {*ess, *skse, *sidecar};

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        replicaFiles);
    REQUIRE(checkpointPlan.IsReady());

    const auto executed = PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        checkpointPlan);
    REQUIRE(executed.IsSuccess());
    REQUIRE(PartyQuestReplicaFileExecutor::VerifyCheckpoint(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        checkpointPlan).IsSuccess());

    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair);
    REQUIRE(std::filesystem::exists(checkpointRoot / "saves" / "Hero.ess"));
    REQUIRE(std::filesystem::exists(checkpointRoot / "saves" / "Hero.skse"));
    REQUIRE(std::filesystem::exists(checkpointRoot / "sidecars" / "external" / "NetImmerse" / "Hero.json"));
}

TEST_CASE("Checkpoint executor rejects outside and recursive checkpoint sources", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);

    SECTION("outside player root")
    {
        const auto outside = sandbox.Root / "Elsewhere" / "Hero.ess";
        WriteFile(outside, "OUTSIDE_REPLICA");
        const auto inspected = PartyQuestReplicaFileExecutor::InspectSource(
            PartyQuestReplicaFileKind::SkyrimSave,
            outside,
            "Hero.ess");
        REQUIRE(inspected.has_value());

        const auto plan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            {*inspected});
        REQUIRE(plan.IsReady());
        REQUIRE(PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
                    paths,
                    PartyQuestCheckpointKind::PreRepair,
                    plan).Status ==
            PartyQuestReplicaExecutionStatus::CheckpointSourceOutsidePlayerRoot);
    }

    SECTION("checkpoint of checkpoint")
    {
        const auto oldCheckpointRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
            paths,
            PartyQuestCheckpointKind::SessionStart);
        const auto recursiveSource = oldCheckpointRoot / "saves" / "Hero.ess";
        WriteFile(recursiveSource, "OLD_CHECKPOINT");
        const auto inspected = PartyQuestReplicaFileExecutor::InspectSource(
            PartyQuestReplicaFileKind::SkyrimSave,
            recursiveSource,
            "Hero.ess");
        REQUIRE(inspected.has_value());

        const auto plan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            {*inspected});
        REQUIRE(plan.IsReady());
        REQUIRE(PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
                    paths,
                    PartyQuestCheckpointKind::PreRepair,
                    plan).Status ==
            PartyQuestReplicaExecutionStatus::CheckpointSourceInsideCheckpointTree);
    }
}

TEST_CASE("Published verification detects later co-op replica corruption", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan).IsSuccess());

    WriteFile(paths.SavesDirectory / "Hero.ess", "CORRUPTED_AFTER_IMPORT");
    REQUIRE(PartyQuestReplicaFileExecutor::VerifyImport(paths, plan).Status ==
        PartyQuestReplicaExecutionStatus::VerificationFailed);
}

TEST_CASE("Executor does not trust a manually mutated ready plan destination", "[quest.party-state.replica-executor]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    plan.Operations[0].DestinationPath = sandbox.Root / "escape.ess";
    REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan).Status ==
        PartyQuestReplicaExecutionStatus::InvalidDestination);
    REQUIRE_FALSE(std::filesystem::exists(sandbox.Root / "escape.ess"));
}

TEST_CASE("Replica executor rejects an over-budget forged destination before filesystem work", "[quest.party-state.replica-executor][resource-budget][path-budget]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    plan.Operations[0].DestinationPath =
        paths.SavesDirectory /
        std::string(PartyQuestReplicaResourcePolicy::MaxMutablePathBytes, 'x');
    const auto executed = PartyQuestReplicaFileExecutor::ExecuteImport(paths, plan);
    REQUIRE(executed.Status == PartyQuestReplicaExecutionStatus::ResourceLimitExceeded);
    REQUIRE(executed.CompletedOperations == 0);

    const std::filesystem::path excessiveSource{
        std::string(PartyQuestReplicaResourcePolicy::MaxFilesystemPathBytes + 1, 's')};
    REQUIRE_FALSE(PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        excessiveSource,
        "Hero.ess").has_value());
}

TEST_CASE("Executor revalidates resource limits and disk reserve before staging", "[quest.party-state.replica-executor][resource-budget]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    SECTION("ready plan cannot bypass tighter local limits")
    {
        auto oversizedPlan = plan;
        while (oversizedPlan.Operations.size() <= PartyQuestReplicaResourcePolicy::MaxFiles)
            oversizedPlan.Operations.push_back(oversizedPlan.Operations.back());
        REQUIRE(PartyQuestReplicaFileExecutor::ExecuteImport(paths, oversizedPlan).Status ==
            PartyQuestReplicaExecutionStatus::ResourceLimitExceeded);
    }

    SECTION("disk reserve includes checkpoint restore staging and safety margin")
    {
        const auto required = PartyQuestReplicaResourcePolicy::RequiredFreeBytes(plan);
        REQUIRE(required.has_value());
        REQUIRE(*required > PartyQuestReplicaResourcePolicy::MinimumFreeSpaceReserveBytes);
        REQUIRE_FALSE(PartyQuestReplicaResourcePolicy::HasSufficientDiskSpace(plan, *required - 1));
        REQUIRE(PartyQuestReplicaResourcePolicy::HasSufficientDiskSpace(plan, *required));
    }
}

TEST_CASE("Replica execution deadline expires before new filesystem publication", "[quest.party-state.replica-executor][resource-budget][timeout]")
{
    TempReplicaSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    const auto files = BuildInspectedSoloFiles(sandbox);
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files);
    REQUIRE(plan.IsReady());

    ExpiringExecutionClock clock;
    const auto result = PartyQuestReplicaFileExecutor::ExecuteImport(
        paths,
        plan,
        {ExpireAfterAdmission, &clock});

    REQUIRE(result.Status ==
        PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded);
    REQUIRE(result.CompletedOperations == 0);
    REQUIRE(clock.Calls >= 2);
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "Hero.ess"));
    REQUIRE_FALSE(std::filesystem::exists(paths.SavesDirectory / "Hero.skse"));
}
