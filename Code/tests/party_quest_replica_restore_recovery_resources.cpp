#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
const PartyQuestCampaignId kRecoveryResourceCampaign{
    0xA102030405060708ull,
    0xB112233445566778ull};
const PartyQuestPlayerProfileId kRecoveryResourcePlayer{
    0xC877665544332211ull,
    0xD070605040302010ull};

struct RecoveryResourceSandbox
{
    std::filesystem::path Root;

    RecoveryResourceSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_recovery_resource_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RecoveryResourceSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRecoveryResourceBytes(
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

std::string ReadRecoveryResourceBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestCoopSavePaths BuildRecoveryResourcePaths(
    const RecoveryResourceSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kRecoveryResourceCampaign,
        kRecoveryResourcePlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestReplicaRestorePlan BuildRecoveryResourcePlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision,
    const std::filesystem::path& acCheckpoint,
    const std::filesystem::path& acDestination)
{
    const auto observation =
        PartyQuestReplicaFileExecutor::ObserveRegularFile(acCheckpoint);
    REQUIRE(observation.has_value());

    PartyQuestReplicaRestorePlan plan;
    plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
    plan.CampaignId = kRecoveryResourceCampaign;
    plan.PlayerProfileId = kRecoveryResourcePlayer;
    plan.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    plan.CampaignWorldRevision = aWorldRevision;
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        acCheckpoint,
        acDestination,
        observation->Size,
        observation->Digest});
    return plan;
}

PartyQuestReplicaRestoreJournalState PrepareDurableRecoveryResourceState(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId)
{
    const auto prepared =
        PartyQuestReplicaRestoreJournal::Prepare(acPaths, acPlan, aRestoreId);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                state) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    return state;
}

struct DiskSpaceProbe
{
    uint64_t AvailableBytes{};
    size_t Reads{};
};

bool ReportDiskSpace(
    const std::filesystem::path& acPath,
    uint64_t& aAvailableBytes,
    void* apContext) noexcept
{
    auto& probe = *static_cast<DiskSpaceProbe*>(apContext);
    ++probe.Reads;
    if (acPath.empty())
        return false;
    aAvailableBytes = probe.AvailableBytes;
    return true;
}

PartyQuestReplicaRestoreExecutionHooks LowDiskHooks(DiskSpaceProbe& aProbe) noexcept
{
    PartyQuestReplicaRestoreExecutionHooks hooks;
    hooks.Context = &aProbe;
    hooks.QueryAvailableBytes = ReportDiskSpace;
    return hooks;
}
} // namespace

TEST_CASE(
    "Fresh restore fails disk preflight before publishing its durable journal",
    "[quest.party-state.restore-executor][resource-budget][disk-full]")
{
    RecoveryResourceSandbox sandbox;
    const auto paths = BuildRecoveryResourcePaths(sandbox);
    constexpr uint64_t kWorldRevision = 930;
    constexpr uint64_t kRestoreId = 4101;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRecoveryResourceBytes(checkpoint, "CANONICAL_930");
    WriteRecoveryResourceBytes(destination, "ORIGINAL_930");

    const auto plan = BuildRecoveryResourcePlan(
        paths,
        kWorldRevision,
        checkpoint,
        destination);
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, kRestoreId);
    REQUIRE(prepared.IsReady());

    DiskSpaceProbe probe{};
    const auto report = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        kRestoreId,
        LowDiskHooks(probe));

    REQUIRE(report.Status ==
        PartyQuestReplicaRestoreExecutionStatus::InsufficientDiskSpace);
    REQUIRE(probe.Reads == 1);
    REQUIRE(ReadRecoveryResourceBytes(destination) == "ORIGINAL_930");
    REQUIRE_FALSE(std::filesystem::exists(prepared.State->TransactionDirectory));
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestReplicaRestoreJournal::GetJournalPath(*prepared.State)));
}

