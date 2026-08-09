#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kRecoveryCampaign{0x0123456789ABCDEFull, 0x1029384756ABCDEFull};
const PartyQuestPlayerProfileId kRecoveryPlayer{0xFEDCBA9876543210ull, 0x5647382910FEDCBAull};

struct RecoverySandbox
{
    std::filesystem::path Root;

    RecoverySandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_recovery_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RecoverySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRecoveryBytes(
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

std::string ReadRecoveryBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    std::string bytes;
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    REQUIRE(size >= 0);
    bytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE((file.good() || file.eof()));
    return bytes;
}

PartyQuestCoopSavePaths BuildRecoveryPaths(const RecoverySandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kRecoveryCampaign,
        kRecoveryPlayer);
    REQUIRE(paths.has_value());
    REQUIRE(PartyQuestCoopSaveLayout::Matches(
        *paths,
        kRecoveryCampaign,
        kRecoveryPlayer));
    return *paths;
}

PartyQuestReplicaCopyPlan BuildSingleFileCheckpointPlan(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aWorldRevision)
{
    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        acPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    REQUIRE(spec.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        acPaths,
        aKind,
        aWorldRevision,
        {*spec});
    REQUIRE(plan.IsReady());
    return plan;
}

void PublishCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aWorldRevision,
    const std::string& acCheckpointBytes)
{
    WriteRecoveryBytes(acPaths.SavesDirectory / "Hero.ess", acCheckpointBytes);
    PartyQuestReplicaSnapshotManager manager(
        acPaths,
        kRecoveryCampaign,
        kRecoveryPlayer);
    const auto plan = BuildSingleFileCheckpointPlan(acPaths, aKind, aWorldRevision);
    const auto snapshot = manager.EnsureRevisionCheckpoint(
        aKind,
        aWorldRevision,
        plan);
    REQUIRE(snapshot.Status == PartyQuestReplicaSnapshotStatus::Ready);
}

PartyQuestRuntimeRecoveryState BuildBlockedRecoveryState(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(51, 0x1000);
    active.CanonicalDigest = 0xAABBCCDDEEFF0011ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions, active.CanonicalDigest, 0x51001001);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kRecoveryCampaign;
    state.PlayerProfileId = kRecoveryPlayer;
    state.Active = active;
    return state;
}

struct RecoveryDurableCapture
{
    bool Allow{true};
    std::vector<PartyQuestRuntimeRecoveryState> States;

    bool Persist(const PartyQuestRuntimeRecoveryState& acState)
    {
        if (!Allow)
            return false;
        States.push_back(acState);
        return true;
    }
};

PartyQuestRuntimeApplySession BuildBlockedSession(
    RecoveryDurableCapture& aCapture,
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplySession session(
        kRecoveryCampaign,
        kRecoveryPlayer,
        [&aCapture](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aCapture.Persist(acState);
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    const auto disposition = session.RestoreRecoveryState(
        BuildBlockedRecoveryState(aTransactionId, aWorldRevision));
    REQUIRE(disposition == PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);
    return session;
}

PartyQuestReplicaRestorePlan LoadExactPreRepairRestorePlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());

    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        acPaths,
        kRecoveryCampaign,
        kRecoveryPlayer,
        *loaded.Manifest);
    REQUIRE(plan.IsReady());
    return plan;
}
} // namespace

TEST_CASE("Crash recovery restores exact PreRepair revision before clearing runtime barrier", "[quest.party-state.runtime-recovery]")
{
    RecoverySandbox sandbox;
    const auto paths = BuildRecoveryPaths(sandbox);
    constexpr uint64_t kWorldRevision = 1600;
    constexpr uint64_t kTransactionId = 21001;

    PublishCheckpoint(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision,
        "PRE_REPAIR_1600");
    WriteRecoveryBytes(paths.SavesDirectory / "Hero.ess", "MUTATED_AFTER_1600");

    RecoveryDurableCapture capture;
    auto session = BuildBlockedSession(capture, kTransactionId, kWorldRevision);
    const auto result = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);

    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(result.IsResolved());
    REQUIRE(result.TransactionId == kTransactionId);
    REQUIRE(result.RestoreId == kTransactionId);
    REQUIRE(result.TargetWorldRevision == kWorldRevision);
    REQUIRE(result.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(result.RuntimeTransition == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "PRE_REPAIR_1600");
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() == nullptr);
    REQUIRE_FALSE(capture.States.empty());
    REQUIRE(capture.States.back().Active == std::nullopt);
}

