#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

const std::filesystem::path& GetTPTestsExecutablePath() noexcept;

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

PartyQuestCoopSavePaths BuildExecutorPaths(const std::filesystem::path& acRoot)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acRoot / "CoopCampaigns", kExecutorCampaign, kExecutorPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

bool SetExecutorEnvironment(const char* apName, const std::string& acValue)
{
#ifdef _WIN32
    return _putenv_s(apName, acValue.c_str()) == 0;
#else
    return setenv(apName, acValue.c_str(), 1) == 0;
#endif
}

void ClearExecutorEnvironment(const char* apName)
{
#ifdef _WIN32
    _putenv_s(apName, "");
#else
    unsetenv(apName);
#endif
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

PartyQuestReplicaRestorePlan BuildMultiFileExecutorPlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        acPaths, PartyQuestCheckpointKind::PreRepair, aWorldRevision);
    const auto saveSource = checkpointRoot / "saves" / "Hero.ess";
    const auto cosaveSource = checkpointRoot / "saves" / "Hero.skse";
    const auto sidecarSource = checkpointRoot / "sidecars" / "external" /
        "RequiredProvider" / "State.bin";
    const auto saveObservation =
        PartyQuestReplicaFileExecutor::ObserveRegularFile(saveSource);
    const auto cosaveObservation =
        PartyQuestReplicaFileExecutor::ObserveRegularFile(cosaveSource);
    const auto sidecarObservation =
        PartyQuestReplicaFileExecutor::ObserveRegularFile(sidecarSource);
    REQUIRE(saveObservation.has_value());
    REQUIRE(cosaveObservation.has_value());
    REQUIRE(sidecarObservation.has_value());

    PartyQuestReplicaRestorePlan plan;
    plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
    plan.CampaignId = kExecutorCampaign;
    plan.PlayerProfileId = kExecutorPlayer;
    plan.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    plan.CampaignWorldRevision = aWorldRevision;
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        saveSource,
        acPaths.SavesDirectory / "Hero.ess",
        saveObservation->Size,
        saveObservation->Digest});
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkseCosave,
        cosaveSource,
        acPaths.SavesDirectory / "Hero.skse",
        cosaveObservation->Size,
        cosaveObservation->Digest});
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::ExternalSidecar,
        sidecarSource,
        acPaths.SidecarsDirectory / "external" / "RequiredProvider" / "State.bin",
        sidecarObservation->Size,
        sidecarObservation->Digest});
    return plan;
}

void WriteMultiFileExecutorFixture(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        acPaths, PartyQuestCheckpointKind::PreRepair, aWorldRevision);
    WriteExecutorBytes(
        checkpointRoot / "saves" / "Hero.ess",
        "CANONICAL_SAVE_" + std::to_string(aWorldRevision));
    WriteExecutorBytes(
        checkpointRoot / "saves" / "Hero.skse",
        "CANONICAL_COSAVE_" + std::to_string(aWorldRevision));
    WriteExecutorBytes(
        checkpointRoot / "sidecars" / "external" / "RequiredProvider" / "State.bin",
        "CANONICAL_SIDECAR_" + std::to_string(aWorldRevision));
    WriteExecutorBytes(
        acPaths.SavesDirectory / "Hero.ess",
        "ORIGINAL_SAVE_" + std::to_string(aWorldRevision));
    WriteExecutorBytes(
        acPaths.SavesDirectory / "Hero.skse",
        "ORIGINAL_COSAVE_" + std::to_string(aWorldRevision));
}

struct RestoreBoundaryFailureScript
{
    std::vector<std::pair<PartyQuestReplicaRestoreExecutionBoundary, size_t>> Steps;
    size_t Next{};
};

PartyQuestReplicaRestoreExecutionDirective FailAtScriptedBoundary(
    PartyQuestReplicaRestoreExecutionBoundary aBoundary,
    size_t aOperation,
    void* apContext) noexcept
{
    auto& script = *static_cast<RestoreBoundaryFailureScript*>(apContext);
    if (script.Next < script.Steps.size() &&
        script.Steps[script.Next] == std::pair{aBoundary, aOperation})
    {
        ++script.Next;
        return PartyQuestReplicaRestoreExecutionDirective::FailClosed;
    }
    return PartyQuestReplicaRestoreExecutionDirective::Continue;
}

struct RestoreCrashBoundary
{
    PartyQuestReplicaRestoreExecutionBoundary Boundary;
    size_t Operation{};
};