TEST_CASE(
    "Pre-mutation restore recovery rechecks disk space after restart",
    "[quest.party-state.restore-executor][resource-budget][disk-full][recovery]")
{
    SECTION("Prepared")
    {
        RecoveryResourceSandbox sandbox;
        const auto paths = BuildRecoveryResourcePaths(sandbox);
        constexpr uint64_t kWorldRevision = 931;
        const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            kWorldRevision) / "saves" / "Hero.ess";
        const auto destination = paths.SavesDirectory / "Hero.ess";
        WriteRecoveryResourceBytes(checkpoint, "CANONICAL_931");
        WriteRecoveryResourceBytes(destination, "ORIGINAL_931");
        const auto plan = BuildRecoveryResourcePlan(
            paths,
            kWorldRevision,
            checkpoint,
            destination);
        const auto state = PrepareDurableRecoveryResourceState(paths, plan, 4102);
        REQUIRE(state.Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);

        DiskSpaceProbe probe{};
        const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
        const auto report = PartyQuestReplicaRestoreExecutor::Recover(
            paths,
            kRecoveryResourceCampaign,
            kRecoveryResourcePlayer,
            journalPath,
            LowDiskHooks(probe));

        REQUIRE(report.Status ==
            PartyQuestReplicaRestoreExecutionStatus::InsufficientDiskSpace);
        REQUIRE(probe.Reads == 1);
        REQUIRE(ReadRecoveryResourceBytes(destination) == "ORIGINAL_931");
        REQUIRE_FALSE(std::filesystem::exists(state.Operations[0].RollbackPath));
        const auto retained = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
        REQUIRE(retained.Status ==
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
        REQUIRE(retained.State.has_value());
        REQUIRE(retained.State->Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
    }

    SECTION("BackupsReady")
    {
        RecoveryResourceSandbox sandbox;
        const auto paths = BuildRecoveryResourcePaths(sandbox);
        constexpr uint64_t kWorldRevision = 932;
        const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            kWorldRevision) / "saves" / "Hero.ess";
        const auto destination = paths.SavesDirectory / "Hero.ess";
        WriteRecoveryResourceBytes(checkpoint, "CANONICAL_932");
        WriteRecoveryResourceBytes(destination, "ORIGINAL_932");
        const auto plan = BuildRecoveryResourcePlan(
            paths,
            kWorldRevision,
            checkpoint,
            destination);
        auto state = PrepareDurableRecoveryResourceState(paths, plan, 4103);
        WriteRecoveryResourceBytes(state.Operations[0].RollbackPath, "ORIGINAL_932");
        REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
            PartyQuestReplicaRestoreJournalStatus::Ready);
        const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
        REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                    journalPath,
                    state) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

        DiskSpaceProbe probe{};
        const auto report = PartyQuestReplicaRestoreExecutor::Recover(
            paths,
            kRecoveryResourceCampaign,
            kRecoveryResourcePlayer,
            journalPath,
            LowDiskHooks(probe));

        REQUIRE(report.Status ==
            PartyQuestReplicaRestoreExecutionStatus::InsufficientDiskSpace);
        REQUIRE(probe.Reads == 1);
        REQUIRE(ReadRecoveryResourceBytes(destination) == "ORIGINAL_932");
        REQUIRE(ReadRecoveryResourceBytes(state.Operations[0].RollbackPath) ==
            "ORIGINAL_932");
        const auto retained = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
        REQUIRE(retained.Status ==
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
        REQUIRE(retained.State.has_value());
        REQUIRE(retained.State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    }
}

TEST_CASE(
    "Post-mutation rollback is never blocked by the disk admission gate",
    "[quest.party-state.restore-executor][resource-budget][disk-full][recovery]")
{
    RecoveryResourceSandbox sandbox;
    const auto paths = BuildRecoveryResourcePaths(sandbox);
    constexpr uint64_t kWorldRevision = 933;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRecoveryResourceBytes(checkpoint, "CANONICAL_933");
    WriteRecoveryResourceBytes(destination, "ORIGINAL_933");
    const auto plan = BuildRecoveryResourcePlan(
        paths,
        kWorldRevision,
        checkpoint,
        destination);
    auto state = PrepareDurableRecoveryResourceState(paths, plan, 4104);
    WriteRecoveryResourceBytes(state.Operations[0].RollbackPath, "ORIGINAL_933");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                state) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                state) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    WriteRecoveryResourceBytes(destination, "CANONICAL_933");

    DiskSpaceProbe probe{};
    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kRecoveryResourceCampaign,
        kRecoveryResourcePlayer,
        journalPath,
        LowDiskHooks(probe));

    REQUIRE(report.Status ==
        PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(report.RollbackPerformed);
    REQUIRE(probe.Reads == 0);
    REQUIRE(ReadRecoveryResourceBytes(destination) == "ORIGINAL_933");
}
