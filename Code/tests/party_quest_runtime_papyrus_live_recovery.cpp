#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>
#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_process_owner_test_support.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kLiveRecoveryCampaign{
    0x3132333435363738ull,
    0x4142434445464748ull};
const PartyQuestPlayerProfileId kLiveRecoveryPlayer{
    0x5152535455565758ull,
    0x6162636465666768ull};

struct LiveRecoverySandbox
{
    std::filesystem::path Root;

    LiveRecoverySandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_live_recovery_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~LiveRecoverySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteLiveRecoveryBytes(
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

std::string ReadLiveRecoveryBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestRuntimeApplyRequest BuildLiveRecoveryRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(97, 0x2800);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 8;
    snapshot.InitiatorPlayerId = 17;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
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

class UnknownPapyrusObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Unknown,
            0,
            0};
    }
};

class UnsupportedPapyrusObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Unsupported,
            0,
            0};
    }
};
} // namespace

TEST_CASE("Papyrus timeout retains process guard until exact live PreRepair restore", "[quest.party-state.runtime-recovery][quiescence][live-recovery]")
{
    constexpr uint64_t transactionId = 28001;
    constexpr uint64_t worldRevision = 1880;

    LiveRecoverySandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kLiveRecoveryCampaign,
        kLiveRecoveryPlayer);
    REQUIRE(paths.has_value());

    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteLiveRecoveryBytes(liveSave, "PRE_REPAIR_1880");

    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        liveSave,
        "Hero.ess");
    REQUIRE(spec.has_value());
    const auto checkpointPlan =
        PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
            *paths,
            PartyQuestCheckpointKind::PreRepair,
            worldRevision,
            {*spec});
    REQUIRE(checkpointPlan.IsReady());

    {
        PartyQuestReplicaSnapshotManager manager(
            *paths,
            kLiveRecoveryCampaign,
            kLiveRecoveryPlayer);
        REQUIRE(manager.EnsureRevisionCheckpoint(
                    PartyQuestCheckpointKind::PreRepair,
                    worldRevision,
                    checkpointPlan).IsReady());
    }

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kLiveRecoveryCampaign,
        kLiveRecoveryPlayer,
        *paths);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const auto request = BuildLiveRecoveryRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    WriteLiveRecoveryBytes(liveSave, "MUTATED_AFTER_RUNTIME_APPLY");

    UnknownPapyrusObserver observer;
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor monitor(observer);
    REQUIRE(monitor.Begin(transactionId, 1000, 50, observerAuthorization));

    const auto waiting = guarded.PollPapyrusRuntime(monitor, transactionId, 1020);
    REQUIRE(waiting.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(waiting.GuardHeld);
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);

    const auto timedOut = guarded.PollPapyrusRuntime(monitor, transactionId, 1050);
    REQUIRE(timedOut.Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(timedOut.TransitionStatus ==
        PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired);
    REQUIRE(timedOut.GuardHeld);
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::TimedOut);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto blockedRequest = BuildLiveRecoveryRequest(transactionId + 1, worldRevision + 1);
    const auto blocked = guarded.Begin(blockedRequest);
    REQUIRE(blocked.Status == PartyQuestRuntimeGuardStatus::GuardBusy);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto recovered = guarded.ResolveLiveRecovery(*paths);
    REQUIRE(recovered.Status == PartyQuestRuntimeRecoveryStatus::Restored);
    REQUIRE(recovered.RestoreId == transactionId);
    REQUIRE(recovered.RuntimeTransition ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(ReadLiveRecoveryBytes(liveSave) == "PRE_REPAIR_1880");
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(processGuard.IsActive());
}

TEST_CASE("Only terminal authoritative monitor failures request live recovery", "[quest.party-state.runtime-recovery][quiescence][live-recovery]")
{
    constexpr uint64_t transactionId = 28002;
    constexpr uint64_t worldRevision = 1881;

    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kLiveRecoveryCampaign,
        kLiveRecoveryPlayer,
        "tp_party_quest_live_recovery_28002");
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const auto request = BuildLiveRecoveryRequest(transactionId, worldRevision);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                session,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);

    UnknownPapyrusObserver waitingObserver;
    const auto waitingAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(waitingObserver);
    PartyQuestPapyrusRuntimeMonitor waitingMonitor(waitingObserver);
    REQUIRE(waitingMonitor.Begin(
        transactionId,
        2000,
        1000,
        waitingAuthorization));

    const auto wrongTransaction = guarded.PollPapyrusRuntime(
        waitingMonitor,
        transactionId + 1,
        2010);
    REQUIRE(wrongTransaction.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);

    const auto waiting = guarded.PollPapyrusRuntime(
        waitingMonitor,
        transactionId,
        2020);
    REQUIRE(waiting.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(waiting.GuardHeld);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);

    REQUIRE(waitingMonitor.Reset(transactionId));
    UnsupportedPapyrusObserver unsupportedObserver;
    const auto unsupportedAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(unsupportedObserver);
    PartyQuestPapyrusRuntimeMonitor unsupportedMonitor(unsupportedObserver);
    REQUIRE(unsupportedMonitor.Begin(
        transactionId,
        3000,
        1000,
        unsupportedAuthorization));

    const auto unsupported = guarded.PollPapyrusRuntime(
        unsupportedMonitor,
        transactionId,
        3010);
    REQUIRE(unsupported.Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(unsupported.GuardHeld);
    REQUIRE(unsupportedMonitor.GetStatus() ==
        PartyQuestPapyrusRuntimeMonitorStatus::Unsupported);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
}
