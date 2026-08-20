#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_process_owner_test_support.h>
#include <party_quest_runtime_recovery_coordinator_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{0x75767778797A7B7Cull, 0x85868788898A8B8Cull};
const PartyQuestPlayerProfileId kPlayer{0x95969798999A9B9Cull, 0xA5A6A7A8A9AAABACull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_pending_attempt_publication_" +
             std::to_string(nonce));
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

PartyQuestRuntimeApplyRequest BuildRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(101, 0x2B00);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 11;
    snapshot.InitiatorPlayerId = 23;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

PartyQuestReplicaRestorePlan PublishCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision,
    const std::string& acPreRepairBytes)
{
    const auto liveSave = acPaths.SavesDirectory / "Hero.ess";
    WriteText(liveSave, acPreRepairBytes);
    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        liveSave,
        "Hero.ess");
    REQUIRE(spec.has_value());
    const auto checkpointPlan =
        PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision,
            {*spec});
    REQUIRE(checkpointPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(acPaths, kCampaign, kPlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                aWorldRevision,
                checkpointPlan).IsReady());

    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            aWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    const auto restorePlan = PartyQuestReplicaRestorePlanner::Build(
        acPaths,
        kCampaign,
        kPlayer,
        *loaded.Manifest);
    REQUIRE(restorePlan.IsReady());
    return restorePlan;
}

PartyQuestRuntimeRestoreAttemptResult InitializeStrongAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId)
{
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(acPaths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability =
        lease.CreatePublicationCapability(acPaths, kCampaign, kPlayer);
    REQUIRE(capability.Protects(acPaths, kCampaign, kPlayer));
    return PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        acPaths,
        kCampaign,
        kPlayer,
        aTransactionId,
        capability);
}

std::filesystem::path MovePrimaryMappingToPendingPublication(
    const PartyQuestRuntimeRestoreAttemptResult& acAttempt)
{
    REQUIRE(acAttempt.State.has_value());
    REQUIRE(std::filesystem::exists(acAttempt.StatePath));

    auto temporary = acAttempt.StatePath;
    temporary += ".tmp";
    REQUIRE_FALSE(std::filesystem::exists(temporary));

    std::error_code ec;
    std::filesystem::rename(acAttempt.StatePath, temporary, ec);
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(std::filesystem::exists(acAttempt.StatePath));
    REQUIRE(std::filesystem::exists(temporary));
    return temporary;
}

std::filesystem::path PersistLegacyJournal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aTransactionId)
{
    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        acPaths,
        acPlan,
        aTransactionId);
    REQUIRE(prepared.IsReady());
    REQUIRE(prepared.State.has_value());

    const auto journalPath =
        PartyQuestReplicaRestoreJournal::GetJournalPath(*prepared.State);
    std::error_code ec;
    std::filesystem::create_directories(
        prepared.State->TransactionDirectory,
        ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                *prepared.State) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    return journalPath;
}

void RemoveLegacyJournalDirectory(const std::filesystem::path& acJournalPath)
{
    std::error_code ec;
    REQUIRE(std::filesystem::remove_all(acJournalPath.parent_path(), ec) > 0);
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(std::filesystem::exists(acJournalPath));
}

PartyQuestRuntimeRecoveryState BuildRecoveryState(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(101, 0x2B00);
    active.CanonicalDigest = 0xDDBBAA9988776655ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions,
        active.CanonicalDigest,
        0x72002002);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.Active = active;
    return state;
}