PartyQuestReplicaRestoreExecutionDirective CrashAtBoundary(
    PartyQuestReplicaRestoreExecutionBoundary aBoundary,
    size_t aOperation,
    void* apContext) noexcept
{
    const auto& crash = *static_cast<const RestoreCrashBoundary*>(apContext);
    if (crash.Boundary == aBoundary && crash.Operation == aOperation)
        std::_Exit(86);
    return PartyQuestReplicaRestoreExecutionDirective::Continue;
}

struct RestoreDeadlineScript
{
    uint64_t Now{1};
    bool ExpireAfterAdmission{};
    bool ExpireOnFirstPublication{};
    size_t Reads{};
};

uint64_t ReadRestoreDeadline(void* apContext) noexcept
{
    auto& script = *static_cast<RestoreDeadlineScript*>(apContext);
    ++script.Reads;
    if (script.ExpireAfterAdmission && script.Reads > 1)
        return PartyQuestReplicaResourcePolicy::MaxExecutionNanoseconds + 1;
    return script.Now;
}

PartyQuestReplicaRestoreExecutionDirective ExpireRestoreAtBoundary(
    PartyQuestReplicaRestoreExecutionBoundary aBoundary,
    size_t aOperation,
    void* apContext) noexcept
{
    auto& script = *static_cast<RestoreDeadlineScript*>(apContext);
    if (script.ExpireOnFirstPublication &&
        aBoundary == PartyQuestReplicaRestoreExecutionBoundary::RestoredFilePublished &&
        aOperation == 0)
    {
        script.Now = PartyQuestReplicaResourcePolicy::MaxExecutionNanoseconds + 1;
    }
    return PartyQuestReplicaRestoreExecutionDirective::Continue;
}

uint64_t InvalidRestoreDeadline(void*) noexcept
{
    return 0;
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

void RunExecutorCrashProcess(
    const RestoreExecutorSandbox& acSandbox,
    const char* apPhase,
    uint64_t aWorldRevision,
    uint64_t aRestoreId)
{
    REQUIRE(SetExecutorEnvironment("TP_RESTORE_CRASH_ROOT", acSandbox.Root.string()));
    REQUIRE(SetExecutorEnvironment("TP_RESTORE_CRASH_PHASE", apPhase));

    const auto& executable = GetTPTestsExecutablePath();
    REQUIRE_FALSE(executable.empty());
    const std::string command =
        "\"" + executable.string() +
        "\" \"Restore executor subprocess crash helper\" --reporter compact";
    const int exitCode = std::system(command.c_str());

    ClearExecutorEnvironment("TP_RESTORE_CRASH_PHASE");
    ClearExecutorEnvironment("TP_RESTORE_CRASH_ROOT");
    REQUIRE(exitCode != 0);

    const auto paths = BuildExecutorPaths(acSandbox);
    const bool mutationCrash =
        std::string(apPhase) == "OriginalMoved" ||
        std::string(apPhase) == "FirstPublished";
    PartyQuestReplicaRestorePlan plan;
    if (mutationCrash)
    {
        plan = BuildMultiFileExecutorPlan(paths, aWorldRevision);
    }
    else
    {
        const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths, PartyQuestCheckpointKind::PreRepair, aWorldRevision) /
            "saves" / "Hero.ess";
        plan = BuildExecutorPlan(
            paths, checkpoint, paths.SavesDirectory / "Hero.ess", aWorldRevision);
    }
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, aRestoreId);
    REQUIRE(prepared.IsReady());

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kExecutorCampaign,
        kExecutorPlayer,
        PartyQuestReplicaRestoreJournal::GetJournalPath(*prepared.State));
    if (mutationCrash)
    {
        REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
        REQUIRE(report.RollbackPerformed);
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
            "ORIGINAL_SAVE_" + std::to_string(aWorldRevision));
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
            "ORIGINAL_COSAVE_" + std::to_string(aWorldRevision));
        REQUIRE_FALSE(std::filesystem::exists(
            paths.SidecarsDirectory / "external" / "RequiredProvider" / "State.bin"));
    }
    else
    {
        REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
            (std::string("CANONICAL_") + std::to_string(aWorldRevision)));
    }
}
} // namespace