TEST_CASE("Crash recovery never guesses LastKnownGood when exact PreRepair revision is absent", "[quest.party-state.runtime-recovery]")
{
    RecoverySandbox sandbox;
    const auto paths = BuildRecoveryPaths(sandbox);
    constexpr uint64_t kWorldRevision = 1610;

    PublishCheckpoint(
        paths,
        PartyQuestCheckpointKind::LastKnownGood,
        kWorldRevision,
        "LAST_KNOWN_GOOD_1610");
    WriteRecoveryBytes(paths.SavesDirectory / "Hero.ess", "MUTATED_1610");

    RecoveryDurableCapture capture;
    auto session = BuildBlockedSession(capture, 21002, kWorldRevision);
    const auto result = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);

    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::CheckpointMissing);
    REQUIRE(result.ManifestStatus == PartyQuestReplicaManifestPersistenceStatus::FileNotFound);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "MUTATED_1610");
}

TEST_CASE("Durable barrier clear can be retried after checkpoint restore already committed", "[quest.party-state.runtime-recovery]")
{
    RecoverySandbox sandbox;
    const auto paths = BuildRecoveryPaths(sandbox);
    constexpr uint64_t kWorldRevision = 1620;
    constexpr uint64_t kTransactionId = 21003;

    PublishCheckpoint(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision,
        "PRE_REPAIR_1620");
    WriteRecoveryBytes(paths.SavesDirectory / "Hero.ess", "MUTATED_1620");

    RecoveryDurableCapture capture;
    auto session = BuildBlockedSession(capture, kTransactionId, kWorldRevision);
    capture.Allow = false;

    const auto first = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);
    REQUIRE(first.Status == PartyQuestRuntimeRecoveryStatus::RuntimeStatePersistenceFailed);
    REQUIRE(first.RestoreId == kTransactionId);
    REQUIRE(first.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "PRE_REPAIR_1620");
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());

    capture.Allow = true;
    const auto second = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);
    REQUIRE(second.Status == PartyQuestRuntimeRecoveryStatus::AlreadyRestored);
    REQUIRE(second.RestoreId == kTransactionId);
    REQUIRE(second.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted);
    REQUIRE(second.RuntimeTransition == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "PRE_REPAIR_1620");
}

TEST_CASE("Interrupted restore rollback keeps runtime barrier until a later exact restore completes", "[quest.party-state.runtime-recovery]")
{
    RecoverySandbox sandbox;
    const auto paths = BuildRecoveryPaths(sandbox);
    constexpr uint64_t kWorldRevision = 1630;
    constexpr uint64_t kTransactionId = 21004;

    PublishCheckpoint(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision,
        "PRE_REPAIR_1630");
    WriteRecoveryBytes(paths.SavesDirectory / "Hero.ess", "MUTATED_1630");

    const auto restorePlan = LoadExactPreRepairRestorePlan(paths, kWorldRevision);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        paths,
        restorePlan,
        kTransactionId);
    REQUIRE(prepared.IsReady());
    auto restoreState = *prepared.State;
    REQUIRE(restoreState.RestoreId == kTransactionId);
    REQUIRE(restoreState.Operations.size() == 1);
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(restoreState);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                restoreState) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteRecoveryBytes(
        restoreState.Operations[0].RollbackPath,
        "MUTATED_1630");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(restoreState) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                restoreState) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(restoreState) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                restoreState) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    // Simulate a crash after this file had already been replaced.
    WriteRecoveryBytes(paths.SavesDirectory / "Hero.ess", "PRE_REPAIR_1630");

    RecoveryDurableCapture capture;
    auto session = BuildBlockedSession(capture, kTransactionId, kWorldRevision);
    const auto rollback = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);
    REQUIRE(rollback.Status ==
        PartyQuestRuntimeRecoveryStatus::RollbackRecoveredRetryRequired);
    REQUIRE(rollback.RestoreId == kTransactionId);
    REQUIRE(rollback.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "MUTATED_1630");
    REQUIRE_FALSE(std::filesystem::exists(restoreState.TransactionDirectory));

    const auto retried = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        paths);
    REQUIRE(retried.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(retried.RestoreId == kTransactionId);
    REQUIRE(retried.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadRecoveryBytes(paths.SavesDirectory / "Hero.ess") == "PRE_REPAIR_1630");
}
