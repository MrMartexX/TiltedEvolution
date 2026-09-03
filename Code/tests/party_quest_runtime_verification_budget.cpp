#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>
#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_process_owner_test_support.h>
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

struct VerificationCompatibilityFixture
{
    PartyQuestRuntimeCompatibilityRequirement Requirement;
    PartyQuestRuntimeCompatibilityFacts Facts;
    uint64_t Fingerprint{};
};

VerificationCompatibilityFixture BuildCompatibility(const GameId& acQuestId)
{
    VerificationCompatibilityFixture fixture;
    fixture.Requirement.QuestId = acQuestId;
    fixture.Requirement.ProfileVersion = 7;
    fixture.Requirement.ResolvedRecordFingerprint = 0x7100710071007100ull;
    fixture.Requirement.WinningOverrideFingerprint = 0x7200720072007200ull;
    fixture.Requirement.ScriptFingerprint = 0x7300730073007300ull;
    fixture.Requirement.NativeAdapterFingerprint = 0x7400740074007400ull;
    fixture.Requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;

    fixture.Facts.ProfileVersion = fixture.Requirement.ProfileVersion;
    fixture.Facts.ResolvedRecordFingerprint = fixture.Requirement.ResolvedRecordFingerprint;
    fixture.Facts.WinningOverrideFingerprint = fixture.Requirement.WinningOverrideFingerprint;
    fixture.Facts.ScriptFingerprint = fixture.Requirement.ScriptFingerprint;
    fixture.Facts.NativeAdapterFingerprint = fixture.Requirement.NativeAdapterFingerprint;
    fixture.Facts.AdapterMutationComponents = fixture.Requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        fixture.Requirement,
        fixture.Facts);
    REQUIRE(decision.IsAuthorized());
    fixture.Fingerprint = decision.SafetyProfile.GetCompatibilityFingerprint();
    REQUIRE(fixture.Fingerprint != 0);
    return fixture;
}

PartyQuestRuntimeApplyRequest BuildVerificationBudgetRequest(
    uint64_t aTransactionId,
    uint32_t aQuestLocalId,
    const VerificationCompatibilityFixture& acCompatibility)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(96, aQuestLocalId);
    REQUIRE(snapshot.QuestId == acCompatibility.Requirement.QuestId);
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
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlanWithCompatibilityFingerprint(
        request.Plan,
        snapshot,
        acCompatibility.Fingerprint);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

class VerificationIdleObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Idle,
            0,
            900,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains};
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
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                aSession,
                transactionId) ==
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

PartyQuestRuntimeGuardedVerificationResult SubmitSample(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId,
    uint64_t aNowMs,
    QuestSnapshot aSnapshot,
    const VerificationCompatibilityFixture& acCompatibility)
{
    auto begin = PartyQuestRuntimeVerificationGate::BeginAttempt(
        aGuarded, aSession, aMonitor, aTransactionId);
    REQUIRE(begin.Status == PartyQuestRuntimeVerificationEvidenceStatus::Accepted);
    REQUIRE(begin.Attempt.has_value());
    return PartyQuestRuntimeVerificationGate::Submit(
        aGuarded,
        aSession,
        aMonitor,
        std::move(*begin.Attempt),
        aNowMs,
        acCompatibility.Requirement,
        [snapshot = std::move(aSnapshot)](const GameId& acQuestId) mutable
            -> std::optional<QuestSnapshot>
        {
            if (snapshot.QuestId != acQuestId)
                return std::nullopt;
            return snapshot;
        },
        [facts = acCompatibility.Facts](const GameId&)
            -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            return facts;
        });
}

PartyQuestRuntimeGuardedVerificationResult SubmitAttempt(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    PartyQuestRuntimeVerificationAttempt&& aAttempt,
    uint64_t aNowMs,
    QuestSnapshot aSnapshot,
    const VerificationCompatibilityFixture& acCompatibility)
{
    return PartyQuestRuntimeVerificationGate::Submit(
        aGuarded, aSession, aMonitor, std::move(aAttempt), aNowMs,
        acCompatibility.Requirement,
        [snapshot = std::move(aSnapshot)](const GameId&) mutable
            -> std::optional<QuestSnapshot> { return snapshot; },
        [facts = acCompatibility.Facts](const GameId&)
            -> std::optional<PartyQuestRuntimeCompatibilityFacts> { return facts; });
}
} // namespace