TEST_CASE("Restore executor subprocess crash helper", "[.][quest.party-state.restore-executor][fault-helper]")
{
    const char* rootValue = std::getenv("TP_RESTORE_CRASH_ROOT");
    const char* phaseValue = std::getenv("TP_RESTORE_CRASH_PHASE");
    REQUIRE(rootValue != nullptr);
    REQUIRE(phaseValue != nullptr);

    const std::string phase = phaseValue;
    const uint64_t worldRevision =
        phase == "Prepared" ? 910 :
        phase == "BackupsReady" ? 911 :
        phase == "OriginalMoved" ? 913 : 914;
    const uint64_t restoreId =
        phase == "Prepared" ? 3001 :
        phase == "BackupsReady" ? 3002 :
        phase == "OriginalMoved" ? 3004 : 3005;
    REQUIRE((phase == "Prepared" || phase == "BackupsReady" ||
        phase == "OriginalMoved" || phase == "FirstPublished"));

    const auto paths = BuildExecutorPaths(std::filesystem::path(rootValue));
    if (phase == "OriginalMoved" || phase == "FirstPublished")
    {
        WriteMultiFileExecutorFixture(paths, worldRevision);
        const auto plan = BuildMultiFileExecutorPlan(paths, worldRevision);
        RestoreCrashBoundary crash{
            phase == "OriginalMoved"
                ? PartyQuestReplicaRestoreExecutionBoundary::OriginalMovedAside
                : PartyQuestReplicaRestoreExecutionBoundary::RestoredFilePublished,
            0};
        const auto report = PartyQuestReplicaRestoreExecutor::Execute(
            paths,
            plan,
            restoreId,
            {CrashAtBoundary, &crash});
        FAIL("restore returned instead of terminating at the injected crash boundary: " <<
            static_cast<int>(report.Status));
    }

    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, worldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, std::string("CANONICAL_") + std::to_string(worldRevision));
    WriteExecutorBytes(destination, std::string("ORIGINAL_") + std::to_string(worldRevision));

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, worldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, restoreId);
    if (phase == "BackupsReady")
    {
        WriteExecutorBytes(
            state.Operations[0].RollbackPath,
            std::string("ORIGINAL_") + std::to_string(worldRevision));
        REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
            PartyQuestReplicaRestoreJournalStatus::Ready);
        REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                    PartyQuestReplicaRestoreJournal::GetJournalPath(state), state) ==
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    }

    std::_Exit(86);
}

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

TEST_CASE("Prepared journal resumes exact restore after process crash", "[quest.party-state.restore-executor][fault]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 907;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_907");
    WriteExecutorBytes(destination, "ORIGINAL_907");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    const auto state = PrepareDurableExecutorState(paths, plan, 2003);
    REQUIRE(state.Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kExecutorCampaign,
        kExecutorPlayer,
        PartyQuestReplicaRestoreJournal::GetJournalPath(state));
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(ReadExecutorBytes(destination) == "CANONICAL_907");
}

TEST_CASE("BackupsReady journal resumes exact restore after process crash", "[quest.party-state.restore-executor][fault]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 908;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_908");
    WriteExecutorBytes(destination, "ORIGINAL_908");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 2004);
    REQUIRE(state.Operations.size() == 1);
    WriteExecutorBytes(state.Operations[0].RollbackPath, "ORIGINAL_908");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(ReadExecutorBytes(destination) == "CANONICAL_908");
}

TEST_CASE("Durable pre-mutation restore phases survive abrupt process termination", "[quest.party-state.restore-executor][fault][process]")
{
    SECTION("Prepared")
    {
        RestoreExecutorSandbox sandbox;
        RunExecutorCrashProcess(sandbox, "Prepared", 910, 3001);
    }

    SECTION("BackupsReady")
    {
        RestoreExecutorSandbox sandbox;
        RunExecutorCrashProcess(sandbox, "BackupsReady", 911, 3002);
    }
}

TEST_CASE("Partial multi-file publication rolls every destination back", "[quest.party-state.restore-executor][fault][multi-file]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 913;
    constexpr uint64_t kRestoreId = 3004;
    WriteMultiFileExecutorFixture(paths, kWorldRevision);
    const auto plan = BuildMultiFileExecutorPlan(paths, kWorldRevision);

    RestoreBoundaryFailureScript script{{
        {PartyQuestReplicaRestoreExecutionBoundary::RestoredFilePublished, 0}}};
    const auto report = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        kRestoreId,
        {FailAtScriptedBoundary, &script});

    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::ReplacementFailed);
    REQUIRE(report.CompletedOperations == 1);
    REQUIRE(report.RollbackPerformed);
    REQUIRE_FALSE(report.IsCheckpointRestored());
    REQUIRE(script.Next == script.Steps.size());
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_SAVE_913");
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
        "ORIGINAL_COSAVE_913");
    REQUIRE_FALSE(std::filesystem::exists(report.JournalPath.parent_path()));
}