PartyQuestRuntimeApplySession BuildBlockedSession(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE(session.RestoreRecoveryState(
                BuildRecoveryState(aTransactionId, aWorldRevision)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    return session;
}
} // namespace

TEST_CASE(
    "pending strong attempt publication blocks crash recovery legacy downgrade",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][pending-publication][conflict][crash]")
{
    Sandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 28201;
    constexpr uint64_t worldRevision = 1920;
    const std::string preRepairBytes = "PRE_REPAIR_PENDING_CRASH";
    const std::string mutatedBytes = "MUTATED_PENDING_CRASH";
    const auto restorePlan =
        PublishCheckpoint(*paths, worldRevision, preRepairBytes);
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteText(liveSave, mutatedBytes);

    const auto strongAttempt = InitializeStrongAttempt(*paths, transactionId);
#ifdef _WIN32
    REQUIRE(strongAttempt.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(strongAttempt.IsUsable());
    REQUIRE(strongAttempt.State.has_value());
    const auto strongState = *strongAttempt.State;
    REQUIRE(strongState.CurrentRestoreId != 0);
    REQUIRE(strongState.CurrentRestoreId != transactionId);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    const auto pendingPath =
        MovePrimaryMappingToPendingPublication(strongAttempt);
    const std::string pendingBytes = ReadText(pendingPath);
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::Load(
                *paths,
                kCampaign,
                kPlayer,
                transactionId)
                .Status == PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);

    const auto legacyJournalPath =
        PersistLegacyJournal(*paths, restorePlan, transactionId);
    REQUIRE(legacyJournalPath.lexically_normal() !=
        strongAttempt.JournalPath.lexically_normal());

    auto session = BuildBlockedSession(transactionId, worldRevision);
    const auto conflict =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
            session,
            *paths);
    REQUIRE(conflict.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(conflict.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);
    REQUIRE(conflict.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
    REQUIRE(conflict.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);

    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadText(liveSave) == mutatedBytes);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.StatePath));
    REQUIRE(std::filesystem::exists(pendingPath));
    REQUIRE(ReadText(pendingPath) == pendingBytes);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    const auto legacyAfterConflict =
        PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
    REQUIRE(legacyAfterConflict.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacyAfterConflict.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    REQUIRE(legacyAfterConflict.State.has_value());
    REQUIRE(legacyAfterConflict.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::Prepared);

    // Once the conflicting legacy evidence is explicitly removed, recovery may
    // publish the already-durable staged mapping. It must keep the same exact
    // RestoreId rather than allocate a replacement or fork this transaction.
    RemoveLegacyJournalDirectory(legacyJournalPath);
    const auto recovered =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
            session,
            *paths);
    REQUIRE(recovered.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(recovered.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization);
    REQUIRE(recovered.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable);
    REQUIRE(recovered.RestoreId == strongState.CurrentRestoreId);
    REQUIRE(recovered.DurableRestoreStatus ==
        PartyQuestReplicaDurableRestoreStatus::Success);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(ReadText(liveSave) == preRepairBytes);
    REQUIRE(std::filesystem::exists(strongAttempt.StatePath));
    REQUIRE_FALSE(std::filesystem::exists(pendingPath));

    const auto published = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(published.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(published.State.has_value());
    REQUIRE(*published.State == strongState);
    REQUIRE(published.State->CurrentRestoreId == strongState.CurrentRestoreId);
#endif
}

TEST_CASE(
    "pending strong attempt publication blocks live recovery legacy downgrade",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][pending-publication][conflict][live-recovery]")
{
    Sandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 28202;
    constexpr uint64_t worldRevision = 1930;
    const std::string preRepairBytes = "PRE_REPAIR_PENDING_LIVE";
    const std::string mutatedBytes = "MUTATED_PENDING_LIVE";
    const auto restorePlan =
        PublishCheckpoint(*paths, worldRevision, preRepairBytes);
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteText(liveSave, mutatedBytes);

    const auto strongAttempt = InitializeStrongAttempt(*paths, transactionId);
#ifdef _WIN32
    REQUIRE(strongAttempt.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(strongAttempt.IsUsable());
    REQUIRE(strongAttempt.State.has_value());
    const auto strongState = *strongAttempt.State;
    REQUIRE(strongState.CurrentRestoreId != 0);
    REQUIRE(strongState.CurrentRestoreId != transactionId);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    const auto pendingPath =
        MovePrimaryMappingToPendingPublication(strongAttempt);
    const std::string pendingBytes = ReadText(pendingPath);
    const auto legacyJournalPath =
        PersistLegacyJournal(*paths, restorePlan, transactionId);

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kCampaign,
        kPlayer,
        *paths);
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();

    const auto request = BuildRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto conflict = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(conflict.Status ==
        PartyQuestRuntimeRecoveryStatus::RestoreJournalConflict);
    REQUIRE(conflict.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);
    REQUIRE(conflict.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
    REQUIRE(conflict.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);

    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->TransactionId == transactionId);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(ReadText(liveSave) == mutatedBytes);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.StatePath));
    REQUIRE(std::filesystem::exists(pendingPath));
    REQUIRE(ReadText(pendingPath) == pendingBytes);
    REQUIRE_FALSE(std::filesystem::exists(strongAttempt.JournalPath));

    const auto legacyAfterConflict =
        PartyQuestReplicaRestoreJournalPersistence::Load(legacyJournalPath);
    REQUIRE(legacyAfterConflict.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacyAfterConflict.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    REQUIRE(legacyAfterConflict.State.has_value());
    REQUIRE(legacyAfterConflict.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::Prepared);

    RemoveLegacyJournalDirectory(legacyJournalPath);
    const auto recovered = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(recovered.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(recovered.RestoreAttemptStatus ==
        PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization);
    REQUIRE(recovered.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::PowerLossDurable);
    REQUIRE(recovered.RestoreId == strongState.CurrentRestoreId);
    REQUIRE(recovered.DurableRestoreStatus ==
        PartyQuestReplicaDurableRestoreStatus::Success);
    REQUIRE(ReadText(liveSave) == preRepairBytes);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE(std::filesystem::exists(strongAttempt.StatePath));
    REQUIRE_FALSE(std::filesystem::exists(pendingPath));

    const auto published = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(published.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(published.State.has_value());
    REQUIRE(*published.State == strongState);
    REQUIRE(published.State->CurrentRestoreId == strongState.CurrentRestoreId);
#endif
}
