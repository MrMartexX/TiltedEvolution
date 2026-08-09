#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
const PartyQuestCampaignId kExecutorCampaign{0x1020304050607080ull, 0x1122334455667788ull};
const PartyQuestPlayerProfileId kExecutorPlayer{0x8877665544332211ull, 0x8070605040302010ull};

struct RestoreExecutorSandbox
{
    std::filesystem::path Root;

    RestoreExecutorSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_executor_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RestoreExecutorSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteExecutorBytes(const std::filesystem::path& acPath, const std::string& acBytes)
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

std::string ReadExecutorBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestCoopSavePaths BuildExecutorPaths(const RestoreExecutorSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns", kExecutorCampaign, kExecutorPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestReplicaRestorePlan BuildExecutorPlan(
    const PartyQuestCoopSavePaths& acPaths,
    const std::filesystem::path& acCheckpointSource,
    const std::filesystem::path& acDestination,
    uint64_t aWorldRevision)
{
    const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(acCheckpointSource);
    REQUIRE(observation.has_value());

    PartyQuestReplicaRestorePlan plan;
    plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
    plan.CampaignId = kExecutorCampaign;
    plan.PlayerProfileId = kExecutorPlayer;
    plan.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    plan.CampaignWorldRevision = aWorldRevision;
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        acCheckpointSource,
        acDestination,
        observation->Size,
        observation->Digest});
    return plan;
}

PartyQuestReplicaRestoreJournalState PrepareDurableExecutorState(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId)
{
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(acPaths, acPlan, aRestoreId);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    return state;
}
} // namespace

TEST_CASE("Restore executor commits verified checkpoint bytes without touching solo files", "[quest.party-state.restore-executor]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 900;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    const auto soloSave = sandbox.Root / "SoloSaves" / "Hero.ess";

    WriteExecutorBytes(checkpoint, "CANONICAL_900");
    WriteExecutorBytes(destination, "LOCAL_BEFORE_900");
    WriteExecutorBytes(soloSave, "SOLO_MUST_NOT_CHANGE");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    const auto report = PartyQuestReplicaRestoreExecutor::Execute(paths, plan, 1001);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(report.CompletedOperations == 1);
    REQUIRE_FALSE(report.RollbackPerformed);
    REQUIRE(ReadExecutorBytes(destination) == "CANONICAL_900");
    REQUIRE(ReadExecutorBytes(soloSave) == "SOLO_MUST_NOT_CHANGE");

    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(report.JournalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE(loaded.State->Operations.size() == 1);
    REQUIRE(ReadExecutorBytes(loaded.State->Operations[0].RollbackPath) == "LOCAL_BEFORE_900");
}

TEST_CASE("Restore resource policy reserves backups staging and rollback recovery", "[quest.party-state.restore-executor][resource-budget]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 905;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "RESTORED_BYTES");
    WriteExecutorBytes(destination, "ORIGINAL");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 2001);
    REQUIRE(prepared.IsReady());
    const auto& state = *prepared.State;
    REQUIRE(state.Operations.size() == 1);

    const auto required = PartyQuestReplicaRestoreResourcePolicy::RequiredFreeBytes(state);
    REQUIRE(required.has_value());
    REQUIRE(*required ==
        state.Operations[0].ExpectedRestoredSize +
            state.Operations[0].OriginalSize * 2 +
            PartyQuestReplicaResourcePolicy::MinimumFreeSpaceReserveBytes);
    REQUIRE(PartyQuestReplicaRestoreResourcePolicy::HasSufficientDiskSpace(
        state, *required));
    REQUIRE_FALSE(PartyQuestReplicaRestoreResourcePolicy::HasSufficientDiskSpace(
        state, *required - 1));

    auto oversized = state;
    oversized.Operations[0].ExpectedRestoredSize =
        PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes + 1;
    REQUIRE_FALSE(
        PartyQuestReplicaRestoreResourcePolicy::RequiredFreeBytes(oversized).has_value());
}

TEST_CASE("Restore executor can publish a checkpoint file that was absent locally", "[quest.party-state.restore-executor]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 901;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_901");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    const auto report = PartyQuestReplicaRestoreExecutor::Execute(paths, plan, 1002);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(ReadExecutorBytes(destination) == "CANONICAL_901");

    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(report.JournalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE_FALSE(loaded.State->Operations[0].DestinationExisted);
    REQUIRE_FALSE(std::filesystem::exists(loaded.State->Operations[0].RollbackPath));
}

TEST_CASE("Restore executor refuses destination drift before the mutation barrier", "[quest.party-state.restore-executor]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 902;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_902");
    WriteExecutorBytes(destination, "OBSERVED_BEFORE_902");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 1003);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);

    WriteExecutorBytes(destination, "CHANGED_AFTER_PREPARE");
    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::DestinationChanged);
    REQUIRE_FALSE(report.RollbackPerformed);
    REQUIRE(ReadExecutorBytes(destination) == "CHANGED_AFTER_PREPARE");

    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
}

TEST_CASE("MutationStarted recovery rolls a partially restored replica back and terminates the attempt", "[quest.party-state.restore-executor]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 903;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_903");
    WriteExecutorBytes(destination, "ORIGINAL_903");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 1004);
    REQUIRE(state.Operations.size() == 1);
    WriteExecutorBytes(state.Operations[0].RollbackPath, "ORIGINAL_903");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteExecutorBytes(destination, "CANONICAL_903");
    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(report.RollbackPerformed);
    REQUIRE(ReadExecutorBytes(destination) == "ORIGINAL_903");
    REQUIRE_FALSE(std::filesystem::exists(state.TransactionDirectory));
}

TEST_CASE("Rollback cleanup preserves unknown transaction artifacts", "[quest.party-state.restore-executor][confinement]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 906;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_906");
    WriteExecutorBytes(destination, "ORIGINAL_906");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 2002);
    REQUIRE(state.Operations.size() == 1);
    WriteExecutorBytes(state.Operations[0].RollbackPath, "ORIGINAL_906");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteExecutorBytes(destination, "CANONICAL_906");
    const auto unknown = state.TransactionDirectory / "unknown.keep";
    WriteExecutorBytes(unknown, "DO_NOT_DELETE");

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(report.RollbackPerformed);
    REQUIRE(report.CleanupPending);
    REQUIRE(ReadExecutorBytes(destination) == "ORIGINAL_906");
    REQUIRE(ReadExecutorBytes(unknown) == "DO_NOT_DELETE");
    REQUIRE(std::filesystem::exists(journalPath));
}

TEST_CASE("Restored recovery verifies bytes before durably committing", "[quest.party-state.restore-executor]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 904;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_904");
    WriteExecutorBytes(destination, "ORIGINAL_904");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 1005);
    WriteExecutorBytes(state.Operations[0].RollbackPath, "ORIGINAL_904");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteExecutorBytes(destination, "CANONICAL_904");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkRestored(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE_FALSE(report.RollbackPerformed);
    REQUIRE(ReadExecutorBytes(destination) == "CANONICAL_904");

    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
}