TEST_CASE("Restore deadline preserves the journal boundary and exact originals", "[quest.party-state.restore-executor][resource-budget][timeout]")
{
    SECTION("expiry before MutationStarted leaves live replica untouched")
    {
        RestoreExecutorSandbox sandbox;
        const auto paths = BuildExecutorPaths(sandbox);
        constexpr uint64_t kWorldRevision = 916;
        WriteMultiFileExecutorFixture(paths, kWorldRevision);
        const auto plan = BuildMultiFileExecutorPlan(paths, kWorldRevision);

        RestoreDeadlineScript script{1, true};
        const auto report = PartyQuestReplicaRestoreExecutor::Execute(
            paths,
            plan,
            3007,
            {nullptr, &script, ReadRestoreDeadline});

        REQUIRE(report.Status ==
            PartyQuestReplicaRestoreExecutionStatus::OperationDeadlineExceeded);
        REQUIRE(report.CompletedOperations == 0);
        REQUIRE_FALSE(report.RollbackPerformed);
        REQUIRE_FALSE(report.IsCheckpointRestored());
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
            "ORIGINAL_SAVE_916");
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
            "ORIGINAL_COSAVE_916");
    }

    SECTION("expiry after publication completes exact rollback")
    {
        RestoreExecutorSandbox sandbox;
        const auto paths = BuildExecutorPaths(sandbox);
        constexpr uint64_t kWorldRevision = 917;
        WriteMultiFileExecutorFixture(paths, kWorldRevision);
        const auto plan = BuildMultiFileExecutorPlan(paths, kWorldRevision);

        RestoreDeadlineScript script{1, false, true};
        const auto report = PartyQuestReplicaRestoreExecutor::Execute(
            paths,
            plan,
            3008,
            {ExpireRestoreAtBoundary, &script, ReadRestoreDeadline});

        REQUIRE(report.Status ==
            PartyQuestReplicaRestoreExecutionStatus::OperationDeadlineExceeded);
        REQUIRE(report.CompletedOperations == 1);
        REQUIRE(report.RollbackPerformed);
        REQUIRE_FALSE(report.IsCheckpointRestored());
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
            "ORIGINAL_SAVE_917");
        REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
            "ORIGINAL_COSAVE_917");
        REQUIRE_FALSE(std::filesystem::exists(report.JournalPath.parent_path()));
    }
}

TEST_CASE("Interrupted multi-file rollback keeps a retryable mutation journal", "[quest.party-state.restore-executor][fault][multi-file]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 914;
    constexpr uint64_t kRestoreId = 3005;
    WriteMultiFileExecutorFixture(paths, kWorldRevision);
    const auto plan = BuildMultiFileExecutorPlan(paths, kWorldRevision);

    RestoreBoundaryFailureScript script{{
        {PartyQuestReplicaRestoreExecutionBoundary::RestoredFilePublished, 1},
        {PartyQuestReplicaRestoreExecutionBoundary::OriginalStateRestored, 1}}};
    const auto failed = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        kRestoreId,
        {FailAtScriptedBoundary, &script});

    REQUIRE(failed.Status == PartyQuestReplicaRestoreExecutionStatus::RollbackFailed);
    REQUIRE(failed.CompletedOperations == 2);
    REQUIRE_FALSE(failed.RollbackPerformed);
    REQUIRE_FALSE(failed.IsCheckpointRestored());
    REQUIRE(script.Next == script.Steps.size());
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
        "CANONICAL_SAVE_914");
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
        "ORIGINAL_COSAVE_914");

    const auto retained = PartyQuestReplicaRestoreJournalPersistence::Load(
        failed.JournalPath);
    REQUIRE(retained.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(retained.State.has_value());
    REQUIRE(retained.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::MutationStarted);
    REQUIRE(std::filesystem::exists(retained.State->TransactionDirectory));

    const auto recovered = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kExecutorCampaign,
        kExecutorPlayer,
        failed.JournalPath,
        {nullptr, nullptr, InvalidRestoreDeadline});
    REQUIRE(recovered.Status ==
        PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.IsCheckpointRestored());
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_SAVE_914");
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
        "ORIGINAL_COSAVE_914");
    REQUIRE_FALSE(std::filesystem::exists(retained.State->TransactionDirectory));
}