TEST_CASE("Verification evidence attempts are one-shot and cannot manufacture stability", "[quest.party-state.runtime-guard][verification-envelope][correlation]")
{
    constexpr uint64_t transactionId = 29901;
    PartyQuestRuntimeProcessOwnerTestScope owner(kVerificationBudgetCampaign,
        kVerificationBudgetPlayer, "tp_party_quest_verification_correlation_29901");
    auto& guarded = owner.GuardedSession();
    auto& session = owner.RuntimeSession();
    const auto compatibility = BuildCompatibility(GameId(96, 0x2900));
    const auto request = BuildVerificationBudgetRequest(
        transactionId, 0x2900, compatibility);
    PartyQuestRuntimeVerificationMonitor monitor;
    AdvanceToBoundedVerification(guarded, session, request, monitor);

    auto begin = PartyQuestRuntimeVerificationGate::BeginAttempt(
        guarded, session, monitor, transactionId);
    REQUIRE(begin.Status == PartyQuestRuntimeVerificationEvidenceStatus::Accepted);
    REQUIRE(begin.Attempt.has_value());
    auto attempt = std::move(*begin.Attempt);
    const auto first = SubmitAttempt(guarded, session, monitor, std::move(attempt),
        130, request.CanonicalSnapshot, compatibility);
    REQUIRE(first.EvidenceStatus == PartyQuestRuntimeVerificationEvidenceStatus::Accepted);
    REQUIRE(first.Verification == PartyQuestRuntimeVerificationStatus::NeedsStableSample);

    const auto duplicate = SubmitAttempt(guarded, session, monitor, std::move(attempt),
        131, request.CanonicalSnapshot, compatibility);
    REQUIRE(duplicate.EvidenceStatus == PartyQuestRuntimeVerificationEvidenceStatus::Duplicate);
    REQUIRE(monitor.GetStatus() == PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(session.GetCoordinator().GetActive()->StableCanonicalSamples == 1);
}

TEST_CASE("Out-of-order verification evidence is stale while independent samples reach success", "[quest.party-state.runtime-guard][verification-envelope][correlation]")
{
    constexpr uint64_t transactionId = 29902;
    PartyQuestRuntimeProcessOwnerTestScope owner(kVerificationBudgetCampaign,
        kVerificationBudgetPlayer, "tp_party_quest_verification_correlation_29902");
    auto& guarded = owner.GuardedSession();
    auto& session = owner.RuntimeSession();
    const auto compatibility = BuildCompatibility(GameId(96, 0x2901));
    const auto request = BuildVerificationBudgetRequest(
        transactionId, 0x2901, compatibility);
    PartyQuestRuntimeVerificationMonitor monitor;
    AdvanceToBoundedVerification(guarded, session, request, monitor);

    auto earlier = PartyQuestRuntimeVerificationGate::BeginAttempt(
        guarded, session, monitor, transactionId);
    auto later = PartyQuestRuntimeVerificationGate::BeginAttempt(
        guarded, session, monitor, transactionId);
    REQUIRE(earlier.Attempt.has_value());
    REQUIRE(later.Attempt.has_value());
    REQUIRE(SubmitAttempt(guarded, session, monitor, std::move(*later.Attempt),
                130, request.CanonicalSnapshot, compatibility).EvidenceStatus ==
        PartyQuestRuntimeVerificationEvidenceStatus::Accepted);
    REQUIRE(SubmitAttempt(guarded, session, monitor, std::move(*earlier.Attempt),
                131, request.CanonicalSnapshot, compatibility).EvidenceStatus ==
        PartyQuestRuntimeVerificationEvidenceStatus::Stale);
    REQUIRE(SubmitSample(guarded, session, monitor, transactionId, 140,
                request.CanonicalSnapshot, compatibility).EvidenceStatus ==
        PartyQuestRuntimeVerificationEvidenceStatus::VerifiedSuccess);
}

TEST_CASE("Unavailable or partial verification observers remain unknown and leave state unchanged", "[quest.party-state.runtime-guard][verification-envelope][observer]")
{
    constexpr uint64_t transactionId = 29903;
    PartyQuestRuntimeProcessOwnerTestScope owner(kVerificationBudgetCampaign,
        kVerificationBudgetPlayer, "tp_party_quest_verification_correlation_29903");
    auto& guarded = owner.GuardedSession();
    auto& session = owner.RuntimeSession();
    const auto compatibility = BuildCompatibility(GameId(96, 0x2902));
    const auto request = BuildVerificationBudgetRequest(
        transactionId, 0x2902, compatibility);
    PartyQuestRuntimeVerificationMonitor monitor;
    AdvanceToBoundedVerification(guarded, session, request, monitor);

    auto begin = PartyQuestRuntimeVerificationGate::BeginAttempt(
        guarded, session, monitor, transactionId);
    REQUIRE(begin.Attempt.has_value());
    const auto unknown = PartyQuestRuntimeVerificationGate::Submit(
        guarded, session, monitor, std::move(*begin.Attempt), 130,
        compatibility.Requirement,
        [snapshot = request.CanonicalSnapshot](const GameId&)
            -> std::optional<QuestSnapshot> { return snapshot; },
        {});
    REQUIRE(unknown.EvidenceStatus ==
        PartyQuestRuntimeVerificationEvidenceStatus::ObserverUnavailable);
    REQUIRE(unknown.Verification == PartyQuestRuntimeVerificationStatus::InvalidState);
    REQUIRE(monitor.GetDivergentSamples() == 0);
    REQUIRE(monitor.GetStatus() == PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(session.GetCoordinator().GetActive()->StableCanonicalSamples == 0);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
}

TEST_CASE("Repeated verification divergence exhausts the process budget and requires exact recovery", "[quest.party-state.runtime-guard][verification-budget]")
{
    constexpr uint64_t transactionId = 30001;
    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        "tp_party_quest_verification_budget_30001");
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const GameId questId(96, 0x3000);
    const auto compatibility = BuildCompatibility(questId);
    const auto request = BuildVerificationBudgetRequest(
        transactionId,
        questId.BaseId,
        compatibility);
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
        const auto sample = SubmitSample(
            guarded,
            session,
            verificationMonitor,
            transactionId,
            120 + index * 10,
            divergent,
            compatibility);
        REQUIRE(sample.Status == PartyQuestRuntimeGuardStatus::Ready);
        REQUIRE(sample.Verification == PartyQuestRuntimeVerificationStatus::Diverged);
        REQUIRE(sample.MonitorStatus ==
            PartyQuestRuntimeVerificationMonitorStatus::Waiting);
        REQUIRE(sample.GuardHeld);
        REQUIRE(verificationMonitor.GetDivergentSamples() == index);
    }

    const auto exhausted = SubmitSample(
        guarded,
        session,
        verificationMonitor,
        transactionId,
        160,
        divergent,
        compatibility);
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

    const auto otherCompatibility = BuildCompatibility(GameId(96, 0x3001));
    const auto other = BuildVerificationBudgetRequest(
        30002,
        0x3001,
        otherCompatibility);
    const auto blocked = guarded.Begin(other);
    REQUIRE(blocked.Status == PartyQuestRuntimeGuardStatus::GuardBusy);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}

