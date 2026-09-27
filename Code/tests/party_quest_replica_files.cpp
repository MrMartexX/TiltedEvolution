#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <catch2/catch.hpp>

#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{0x1111, 0x2222};
const PartyQuestPlayerProfileId kPlayer{0x3333, 0x4444};

PartyQuestCoopSavePaths BuildPaths()
{
    const auto paths = PartyQuestCoopSaveLayout::Build("CoopCampaigns", kCampaign, kPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

std::vector<PartyQuestReplicaFileSpec> BuildSoloFileSet()
{
    return {
        {PartyQuestReplicaFileKind::SkyrimSave, "SoloSaves/TestCharacter.ess", "TestCharacter.ess", 1024, 0x1111111111111111ull},
        {PartyQuestReplicaFileKind::SkseCosave, "SoloSaves/TestCharacter.skse", "TestCharacter.skse", 128, 0x2222222222222222ull},
        {PartyQuestReplicaFileKind::ExternalSidecar, "SoloSaves/NetImmerse/TestCharacter.json", "NetImmerse/TestCharacter.json", 64, 0x3333333333333333ull}
    };
}
} // namespace

TEST_CASE("Solo save import plan targets only the isolated player replica", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();
    const auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, BuildSoloFileSet());

    REQUIRE(plan.Status == PartyQuestReplicaCopyPlanStatus::Ready);
    REQUIRE(plan.Operations.size() == 3);

    for (const auto& operation : plan.Operations)
    {
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(paths.PlayerDirectory, operation.DestinationPath));
        REQUIRE(operation.SourcePath != operation.DestinationPath);
        REQUIRE(operation.ExpectedDigest != 0);
    }

    REQUIRE(plan.Operations[0].DestinationPath == paths.SavesDirectory / "TestCharacter.ess");
    REQUIRE(plan.Operations[1].DestinationPath == paths.SavesDirectory / "TestCharacter.skse");
    REQUIRE(plan.Operations[2].DestinationPath == paths.SidecarsDirectory / "external" / "NetImmerse" / "TestCharacter.json");
}

TEST_CASE("Replica file planner rejects path traversal absolute destinations and flat save nesting", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();

    SECTION("parent traversal")
    {
        auto files = BuildSoloFileSet();
        files[2].RelativePath = "../escape.json";
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::InvalidRelativePath);
    }

    SECTION("absolute destination")
    {
        auto files = BuildSoloFileSet();
        files[2].RelativePath = std::filesystem::path("/tmp/escape.json");
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::InvalidRelativePath);
    }

    SECTION("main save cannot create nested destination")
    {
        auto files = BuildSoloFileSet();
        files[0].RelativePath = "nested/TestCharacter.ess";
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::InvalidRelativePath);
    }
}

TEST_CASE("Replica import requires one verified main ess and validates cosave extensions", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();

    SECTION("missing main save")
    {
        auto files = BuildSoloFileSet();
        files.erase(files.begin());
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::MissingMainSave);
    }

    SECTION("multiple main saves")
    {
        auto files = BuildSoloFileSet();
        files.push_back({PartyQuestReplicaFileKind::SkyrimSave, "SoloSaves/Other.ess", "Other.ess", 1, 0x44});
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::MultipleMainSaves);
    }

    SECTION("wrong main extension")
    {
        auto files = BuildSoloFileSet();
        files[0].RelativePath = "TestCharacter.txt";
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::InvalidExtension);
    }

    SECTION("wrong skse extension")
    {
        auto files = BuildSoloFileSet();
        files[1].RelativePath = "TestCharacter.bin";
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::InvalidExtension);
    }

    SECTION("missing digest")
    {
        auto files = BuildSoloFileSet();
        files[0].Digest = 0;
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::MissingDigest);
    }
}

TEST_CASE("Replica planner rejects duplicate sources destinations and source overwrite", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();

    SECTION("duplicate source")
    {
        auto files = BuildSoloFileSet();
        files.push_back({PartyQuestReplicaFileKind::ExternalSidecar, files[2].SourcePath, "other.json", 1, 0x55});
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::DuplicateSource);
    }

    SECTION("duplicate destination")
    {
        auto files = BuildSoloFileSet();
        files.push_back({PartyQuestReplicaFileKind::ExternalSidecar, "SoloSaves/Other.json", files[2].RelativePath, 1, 0x66});
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::DuplicateDestination);
    }

    SECTION("source already equals import destination")
    {
        auto files = BuildSoloFileSet();
        files[0].SourcePath = paths.SavesDirectory / "TestCharacter.ess";
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::SourceDestinationCollision);
    }
}