TEST_CASE("Interrupted rollback remembers removal of a newly published required sidecar", "[quest.party-state.restore-executor][fault][multi-file][sidecar]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 915;
    constexpr uint64_t kRestoreId = 3006;
    WriteMultiFileExecutorFixture(paths, kWorldRevision);
    const auto plan = BuildMultiFileExecutorPlan(paths, kWorldRevision);
    const auto sidecar = paths.SidecarsDirectory / "external" /
        "RequiredProvider" / "State.bin";
    REQUIRE_FALSE(std::filesystem::exists(sidecar));

    RestoreBoundaryFailureScript script{{
        {PartyQuestReplicaRestoreExecutionBoundary::RestoredFilePublished, 2},
        {PartyQuestReplicaRestoreExecutionBoundary::OriginalStateRestored, 2}}};
    const auto failed = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        kRestoreId,
        {FailAtScriptedBoundary, &script});

    REQUIRE(failed.Status == PartyQuestReplicaRestoreExecutionStatus::RollbackFailed);
    REQUIRE(failed.CompletedOperations == 3);
    REQUIRE_FALSE(failed.RollbackPerformed);
    REQUIRE_FALSE(failed.IsCheckpointRestored());
    REQUIRE(script.Next == script.Steps.size());
    REQUIRE_FALSE(std::filesystem::exists(sidecar));
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
        "CANONICAL_SAVE_915");
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
        "CANONICAL_COSAVE_915");

    const auto retained = PartyQuestReplicaRestoreJournalPersistence::Load(
        failed.JournalPath);
    REQUIRE(retained.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(retained.State.has_value());
    REQUIRE(retained.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::MutationStarted);

    const auto recovered = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kExecutorCampaign,
        kExecutorPlayer,
        failed.JournalPath);
    REQUIRE(recovered.Status ==
        PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.IsCheckpointRestored());
    REQUIRE_FALSE(std::filesystem::exists(sidecar));
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_SAVE_915");
    REQUIRE(ReadExecutorBytes(paths.SavesDirectory / "Hero.skse") ==
        "ORIGINAL_COSAVE_915");
    REQUIRE_FALSE(std::filesystem::exists(retained.State->TransactionDirectory));
}

TEST_CASE("Multi-file restore recovers exact originals after rename-window process crashes", "[quest.party-state.restore-executor][fault][multi-file][process]")
{
    SECTION("destination moved aside before publication")
    {
        RestoreExecutorSandbox sandbox;
        RunExecutorCrashProcess(sandbox, "OriginalMoved", 913, 3004);
    }

    SECTION("first restored file published")
    {
        RestoreExecutorSandbox sandbox;
        RunExecutorCrashProcess(sandbox, "FirstPublished", 914, 3005);
    }
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

TEST_CASE("Restored recovery rolls back and remains uncommitted when live bytes diverge", "[quest.party-state.restore-executor][fault]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 912;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_912");
    WriteExecutorBytes(destination, "ORIGINAL_912");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    auto state = PrepareDurableExecutorState(paths, plan, 3003);
    WriteExecutorBytes(state.Operations[0].RollbackPath, "ORIGINAL_912");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteExecutorBytes(destination, "CANONICAL_912");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkRestored(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteExecutorBytes(destination, "CORRUPTED_RESTORED_912");

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, journalPath);
    REQUIRE(report.Status ==
        PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed);
    REQUIRE(report.RollbackPerformed);
    REQUIRE_FALSE(report.IsCheckpointRestored());
    REQUIRE(ReadExecutorBytes(destination) == "ORIGINAL_912");
}

TEST_CASE("Committed recovery re-verifies live bytes before reporting an exact restore", "[quest.party-state.restore-executor][fault]")
{
    RestoreExecutorSandbox sandbox;
    const auto paths = BuildExecutorPaths(sandbox);
    constexpr uint64_t kWorldRevision = 909;
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, kWorldRevision) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteExecutorBytes(checkpoint, "CANONICAL_909");
    WriteExecutorBytes(destination, "ORIGINAL_909");

    const auto plan = BuildExecutorPlan(paths, checkpoint, destination, kWorldRevision);
    const auto executed = PartyQuestReplicaRestoreExecutor::Execute(paths, plan, 2005);
    REQUIRE(executed.Status == PartyQuestReplicaRestoreExecutionStatus::Success);

    const auto verified = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, executed.JournalPath);
    REQUIRE(verified.Status == PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted);
    REQUIRE(verified.IsCheckpointRestored());

    WriteExecutorBytes(destination, "CORRUPTED_AFTER_COMMIT");
    const auto rejected = PartyQuestReplicaRestoreExecutor::Recover(
        paths, kExecutorCampaign, kExecutorPlayer, executed.JournalPath);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed);
    REQUIRE_FALSE(rejected.IsCheckpointRestored());
    REQUIRE_FALSE(rejected.RollbackPerformed);
    REQUIRE(ReadExecutorBytes(destination) == "CORRUPTED_AFTER_COMMIT");

    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(executed.JournalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
}
