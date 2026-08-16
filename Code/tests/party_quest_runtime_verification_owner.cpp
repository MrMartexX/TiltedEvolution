#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>
#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_process_owner_test_support.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kVerificationOwnerCampaign{
    0xA301A302A303A304ull,
    0xA305A306A307A308ull};
const PartyQuestPlayerProfileId kVerificationOwnerPlayer{
    0xA401A402A403A404ull,
    0xA405A406A407A408ull};

struct VerificationOwnerCompatibility
{
    PartyQuestRuntimeCompatibilityRequirement Requirement;
    PartyQuestRuntimeCompatibilityFacts Facts;
    uint64_t Fingerprint{};
};

VerificationOwnerCompatibility BuildVerificationOwnerCompatibility(
    const GameId& acQuestId)
{
    VerificationOwnerCompatibility fixture;
    fixture.Requirement.QuestId = acQuestId;
    fixture.Requirement.ProfileVersion = 31;
    fixture.Requirement.ResolvedRecordFingerprint = 0xA501A502A503A504ull;
    fixture.Requirement.WinningOverrideFingerprint = 0xA511A512A513A514ull;
    fixture.Requirement.ScriptFingerprint = 0xA521A522A523A524ull;
    fixture.Requirement.NativeAdapterFingerprint = 0xA531A532A533A534ull;
    fixture.Requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;

    fixture.Facts.ProfileVersion = fixture.Requirement.ProfileVersion;
    fixture.Facts.ResolvedRecordFingerprint =
        fixture.Requirement.ResolvedRecordFingerprint;
    fixture.Facts.WinningOverrideFingerprint =
        fixture.Requirement.WinningOverrideFingerprint;
    fixture.Facts.ScriptFingerprint = fixture.Requirement.ScriptFingerprint;
    fixture.Facts.NativeAdapterFingerprint =
        fixture.Requirement.NativeAdapterFingerprint;
    fixture.Facts.AdapterMutationComponents =
        fixture.Requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        fixture.Requirement,
        fixture.Facts);
    REQUIRE(decision.IsAuthorized());
    fixture.Fingerprint = decision.SafetyProfile.GetCompatibilityFingerprint();
    REQUIRE(fixture.Fingerprint != 0);
    return fixture;
}

PartyQuestRuntimeApplyRequest BuildVerificationOwnerRequest(
    uint64_t aTransactionId,
    const VerificationOwnerCompatibility& acCompatibility)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = acCompatibility.Requirement.QuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 60;
    snapshot.Revision = 12;
    snapshot.InitiatorPlayerId = 5;
    snapshot.CompletedStages = {10, 30, 60};
    snapshot.Objectives = {{60, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 52000 + aTransactionId;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
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

class VerificationOwnerIdleObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        return {
            PartyQuestPapyrusRuntimeObservationStatus::Idle,
            0,
            1200,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains};
    }
};

void AdvanceVerificationOwnerToVerifying(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestRuntimeApplyRequest& acRequest,
    PartyQuestRuntimeVerificationMonitor& aMonitor)
{
    const uint64_t transactionId = acRequest.TransactionId;
    REQUIRE(aGuarded.Begin(acRequest).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(
                aSession,
                transactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(aGuarded.ArmRuntimeMutation(transactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);

    VerificationOwnerIdleObserver observer;
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor papyrusMonitor(observer);
    REQUIRE(papyrusMonitor.Begin(
        transactionId,
        100,
        1000,
        observerAuthorization));
    REQUIRE(papyrusMonitor.Poll(transactionId, 110) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(papyrusMonitor.Poll(transactionId, 120) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto authorization = papyrusMonitor.Authorize();
    REQUIRE(authorization.has_value());

    const auto transition = aGuarded.MarkPapyrusQuiescent(
        papyrusMonitor,
        std::move(*authorization),
        aMonitor,
        120);
    REQUIRE(transition.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(transition.GuardHeld);
    REQUIRE(aMonitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(aSession.GetCoordinator().GetActive() != nullptr);
    REQUIRE(aSession.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
}
} // namespace

TEST_CASE(
    "Verification gate rejects split process ownership before observation or recovery",
    "[quest.party-state.runtime-guard][verification-envelope][runtime-owner]")
{
    constexpr uint64_t transactionId = 39001;
    PartyQuestRuntimeProcessOwnerTestScope processOwner(
        kVerificationOwnerCampaign,
        kVerificationOwnerPlayer,
        "tp_party_quest_verification_owner_39001");

    auto& guarded = processOwner.GuardedSession();
    auto& session = processOwner.RuntimeSession();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());

    const GameId questId(99, 0xA300);
    const auto compatibility =
        BuildVerificationOwnerCompatibility(questId);
    const auto request = BuildVerificationOwnerRequest(
        transactionId,
        compatibility);
    PartyQuestRuntimeVerificationMonitor monitor;
    AdvanceVerificationOwnerToVerifying(
        guarded,
        session,
        request,
        monitor);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    PartyQuestRuntimeApplySession unrelatedSession(
        kVerificationOwnerCampaign,
        kVerificationOwnerPlayer);
    PartyQuestSaveGuard unrelatedGuard;
    PartyQuestRuntimeGuardedSession unrelatedGuarded(
        unrelatedSession,
        unrelatedGuard);

    size_t snapshotObservations{};
    size_t compatibilityObservations{};
    const auto snapshotObserver = [
        &snapshotObservations,
        snapshot = request.CanonicalSnapshot](const GameId& acQuestId) mutable
        -> std::optional<QuestSnapshot>
    {
        ++snapshotObservations;
        if (snapshot.QuestId != acQuestId)
            return std::nullopt;
        return snapshot;
    };
    const auto compatibilityObserver = [
        &compatibilityObservations,
        facts = compatibility.Facts](const GameId&)
        -> std::optional<PartyQuestRuntimeCompatibilityFacts>
    {
        ++compatibilityObservations;
        return facts;
    };

    const auto mismatchedSession = PartyQuestRuntimeVerificationGate::Submit(
        guarded,
        unrelatedSession,
        monitor,
        transactionId,
        130,
        compatibility.Requirement,
        snapshotObserver,
        compatibilityObserver);
    REQUIRE(mismatchedSession.Status ==
        PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(mismatchedSession.Verification ==
        PartyQuestRuntimeVerificationStatus::InvalidState);
    REQUIRE(mismatchedSession.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(mismatchedSession.GuardHeld);
    REQUIRE(snapshotObservations == 0);
    REQUIRE(compatibilityObservations == 0);
    REQUIRE(unrelatedSession.GetCoordinator().GetActive() == nullptr);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(monitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(processGuard.GetTransactionId() == transactionId);

    const auto mismatchedGuarded = PartyQuestRuntimeVerificationGate::Submit(
        unrelatedGuarded,
        session,
        monitor,
        transactionId,
        140,
        compatibility.Requirement,
        snapshotObserver,
        compatibilityObserver);
    REQUIRE(mismatchedGuarded.Status ==
        PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(mismatchedGuarded.Verification ==
        PartyQuestRuntimeVerificationStatus::InvalidState);
    REQUIRE(mismatchedGuarded.MonitorStatus ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE_FALSE(mismatchedGuarded.GuardHeld);
    REQUIRE(snapshotObservations == 0);
    REQUIRE(compatibilityObservations == 0);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::Verifying);
    REQUIRE(monitor.GetStatus() ==
        PartyQuestRuntimeVerificationMonitorStatus::Waiting);
    REQUIRE(processGuard.GetTransactionId() == transactionId);
}
