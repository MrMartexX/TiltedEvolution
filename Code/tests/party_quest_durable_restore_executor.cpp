#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableRestoreExecutor.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableRestorePreparation.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x9192939495969798ull,
    0xA1A2A3A4A5A6A7A8ull};
const PartyQuestPlayerProfileId kPlayer{
    0xB1B2B3B4B5B6B7B8ull,
    0xC1C2C3C4C5C6C7C8ull};
constexpr uint64_t kRevision = 6101;

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_durable_restore_exec_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~Sandbox()
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

std::string ReadText(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestReplicaRestorePlan BuildPlan(
    const Sandbox& acSandbox,
    PartyQuestCoopSavePaths& aPaths,
    bool aRemoveLiveSkseBeforePreparation = false)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());
    aPaths = *paths;

    const auto solo = acSandbox.Root / "Solo";
    WriteText(solo / "Hero.ess", "CHECKPOINT_ESS");
    WriteText(solo / "Hero.skse", "CHECKPOINT_SKSE");

    const auto soloEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        solo / "Hero.ess",
        "Hero.ess");
    const auto soloSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        solo / "Hero.skse",
        "Hero.skse");
    REQUIRE(soloEss.has_value());
    REQUIRE(soloSkse.has_value());

    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(
        aPaths,
        {*soloEss, *soloSkse});
    REQUIRE(importPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(aPaths, kCampaign, kPlayer);
    REQUIRE(manager.EnsureImportedReplica(kRevision, importPlan).IsReady());

    const auto liveEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        aPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto liveSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        aPaths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    REQUIRE(liveEss.has_value());
    REQUIRE(liveSkse.has_value());

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        aPaths,
        PartyQuestCheckpointKind::PreRepair,
        kRevision,
        {*liveEss, *liveSkse});
    REQUIRE(checkpointPlan.IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                kRevision,
                checkpointPlan).IsReady());

    WriteText(aPaths.SavesDirectory / "Hero.ess", "LIVE_DIVERGED_ESS");
    WriteText(aPaths.SavesDirectory / "Hero.skse", "LIVE_DIVERGED_SKSE");
    if (aRemoveLiveSkseBeforePreparation)
    {
        std::error_code ec;
        REQUIRE(std::filesystem::remove(aPaths.SavesDirectory / "Hero.skse", ec));
        REQUIRE_FALSE(ec);
    }

    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            aPaths,
            PartyQuestCheckpointKind::PreRepair,
            kRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());

    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        aPaths,
        kCampaign,
        kPlayer,
        *loaded.Manifest);
    REQUIRE(plan.IsReady());
    return plan;
}

struct PreparedFixture
{
    PartyQuestCoopSavePaths Paths;
    PartyQuestReplicaRestorePlan Plan;
    PartyQuestReplicaDurableRestorePreparationReport Prepared;
};

PreparedFixture PrepareFixture(
    const Sandbox& acSandbox,
    uint64_t aRestoreId,
    bool aRemoveLiveSkseBeforePreparation = false)
{
    PreparedFixture fixture;
    fixture.Plan = BuildPlan(
        acSandbox,
        fixture.Paths,
        aRemoveLiveSkseBeforePreparation);
    fixture.Prepared = PartyQuestReplicaDurableRestorePreparation::Prepare(
        fixture.Paths,
        fixture.Plan,
        aRestoreId);
    REQUIRE(fixture.Prepared.IsBackupsReady());
    REQUIRE(fixture.Prepared.State.has_value());
    return fixture;
}

PartyQuestReplicaRestoreJournalState LoadStrongJournal(
    const std::filesystem::path& acJournalPath)
{
    const auto loaded =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            acJournalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    return *loaded.State;
}

struct FaultCut
{
    PartyQuestReplicaDurableRestoreBoundary Boundary;
    std::optional<size_t> Operation;
    bool Fired{};
};

PartyQuestReplicaDurableRestoreDirective FailOnceAt(
    PartyQuestReplicaDurableRestoreBoundary aBoundary,
    size_t aOperation,
    void* apContext) noexcept
{
    auto& fault = *static_cast<FaultCut*>(apContext);
    if (!fault.Fired &&
        fault.Boundary == aBoundary &&
        (!fault.Operation || *fault.Operation == aOperation))
    {
        fault.Fired = true;
        return PartyQuestReplicaDurableRestoreDirective::FailClosed;
    }
    return PartyQuestReplicaDurableRestoreDirective::Continue;
}

void RequireGlobalMutationGateClosed()
{
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
} // namespace

