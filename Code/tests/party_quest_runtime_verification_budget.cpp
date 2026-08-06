#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <utility>

namespace
{
const PartyQuestCampaignId kVerificationBudgetCampaign{
    0xB1B2B3B4B5B6B7B8ull,
    0xC1C2C3C4C5C6C7C8ull};
const PartyQuestPlayerProfileId kVerificationBudgetPlayer{
    0xD1D2D3D4D5D6D7D8ull,
    0xE1E2E3E4E5E6E7E8ull};

PartyQuestRuntimeApplyRequest BuildVerificationBudgetRequest(
    uint64_t aTransactionId,
    uint32_t aQuestLocalId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(96, aQuestLocalId);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 8;
    snapshot.InitiatorPlayerId = 12;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 300;
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

class VerificationIdleObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        // This authoritative fixture must prove the complete Papyrus work envelope.
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Idle,
            0,
            900,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains};
    }
};

struct VerificationProcessGuardCleanup
{
    uint64_t TransactionId{};

    ~VerificationProcessGuardCleanup()
    {
        auto& guard = PartyQuestSaveGuard::GetProcessGuard();
        if (TransactionId != 0 && guard.GetTransactionId() == TransactionId)
            guard.Release(TransactionId);
    }
};

void AdvanceToBoundedVerification(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestRuntimeApplyRequest& acRequest,
    PartyQuestRuntimeVerificationMonitor& aVerificationMonitor)
{
    const uint64_t transactionId = acRequest.TransactionId;
    REQUIRE(aGuarded.Begin(acRequest).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(aSession.MarkCheckpointCreated(transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(aGuarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);

    VerificationIdleObserver observer;
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor papyrusMonitor(observer);
    REQUIRE(papyrusMonitor.Begin(transactionId, 100, 1000, observerAuthorization));
    REQUIRE(papyrusMonitor.Poll(transactionId, 110) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(papyrusMonitor.Poll(transactionId, 120) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto authorization = papyrusMonitor.Authorize();
    REQUIRE(authorization.has_value());

    const auto transition = aGuarded.MarkPapyrusQuiescent(
        papyrusMonitor,
        std::move(*authorization),
        aVerificationMonitor,
        120);
    REQUIRE(transition.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(transition.GuardHeld);
    REQUIRE(aVerificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(aSession.GetCoordinator().GetActive() != nullptr);
    REQUIRE(aSession.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
}
} // namespace

TEST_CASE("Repeated verification divergence exhausts the process budget and requires exact recovery", "[quest.party-state.runtime-guard][verification-budget]")
{
    constexpr uint64_t transactionId = 30001;
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    VerificationProcessGuardCleanup cleanup{transactionId};

    PartyQuestRuntimeApplySession session(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; });
    PartyQuestRuntimeGuardedSession guarded(session);
    const auto request = BuildVerificationBudgetRequest(transactionId, 0x3000);
    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    AdvanceToBoundedVerification(guarded, session, request, verificationMonitor);

    QuestSnapshot divergent = request.CanonicalSnapshot;
    divergent.CurrentStage = 99;
    divergent.CompletedStages.push_back(99);
    divergent.Canonicalize();

    for (uint32_t index = 1;
         index < PartyQuestRuntimeVerificationMonitor::kMaxDivergentSamples;
         ++index)
    {
        const auto sample = guarded.SubmitVerificationResnapshot(
            verificationMonitor,
            transactionId,
            120 + index * 10,
            divergent);
        REQUIRE(sample.Status == PartyQuestRuntimeGuardStatus::Ready);
        REQUIRE(sample.Verification == PartyQuestRuntimeVerificationStatus::Diverged);
        REQUIRE(sample.MonitorStatus ==
            PartyQuestRuntimeVerificationMonitorStatus::Waiting);
        REQUIRE(sample.GuardHeld);
        REQUIRE(verificationMonitor.GetDivergentSamples() == index);
    }

    const auto exhausted = guarded.SubmitVerificationResnapshot(
        verificationMonitor,
        transactionId,
        160,
        divergent);
    REQUIRE(exhausted.Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(exhausted.Verification == PartyQuestRuntimeVerificationStatus::Diverged);
    REQUIRE(exhausted.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::DivergenceBudgetExceeded);
    REQUIRE(exhausted.GuardHeld);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);

    const auto other = BuildVerificationBudgetRequest(30002, 0x3001);
    const auto blocked = guarded.Begin(other);
    REQUIRE(blocked.Status == PartyQuestRuntimeGuardStatus::GuardBusy);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}

TEST_CASE("Verification deadline starts at Verifying transition and cannot wait forever for the first resnapshot", "[quest.party-state.runtime-guard][verification-budget]")
{
    constexpr uint64_t transactionId = 30101;
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    VerificationProcessGuardCleanup cleanup{transactionId};

    PartyQuestRuntimeApplySession session(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; });
    PartyQuestRuntimeGuardedSession guarded(session);
    const auto request = BuildVerificationBudgetRequest(transactionId, 0x3100);
    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    AdvanceToBoundedVerification(guarded, session, request, verificationMonitor);

    const auto wrongTransaction = guarded.PollVerification(
        verificationMonitor,
        transactionId + 1,
        130);
    REQUIRE(wrongTransaction.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(verificationMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto timedOut = guarded.PollVerification(
        verificationMonitor,
        transactionId,
        120 + PartyQuestRuntimeVerificationMonitor::kTimeoutMs);
    REQUIRE(timedOut.Status ==
        PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(timedOut.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::TimedOut);
    REQUIRE(timedOut.GuardHeld);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}
