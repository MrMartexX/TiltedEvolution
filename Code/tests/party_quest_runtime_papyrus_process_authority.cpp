#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <type_traits>
#include <utility>
#include <vector>

namespace
{
const PartyQuestCampaignId kPapyrusAuthorityCampaign{
    0x7172737475767778ull,
    0x8182838485868788ull};
const PartyQuestPlayerProfileId kPapyrusAuthorityPlayer{
    0x9192939495969798ull,
    0xA1A2A3A4A5A6A7A8ull};

PartyQuestRuntimeApplyRequest BuildProcessGuardRequest(uint64_t aTransactionId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(95, 0x2600);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 6;
    snapshot.InitiatorPlayerId = 11;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 260;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);

    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

class ProcessIdleObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    explicit ProcessIdleObserver(uint64_t aGeneration)
        : m_generation(aGeneration)
    {
    }

    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Idle,
            0,
            m_generation,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains};
    }

private:
    uint64_t m_generation{};
};

struct ProcessGuardCleanup
{
    uint64_t TransactionId{};

    ~ProcessGuardCleanup()
    {
        auto& guard = PartyQuestSaveGuard::GetProcessGuard();
        if (TransactionId != 0 && guard.GetTransactionId() == TransactionId)
            guard.Release(TransactionId);
    }
};
} // namespace

TEST_CASE("Process guarded runtime requires trusted Papyrus monitor evidence", "[quest.party-state.runtime-guard][quiescence][observer-authority]")
{
    static_assert(std::is_same_v<
        decltype(std::declval<PartyQuestRuntimeGuardedSession&>().GetRuntimeSession()),
        const PartyQuestRuntimeApplySession&>);

    constexpr uint64_t transactionId = 26001;
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    ProcessGuardCleanup cleanup{transactionId};

    PartyQuestRuntimeApplySession session(
        kPapyrusAuthorityCampaign,
        kPapyrusAuthorityPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession guarded(session);
    REQUIRE(&guarded.GetRuntimeSession() == &session);
    REQUIRE(&guarded.GetSaveGuard() == &processGuard);

    const auto request = BuildProcessGuardRequest(transactionId);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(session.MarkCheckpointCreated(transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);

    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(transactionId));
    REQUIRE(tracker.Observe(transactionId, 0, 70) ==
        PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(transactionId, 0, 70) ==
        PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto trackerAuthorization = tracker.Authorize();
    REQUIRE(trackerAuthorization.has_value());

    const auto rejected = guarded.MarkPapyrusQuiescent(
        tracker,
        std::move(*trackerAuthorization));
    REQUIRE(rejected.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(rejected.GuardHeld);
    REQUIRE(trackerAuthorization->IsVerified());
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::WaitingForPapyrus);

    ProcessIdleObserver observer(80);
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor monitor(observer);
    REQUIRE(monitor.Begin(transactionId, 100, 1000, observerAuthorization));
    REQUIRE(monitor.IsAuthoritativeSession());
    REQUIRE(monitor.Poll(transactionId, 110) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(transactionId, 120) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto monitorAuthorization = monitor.Authorize();
    REQUIRE(monitorAuthorization.has_value());

    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    const auto accepted = guarded.MarkPapyrusQuiescent(
        monitor,
        std::move(*monitorAuthorization),
        verificationMonitor,
        120);
    REQUIRE(accepted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(accepted.GuardHeld);
    REQUIRE_FALSE(monitorAuthorization->IsVerified());
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
    REQUIRE(verificationMonitor.GetTransactionId() == transactionId);
    REQUIRE(verificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);

    // Raw process-guard resnapshotting must not bypass the bounded monitor.
    const auto rawResnapshot = guarded.SubmitResnapshot(
        transactionId,
        request.CanonicalSnapshot);
    REQUIRE(rawResnapshot.Verification ==
        PartyQuestRuntimeVerificationStatus::InvalidState);
    REQUIRE_FALSE(rawResnapshot.PersistenceFailed);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
    REQUIRE(verificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);

    const auto first = guarded.SubmitVerificationResnapshot(
        verificationMonitor,
        transactionId,
        130,
        request.CanonicalSnapshot);
    REQUIRE(first.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(first.Verification ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(first.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE_FALSE(first.PersistenceFailed);

    const auto second = guarded.SubmitVerificationResnapshot(
        verificationMonitor,
        transactionId,
        140,
        request.CanonicalSnapshot);
    REQUIRE(second.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(second.Verification == PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(second.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::Stable);
    REQUIRE_FALSE(second.PersistenceFailed);

    // Raw process-guard commit cannot skip the monitor's final deadline check.
    const auto rawCommit = guarded.Commit(transactionId);
    REQUIRE(rawCommit.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(rawCommit.GuardHeld);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToCommit);

    const auto committed = guarded.Commit(
        verificationMonitor,
        transactionId,
        150);
    REQUIRE(committed.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(committed.GuardHeld);
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE(verificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Inactive);
    REQUIRE(session.GetCoordinator().IsCommitted(transactionId));
}

TEST_CASE("Stable verification cannot be committed after its deadline", "[quest.party-state.runtime-guard][verification-budget][commit]")
{
    constexpr uint64_t transactionId = 26002;
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    ProcessGuardCleanup cleanup{transactionId};

    PartyQuestRuntimeApplySession session(
        kPapyrusAuthorityCampaign,
        kPapyrusAuthorityPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession guarded(session);
    const auto request = BuildProcessGuardRequest(transactionId);

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.MarkCheckpointCreated(transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);

    ProcessIdleObserver observer(90);
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor papyrusMonitor(observer);
    REQUIRE(papyrusMonitor.Begin(transactionId, 100, 1000, observerAuthorization));
    REQUIRE(papyrusMonitor.Poll(transactionId, 110) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(papyrusMonitor.Poll(transactionId, 120) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto papyrusAuthorization = papyrusMonitor.Authorize();
    REQUIRE(papyrusAuthorization.has_value());

    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    REQUIRE(guarded.MarkPapyrusQuiescent(
                papyrusMonitor,
                std::move(*papyrusAuthorization),
                verificationMonitor,
                120).Status == PartyQuestRuntimeGuardStatus::Ready);

    REQUIRE(guarded.SubmitVerificationResnapshot(
                verificationMonitor,
                transactionId,
                130,
                request.CanonicalSnapshot).Verification ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    const auto stable = guarded.SubmitVerificationResnapshot(
        verificationMonitor,
        transactionId,
        140,
        request.CanonicalSnapshot);
    REQUIRE(stable.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(stable.Verification == PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToCommit);

    const auto expired = guarded.Commit(
        verificationMonitor,
        transactionId,
        120 + PartyQuestRuntimeVerificationMonitor::kTimeoutMs);
    REQUIRE(expired.Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(expired.TransitionStatus ==
        PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired);
    REQUIRE(expired.GuardHeld);
    REQUIRE(verificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::TimedOut);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToCommit);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE_FALSE(session.GetCoordinator().IsCommitted(transactionId));
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}