TEST_CASE(
    "durable restore continuation commits exact checkpoint and retains compact tombstone",
    "[quest.party-state.replica-restore][durability][commit]")
{
    Sandbox sandbox;
    constexpr uint64_t restoreId = 0x61010001;
    auto fixture = PrepareFixture(sandbox, restoreId);
    const auto rollbackPaths = [&]() {
        std::vector<std::filesystem::path> paths;
        for (const auto& operation : fixture.Prepared.State->Operations)
            paths.push_back(operation.RollbackPath);
        return paths;
    }();

    const auto result = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(result.Status == PartyQuestReplicaDurableRestoreStatus::Success);
    REQUIRE(result.IsCheckpointRestored());
    REQUIRE(result.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE(result.CompletedOperations == 2);
    REQUIRE_FALSE(result.RequiresRecovery);
    REQUIRE_FALSE(result.CleanupPending);

    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "CHECKPOINT_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "CHECKPOINT_SKSE");

    const auto committed = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(committed.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE(std::filesystem::exists(committed.TransactionDirectory));
    REQUIRE(std::filesystem::exists(fixture.Prepared.JournalPath));
    for (const auto& rollback : rollbackPaths)
        REQUIRE_FALSE(std::filesystem::exists(rollback));

    auto temporaryJournal = fixture.Prepared.JournalPath;
    temporaryJournal += ".tmp";
    auto backupJournal = fixture.Prepared.JournalPath;
    backupJournal += ".bak";
    REQUIRE_FALSE(std::filesystem::exists(temporaryJournal));
    REQUIRE_FALSE(std::filesystem::exists(backupJournal));

    const auto duplicate = PartyQuestReplicaDurableRestorePreparation::Prepare(
        fixture.Paths,
        fixture.Plan,
        restoreId);
    REQUIRE(duplicate.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::RestoreIdConflict);

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::AlreadyCommitted);
    REQUIRE_FALSE(recovered.CleanupPending);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable MutationStarted cut reaches compact RolledBack tombstone idempotently",
    "[quest.party-state.replica-restore][durability][rollback][fault]")
{
    Sandbox sandbox;
    constexpr uint64_t restoreId = 0x61010002;
    auto fixture = PrepareFixture(sandbox, restoreId);

    FaultCut fault{PartyQuestReplicaDurableRestoreBoundary::MutationStartedDurable};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(cut.Phase == PartyQuestReplicaRestoreJournalPhase::MutationStarted);
    REQUIRE(cut.RequiresRecovery);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.CleanupPending);
    REQUIRE_FALSE(recovered.RequiresRecovery);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto terminal = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(terminal.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(terminal) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RolledBackClean);
    for (const auto& operation : terminal.Operations)
        REQUIRE_FALSE(std::filesystem::exists(operation.RollbackPath));

    const auto repeated = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(repeated.Status == PartyQuestReplicaDurableRestoreStatus::AlreadyRolledBack);
    REQUIRE(repeated.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE_FALSE(repeated.CleanupPending);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto duplicate = PartyQuestReplicaDurableRestorePreparation::Prepare(
        fixture.Paths,
        fixture.Plan,
        restoreId);
    REQUIRE(duplicate.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::RestoreIdConflict);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable partial destination publication rolls back all originals",
    "[quest.party-state.replica-restore][durability][rollback][partial]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010003);

    FaultCut fault{
        PartyQuestReplicaDurableRestoreBoundary::RestoredFilePublished,
        size_t{0}};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(cut.Phase == PartyQuestReplicaRestoreJournalPhase::MutationStarted);
    REQUIRE(cut.CompletedOperations == 1);
    REQUIRE(cut.RequiresRecovery);

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.CleanupPending);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable Restored cut verifies then commits during recovery",
    "[quest.party-state.replica-restore][durability][recover-commit]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010004);

    FaultCut fault{PartyQuestReplicaDurableRestoreBoundary::RestoredDurable};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(cut.Phase == PartyQuestReplicaRestoreJournalPhase::Restored);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "CHECKPOINT_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "CHECKPOINT_SKSE");

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::RecoveredCommit);
    REQUIRE(recovered.IsCheckpointRestored());
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE_FALSE(recovered.CleanupPending);

    const auto committed = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(committed.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    for (const auto& operation : committed.Operations)
        REQUIRE_FALSE(std::filesystem::exists(operation.RollbackPath));

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "corrupted Restored state rolls back and terminates RolledBack",
    "[quest.party-state.replica-restore][durability][recover-rollback][restored]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010009);

    FaultCut fault{PartyQuestReplicaDurableRestoreBoundary::RestoredDurable};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(cut.Phase == PartyQuestReplicaRestoreJournalPhase::Restored);

    WriteText(fixture.Paths.SavesDirectory / "Hero.ess", "POST_RESTORE_CORRUPTION");

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.CleanupPending);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto terminal = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(terminal.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable RolledBack cut resumes only compaction and keeps restore id tombstone",
    "[quest.party-state.replica-restore][durability][rollback-terminal][fault]")
{
    Sandbox sandbox;
    constexpr uint64_t restoreId = 0x6101000A;
    auto fixture = PrepareFixture(sandbox, restoreId);

    FaultCut mutationCut{PartyQuestReplicaDurableRestoreBoundary::MutationStartedDurable};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &mutationCut});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);

    FaultCut terminalCut{PartyQuestReplicaDurableRestoreBoundary::RolledBackDurable};
    const auto firstRecovery = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &terminalCut});
    REQUIRE(firstRecovery.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(firstRecovery.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(firstRecovery.RollbackPerformed);
    REQUIRE(firstRecovery.CleanupPending);
    REQUIRE(firstRecovery.RequiresRecovery);

    const auto terminalBeforeCleanup = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(terminalBeforeCleanup.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    size_t existingBackups = 0;
    for (const auto& operation : terminalBeforeCleanup.Operations)
    {
        if (operation.DestinationExisted && std::filesystem::exists(operation.RollbackPath))
            ++existingBackups;
    }
    REQUIRE(existingBackups > 0);

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::AlreadyRolledBack);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE_FALSE(recovered.CleanupPending);
    REQUIRE_FALSE(recovered.RequiresRecovery);

    const auto terminal = LoadStrongJournal(fixture.Prepared.JournalPath);
    REQUIRE(terminal.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(std::filesystem::exists(terminal.TransactionDirectory));
    REQUIRE(std::filesystem::exists(fixture.Prepared.JournalPath));
    for (const auto& operation : terminal.Operations)
        REQUIRE_FALSE(std::filesystem::exists(operation.RollbackPath));

    const auto duplicate = PartyQuestReplicaDurableRestorePreparation::Prepare(
        fixture.Paths,
        fixture.Plan,
        restoreId);
    REQUIRE(duplicate.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::RestoreIdConflict);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable Committed cut resumes only compaction and never rolls back",
    "[quest.party-state.replica-restore][durability][commit][fault]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010005);

    FaultCut fault{PartyQuestReplicaDurableRestoreBoundary::CommittedDurable};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(cut.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "CHECKPOINT_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "CHECKPOINT_SKSE");

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::AlreadyCommitted);
    REQUIRE(recovered.IsCheckpointRestored());
    REQUIRE_FALSE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.CleanupPending);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable continuation rejects stale live destination before mutation barrier",
    "[quest.party-state.replica-restore][durability][stale]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010006);
    WriteText(fixture.Paths.SavesDirectory / "Hero.ess", "EXTERNAL_CHANGE");

    const auto result = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(result.Status == PartyQuestReplicaDurableRestoreStatus::DestinationChanged);
    REQUIRE(result.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE_FALSE(result.RequiresRecovery);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "EXTERNAL_CHANGE");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::ResumeBeforeMutation);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "durable continuation rejects changed promoted checkpoint before mutation barrier",
    "[quest.party-state.replica-restore][durability][checkpoint]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010007);
    REQUIRE(fixture.Prepared.State.has_value());
    WriteText(
        fixture.Prepared.State->Operations.front().CheckpointSourcePath,
        "CORRUPTED_CHECKPOINT");

    const auto result = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(result.Status ==
        PartyQuestReplicaDurableRestoreStatus::CheckpointDurabilityUnavailable);
    REQUIRE(result.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE_FALSE(result.RequiresRecovery);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "rollback durably removes destination that was absent before mutation",
    "[quest.party-state.replica-restore][durability][rollback][missing]")
{
    Sandbox sandbox;
    auto fixture = PrepareFixture(sandbox, 0x61010008, true);
    REQUIRE(fixture.Prepared.State.has_value());

    size_t missingIndex = fixture.Prepared.State->Operations.size();
    for (size_t i = 0; i < fixture.Prepared.State->Operations.size(); ++i)
    {
        if (!fixture.Prepared.State->Operations[i].DestinationExisted)
        {
            missingIndex = i;
            break;
        }
    }
    REQUIRE(missingIndex < fixture.Prepared.State->Operations.size());

    FaultCut fault{
        PartyQuestReplicaDurableRestoreBoundary::RestoredFilePublished,
        missingIndex};
    const auto cut = PartyQuestReplicaDurableRestoreExecutor::Continue(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath,
        {FailOnceAt, &fault});
    REQUIRE(cut.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(std::filesystem::exists(fixture.Paths.SavesDirectory / "Hero.skse"));

    const auto recovered = PartyQuestReplicaDurableRestoreExecutor::Recover(
        fixture.Paths,
        kCampaign,
        kPlayer,
        fixture.Prepared.JournalPath);
    REQUIRE(recovered.Status == PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.CleanupPending);
    REQUIRE(ReadText(fixture.Paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE_FALSE(std::filesystem::exists(fixture.Paths.SavesDirectory / "Hero.skse"));

    RequireGlobalMutationGateClosed();
}

TEST_CASE(
    "empty directory durable removal is supported by stable storage",
    "[quest.party-state.durability][directory-remove]")
{
    Sandbox sandbox;
    const auto directory = sandbox.Root / "empty";
    std::error_code ec;
    REQUIRE(std::filesystem::create_directory(directory, ec));
    REQUIRE_FALSE(ec);

    const auto status = PartyQuestStableStorage::RemoveEmptyDirectoryDurably(directory);
    REQUIRE(status == PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(directory));

    RequireGlobalMutationGateClosed();
}