TEST_CASE("Checkpoint plan stays inside explicit checkpoint directory and carries verification data", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();
    std::vector<PartyQuestReplicaFileSpec> replicaFiles = {
        {PartyQuestReplicaFileKind::SkyrimSave, paths.SavesDirectory / "TestCharacter.ess", "TestCharacter.ess", 2048, 0xA1},
        {PartyQuestReplicaFileKind::SkseCosave, paths.SavesDirectory / "TestCharacter.skse", "TestCharacter.skse", 256, 0xA2},
        {PartyQuestReplicaFileKind::ExternalSidecar, paths.SidecarsDirectory / "external" / "plugin.dat", "plugin.dat", 32, 0xA3}
    };

    const auto plan = PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        replicaFiles);
    REQUIRE(plan.IsReady());

    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointDirectory(
        paths, PartyQuestCheckpointKind::PreRepair);
    for (const auto& operation : plan.Operations)
    {
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(checkpointRoot, operation.DestinationPath));
        REQUIRE(PartyQuestReplicaFilePlanner::IsContainedBy(paths.PlayerDirectory, operation.DestinationPath));
    }

    const auto manifest = PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
        kCampaign,
        kPlayer,
        PartyQuestCheckpointKind::PreRepair,
        386,
        plan);
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->CampaignId == kCampaign);
    REQUIRE(manifest->PlayerProfileId == kPlayer);
    REQUIRE(manifest->CampaignWorldRevision == 386);
    REQUIRE(manifest->Kind == PartyQuestCheckpointKind::PreRepair);
    REQUIRE(manifest->Files == plan.Operations);
}

TEST_CASE("Checkpoint manifest fails closed without stable identities revision or ready copy plan", "[quest.party-state.replica-files]")
{
    const auto paths = BuildPaths();
    const auto ready = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, BuildSoloFileSet());
    REQUIRE(ready.IsReady());

    REQUIRE_FALSE(PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
        {}, kPlayer, PartyQuestCheckpointKind::PreJoin, 1, ready).has_value());
    REQUIRE_FALSE(PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
        kCampaign, {}, PartyQuestCheckpointKind::PreJoin, 1, ready).has_value());
    REQUIRE_FALSE(PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
        kCampaign, kPlayer, PartyQuestCheckpointKind::PreJoin, 0, ready).has_value());

    PartyQuestReplicaCopyPlan invalid;
    invalid.Status = PartyQuestReplicaCopyPlanStatus::InvalidSource;
    REQUIRE_FALSE(PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
        kCampaign, kPlayer, PartyQuestCheckpointKind::PreJoin, 1, invalid).has_value());
}

TEST_CASE("Replica planner enforces local file and byte budgets", "[quest.party-state.replica-files][resource-budget]")
{
    const auto paths = BuildPaths();

    SECTION("file count")
    {
        auto files = BuildSoloFileSet();
        for (size_t i = files.size(); i <= PartyQuestReplicaResourcePolicy::MaxFiles; ++i)
        {
            files.push_back({
                PartyQuestReplicaFileKind::ExternalSidecar,
                "SoloSaves/Extra" + std::to_string(i) + ".bin",
                "Extra" + std::to_string(i) + ".bin",
                1,
                static_cast<uint64_t>(i + 1)});
        }
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::ResourceFileCountExceeded);
    }

    SECTION("individual file size")
    {
        auto files = BuildSoloFileSet();
        files[0].Size = PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes + 1;
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::ResourceFileSizeExceeded);
    }

    SECTION("total size")
    {
        auto files = BuildSoloFileSet();
        files[0].Size = PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        files[1].Size = PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        files[2].Size = PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        files.push_back({
            PartyQuestReplicaFileKind::ExternalSidecar,
            "SoloSaves/ExtraA.bin",
            "ExtraA.bin",
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes,
            0x44});
        files.push_back({
            PartyQuestReplicaFileKind::ExternalSidecar,
            "SoloSaves/ExtraB.bin",
            "ExtraB.bin",
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes,
            0x55});
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::ResourceTotalSizeExceeded);
    }
}

TEST_CASE("Replica planner reserves bounded space for crash-safe temporary paths", "[quest.party-state.replica-files][resource-budget][path-budget]")
{
    const std::filesystem::path maximumPath{
        std::string(PartyQuestReplicaResourcePolicy::MaxFilesystemPathBytes, 'a')};
    const std::filesystem::path excessivePath{
        std::string(PartyQuestReplicaResourcePolicy::MaxFilesystemPathBytes + 1, 'a')};
    const std::filesystem::path maximumMutablePath{
        std::string(PartyQuestReplicaResourcePolicy::MaxMutablePathBytes, 'b')};
    const std::filesystem::path excessiveMutablePath{
        std::string(PartyQuestReplicaResourcePolicy::MaxMutablePathBytes + 1, 'b')};

    REQUIRE(PartyQuestReplicaResourcePolicy::IsPathWithinBudget(maximumPath));
    REQUIRE_FALSE(PartyQuestReplicaResourcePolicy::IsPathWithinBudget(excessivePath));
    REQUIRE(PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(maximumMutablePath));
    REQUIRE_FALSE(PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(excessiveMutablePath));

    const auto paths = BuildPaths();
    SECTION("source path")
    {
        auto files = BuildSoloFileSet();
        files[0].SourcePath = excessivePath;
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::ResourcePathLengthExceeded);
    }

    SECTION("destination plus temporary suffix")
    {
        auto files = BuildSoloFileSet();
        files[2].RelativePath = excessiveMutablePath;
        REQUIRE(PartyQuestReplicaFilePlanner::BuildImportPlan(paths, files).Status ==
            PartyQuestReplicaCopyPlanStatus::ResourcePathLengthExceeded);
    }

    SECTION("forged ready plan")
    {
        auto plan = PartyQuestReplicaFilePlanner::BuildImportPlan(paths, BuildSoloFileSet());
        REQUIRE(plan.IsReady());
        plan.Operations[0].DestinationPath = excessiveMutablePath;
        REQUIRE_FALSE(PartyQuestReplicaResourcePolicy::RequiredFreeBytes(plan).has_value());
    }
}