TEST_CASE("Verification deadline starts at Verifying transition and cannot wait forever for the first resnapshot", "[quest.party-state.runtime-guard][verification-budget]")
{
    constexpr uint64_t transactionId = 30101;
    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        "tp_party_quest_verification_budget_30101");
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const GameId questId(96, 0x3100);
    const auto compatibility = BuildCompatibility(questId);
    const auto request = BuildVerificationBudgetRequest(
        transactionId,
        questId.BaseId,
        compatibility);
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

TEST_CASE("Process verification rejects snapshot-only bypass and stale compatibility", "[quest.party-state.runtime-guard][verification-envelope]")
{
    constexpr uint64_t transactionId = 30201;
    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        "tp_party_quest_verification_budget_30201");
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const GameId questId(96, 0x3200);
    const auto compatibility = BuildCompatibility(questId);
    const auto request = BuildVerificationBudgetRequest(
        transactionId,
        questId.BaseId,
        compatibility);
    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    AdvanceToBoundedVerification(guarded, session, request, verificationMonitor);

    // Direct process path has no fresh compatibility capability and therefore
    // fails closed into exact recovery after the mutation barrier.
    const auto bypass = guarded.SubmitVerificationResnapshot(
        verificationMonitor,
        transactionId,
        130,
        request.CanonicalSnapshot);
    REQUIRE(bypass.Status == PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(bypass.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}

TEST_CASE("Stable verification cannot commit after runtime generation changes", "[quest.party-state.runtime-guard][verification-envelope][lifecycle]")
{
    constexpr uint64_t transactionId = 30301;
    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kVerificationBudgetCampaign,
        kVerificationBudgetPlayer,
        "tp_party_quest_verification_budget_30301");
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    REQUIRE_FALSE(processGuard.IsActive());

    const GameId questId(96, 0x3300);
    const auto compatibility = BuildCompatibility(questId);
    const auto request = BuildVerificationBudgetRequest(
        transactionId,
        questId.BaseId,
        compatibility);
    PartyQuestRuntimeVerificationMonitor verificationMonitor;
    AdvanceToBoundedVerification(guarded, session, request, verificationMonitor);

    REQUIRE(SubmitSample(
                guarded,
                session,
                verificationMonitor,
                transactionId,
                130,
                request.CanonicalSnapshot,
                compatibility).Verification ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    const auto stable = SubmitSample(
        guarded,
        session,
        verificationMonitor,
        transactionId,
        140,
        request.CanonicalSnapshot,
        compatibility);
    REQUIRE(stable.Verification == PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(stable.MonitorStatus == PartyQuestRuntimeVerificationMonitorStatus::Stable);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t before = fence.GetGeneration();
    REQUIRE(fence.Invalidate() != before);

    const auto commit = guarded.Commit(
        verificationMonitor,
        transactionId,
        150);
    REQUIRE(commit.Status == PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(commit.GuardHeld);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
    REQUIRE_FALSE(session.GetCoordinator().IsCommitted(transactionId));
}
