#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

#include <utility>

namespace
{
PartyQuestAdmissionDecision BuildRuntimeAdmission(GameId aQuestId)
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(admission.IsAdmitted());
    return admission;
}

PartyQuestRuntimeSafetyProfile BuildRuntimeAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 1;
    requirement.ResolvedRecordFingerprint = 0x1010101010101010ull;
    requirement.WinningOverrideFingerprint = 0x2020202020202020ull;
    requirement.ScriptFingerprint = 0x3030303030303030ull;
    requirement.NativeAdapterFingerprint = 0x4040404040404040ull;
    requirement.AdapterMutationComponents = PartyQuestVerificationComponent::QuestSnapshot;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

QuestSnapshot BuildRuntimeSnapshot(GameId aQuestId, uint16_t aStage = 30)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = 4;
    snapshot.InitiatorPlayerId = 7;
    snapshot.CompletedStages = {10, 20, aStage};
    snapshot.Objectives = {{aStage, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestRuntimeApplyRequest BuildRuntimeRequest(
    uint64_t aTransactionId,
    GameId aQuestId,
    bool aNeedsWorldTargets = false)
{
    QuestSnapshot snapshot = BuildRuntimeSnapshot(aQuestId);
    if (aNeedsWorldTargets)
        snapshot.ReferenceAliases = {{1, GameId(0, 0x1234), false}};
    snapshot.Canonicalize();

    const PartyQuestRuntimeSafetyProfile profile = BuildRuntimeAuthorization(aQuestId);

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 50;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRuntimeAdmission(aQuestId),
        snapshot,
        profile);

    REQUIRE(request.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(request.Plan.DryRunOnly);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(request.Plan.MutationAuthorization.Matches(
        request.CanonicalSnapshot,
        request.Plan.Actions,
        request.Plan.DryRunOnly));
    return request;
}

bool MarkCoordinatorPapyrusQuiescent(
    PartyQuestRuntimeApplyCoordinator& aCoordinator,
    uint64_t aTransactionId,
    uint64_t aGeneration = 1)
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(aTransactionId));
    REQUIRE(tracker.Observe(aTransactionId, 0, aGeneration) ==
        PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(aTransactionId, 0, aGeneration) ==
        PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto authorization = tracker.Authorize();
    REQUIRE(authorization.has_value());
    return aCoordinator.MarkPapyrusQuiescent(
        tracker,
        std::move(*authorization));
}

void AdvanceToVerification(
    PartyQuestRuntimeApplyCoordinator& aCoordinator,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    const auto begin = aCoordinator.Begin(acRequest);
    if (begin == PartyQuestRuntimeApplyBeginStatus::Deferred)
        REQUIRE(aCoordinator.MarkWorldReady(acRequest));
    else
        REQUIRE(begin == PartyQuestRuntimeApplyBeginStatus::Started);

    REQUIRE(aCoordinator.MarkCheckpointCreated(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkApplyDispatched(acRequest.TransactionId));
    REQUIRE(MarkCoordinatorPapyrusQuiescent(aCoordinator, acRequest.TransactionId));
    REQUIRE(aCoordinator.GetActive() != nullptr);
    REQUIRE(aCoordinator.GetActive()->State == PartyQuestRuntimeApplyState::Verifying);
}
} // namespace

TEST_CASE("Runtime apply lifecycle refuses provisional stage-only plans", "[quest.party-state.runtime-apply]")
{
    const GameId questId(9, 0x1000);
    const QuestSnapshot snapshot = BuildRuntimeSnapshot(questId);

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = 1001;
    request.TargetWorldRevision = 10;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRuntimeAdmission(questId),
        snapshot);

    REQUIRE(request.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::StageOnly);

    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::UnsafePlan);
    REQUIRE(coordinator.GetActive() == nullptr);
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());
}

TEST_CASE("Runtime apply requires exact mutation authorization rather than public RuntimeSafe fields", "[quest.party-state.runtime-apply][mutation-authorization]")
{
    SECTION("default token cannot authorize a forged RuntimeSafe plan")
    {
        auto request = BuildRuntimeRequest(1501, GameId(9, 0x1500));
        request.Plan.MutationAuthorization = {};
        PartyQuestRuntimeApplyCoordinator coordinator;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::UnsafePlan);
        REQUIRE(coordinator.GetActive() == nullptr);
    }

    SECTION("canonical snapshot change invalidates the token")
    {
        auto request = BuildRuntimeRequest(1502, GameId(9, 0x1501));
        request.CanonicalSnapshot.CurrentStage = 40;
        request.CanonicalSnapshot.CompletedStages.push_back(40);
        request.CanonicalSnapshot.Canonicalize();
        PartyQuestRuntimeApplyCoordinator coordinator;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::UnsafePlan);
        REQUIRE(coordinator.GetActive() == nullptr);
    }

    SECTION("action expansion invalidates the token")
    {
        auto request = BuildRuntimeRequest(1503, GameId(9, 0x1502));
        request.Plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
        PartyQuestRuntimeApplyCoordinator coordinator;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::UnsafePlan);
        REQUIRE(coordinator.GetActive() == nullptr);
    }

    SECTION("dry-run disposition change invalidates the token")
    {
        auto request = BuildRuntimeRequest(1504, GameId(9, 0x1503));
        request.Plan.DryRunOnly = false;
        PartyQuestRuntimeApplyCoordinator coordinator;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::UnsafePlan);
        REQUIRE(coordinator.GetActive() == nullptr);
    }
}

TEST_CASE("Critical repair sequence requires checkpoint quiescence stable verification and commit", "[quest.party-state.runtime-apply]")
{
    const auto request = BuildRuntimeRequest(2001, GameId(9, 0x2000));
    PartyQuestRuntimeApplyCoordinator coordinator;

    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.IsSaveGuardActive());
    REQUIRE(coordinator.GetActive() != nullptr);
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::AwaitingCheckpoint);

    REQUIRE_FALSE(coordinator.MarkApplyDispatched(request.TransactionId));
    REQUIRE(coordinator.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE(coordinator.MarkApplyDispatched(request.TransactionId));
    REQUIRE(coordinator.GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::WaitingForPapyrus);

    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::InvalidState);
    REQUIRE(MarkCoordinatorPapyrusQuiescent(coordinator, request.TransactionId));

    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::Verifying);

    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::ReadyToCommit);
    REQUIRE(coordinator.IsSaveGuardActive());

    REQUIRE(coordinator.Commit(request.TransactionId));
    REQUIRE(coordinator.GetActive() == nullptr);
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());
    REQUIRE(coordinator.IsCommitted(request.TransactionId));
}

TEST_CASE("Runtime apply rejects stale or unverified quiescence capability", "[quest.party-state.runtime-apply][quiescence]")
{
    const auto request = BuildRuntimeRequest(2501, GameId(9, 0x2500));
    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(coordinator.MarkApplyDispatched(request.TransactionId));

    PartyQuestPapyrusQuiescenceTracker tracker;
    PartyQuestPapyrusQuiescenceAuthorization invalid;
    REQUIRE_FALSE(coordinator.MarkPapyrusQuiescent(tracker, std::move(invalid)));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::WaitingForPapyrus);

    REQUIRE(tracker.Begin(request.TransactionId));
    REQUIRE(tracker.Observe(request.TransactionId, 0, 70) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(request.TransactionId, 0, 70) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto stale = tracker.Authorize();
    REQUIRE(stale.has_value());

    REQUIRE(tracker.Observe(request.TransactionId, 1, 71) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE_FALSE(coordinator.MarkPapyrusQuiescent(tracker, std::move(*stale)));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::WaitingForPapyrus);

    REQUIRE(tracker.Observe(request.TransactionId, 0, 71) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(request.TransactionId, 0, 71) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto current = tracker.Authorize();
    REQUIRE(current.has_value());
    REQUIRE(coordinator.MarkPapyrusQuiescent(tracker, std::move(*current)));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::Verifying);
}

TEST_CASE("Runtime apply transaction ids are idempotent and conflict-safe", "[quest.party-state.runtime-apply]")
{
    auto request = BuildRuntimeRequest(3001, GameId(9, 0x3000));
    PartyQuestRuntimeApplyCoordinator coordinator;

    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::DuplicatePending);

    auto otherRequest = BuildRuntimeRequest(3002, GameId(9, 0x3001));
    REQUIRE(coordinator.Begin(otherRequest) == PartyQuestRuntimeApplyBeginStatus::Busy);

    REQUIRE(coordinator.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(coordinator.MarkApplyDispatched(request.TransactionId));
    REQUIRE(MarkCoordinatorPapyrusQuiescent(coordinator, request.TransactionId));
    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(coordinator.Commit(request.TransactionId));

    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted);

    auto conflict = request;
    conflict.CanonicalSnapshot.CurrentStage = 40;
    conflict.CanonicalSnapshot.CompletedStages.push_back(40);
    conflict.CanonicalSnapshot.Canonicalize();
    conflict.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRuntimeAdmission(conflict.CanonicalSnapshot.QuestId),
        conflict.CanonicalSnapshot,
        BuildRuntimeAuthorization(conflict.CanonicalSnapshot.QuestId));
    REQUIRE(conflict.Plan.MutationAuthorization.IsVerified());
    REQUIRE(coordinator.Begin(conflict) == PartyQuestRuntimeApplyBeginStatus::TransactionConflict);
}

TEST_CASE("Deferred world repair does not lock saving until its targets are ready", "[quest.party-state.runtime-apply]")
{
    const auto request = BuildRuntimeRequest(4001, GameId(9, 0x4000), true);
    REQUIRE(HasPartyQuestApplyAction(request.Plan.Actions, PartyQuestApplyAction::WaitForWorldTargets));

    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Deferred);
    REQUIRE(coordinator.GetActive() != nullptr);
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());
    REQUIRE_FALSE(coordinator.MarkCheckpointCreated(request.TransactionId));

    auto advancedCanonical = request;
    ++advancedCanonical.TargetWorldRevision;
    REQUIRE_FALSE(coordinator.MarkWorldReady(advancedCanonical));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());

    REQUIRE(coordinator.MarkWorldReady(request));
    REQUIRE(coordinator.GetActive()->State == PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE(coordinator.IsSaveGuardActive());
    REQUIRE(coordinator.MarkCheckpointCreated(request.TransactionId));
}

TEST_CASE("Verification refuses divergence until two canonical samples stabilize", "[quest.party-state.runtime-apply]")
{
    const auto request = BuildRuntimeRequest(5001, GameId(9, 0x5000));
    PartyQuestRuntimeApplyCoordinator coordinator;
    AdvanceToVerification(coordinator, request);

    QuestSnapshot divergent = request.CanonicalSnapshot;
    divergent.CurrentStage = 99;
    divergent.CompletedStages.push_back(99);
    divergent.Canonicalize();

    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, divergent) ==
        PartyQuestRuntimeVerificationStatus::Diverged);
    REQUIRE(coordinator.GetActive()->StableCanonicalSamples == 0);
    REQUIRE_FALSE(coordinator.Commit(request.TransactionId));

    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(coordinator.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(coordinator.Commit(request.TransactionId));
}

TEST_CASE("Abort only requires checkpoint restore after runtime mutation may have occurred", "[quest.party-state.runtime-apply]")
{
    const auto beforeApply = BuildRuntimeRequest(6001, GameId(9, 0x6000));
    PartyQuestRuntimeApplyCoordinator coordinator;

    REQUIRE(coordinator.Begin(beforeApply) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.MarkCheckpointCreated(beforeApply.TransactionId));
    REQUIRE(coordinator.Abort(beforeApply.TransactionId));
    REQUIRE_FALSE(coordinator.LastAbortRequiresCheckpointRestore());
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());

    const auto afterApply = BuildRuntimeRequest(6002, GameId(9, 0x6001));
    REQUIRE(coordinator.Begin(afterApply) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.MarkCheckpointCreated(afterApply.TransactionId));
    REQUIRE(coordinator.MarkApplyDispatched(afterApply.TransactionId));
    REQUIRE(coordinator.Abort(afterApply.TransactionId));
    REQUIRE(coordinator.LastAbortRequiresCheckpointRestore());
    REQUIRE_FALSE(coordinator.IsSaveGuardActive());
    REQUIRE_FALSE(coordinator.IsCommitted(afterApply.TransactionId));
}

TEST_CASE("Runtime apply validates transaction identity and canonical revision", "[quest.party-state.runtime-apply]")
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    auto request = BuildRuntimeRequest(7001, GameId(9, 0x7000));

    SECTION("zero transaction id")
    {
        request.TransactionId = 0;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::InvalidRequest);
    }

    SECTION("zero target world revision")
    {
        request.TargetWorldRevision = 0;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::InvalidRequest);
    }

    SECTION("zero canonical quest revision")
    {
        request.CanonicalSnapshot.Revision = 0;
        REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::InvalidRequest);
    }
}

TEST_CASE("Verification envelope binds exact current mutation coverage", "[quest.party-state.runtime-apply][verification-envelope]")
{
    const auto request = BuildRuntimeRequest(7101, GameId(9, 0x7100));
    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);

    const auto* active = coordinator.GetActive();
    REQUIRE(active != nullptr);
    REQUIRE(active->ExpectedVerification.SchemaVersion ==
        PartyQuestVerificationEnvelopeV1::kSchemaVersion);
    REQUIRE(active->ExpectedVerification.Required ==
        (PartyQuestVerificationComponent::QuestSnapshot |
         PartyQuestVerificationComponent::Compatibility));
    REQUIRE(active->ExpectedVerification.Required ==
        (request.Plan.MutationAuthorization.GetAdapterMutationComponents() |
         PartyQuestVerificationComponent::Compatibility));
    REQUIRE(active->ExpectedVerification.QuestSnapshotDigest == active->CanonicalDigest);
    REQUIRE(active->ExpectedVerification.CompatibilityFingerprint ==
        request.Plan.MutationAuthorization.GetCompatibilityFingerprint());
    REQUIRE(active->ExpectedVerification.ComputeFingerprint() != 0);

    auto recovery = coordinator.ExportRecoveryState(
        PartyQuestCampaignId{0x7101, 0x7102},
        PartyQuestPlayerProfileId{0x7103, 0x7104});
    REQUIRE(recovery.Active.has_value());

    SECTION("missing required coverage fails closed")
    {
        recovery.Active->ExpectedVerification.Required =
            PartyQuestVerificationComponent::QuestSnapshot;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("unexpected unverified digest fails closed")
    {
        recovery.Active->ExpectedVerification.AliasDigest = 0xBAD;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("verification schema drift fails closed")
    {
        ++recovery.Active->ExpectedVerification.SchemaVersion;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("unknown verification component fails closed")
    {
        recovery.Active->ExpectedVerification.Required =
            recovery.Active->ExpectedVerification.Required |
            static_cast<PartyQuestVerificationComponent>(1u << 31);
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("inventory effects cannot appear without an authorized observer")
    {
        recovery.Active->ExpectedVerification.InventoryEffectsDigest = 0xBAD;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("world effects cannot appear without an authorized observer")
    {
        recovery.Active->ExpectedVerification.WorldEffectsDigest = 0xBAD;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }

    SECTION("adapter state cannot appear without an authorized observer")
    {
        recovery.Active->ExpectedVerification.AdapterStateDigest = 0xBAD;
        PartyQuestRuntimeApplyCoordinator restored;
        REQUIRE(restored.RestoreRecoveryState(
                    recovery,
                    recovery.CampaignId,
                    recovery.PlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);
    }
}

TEST_CASE("Verification policy rejects unknown mutation surfaces", "[quest.party-state.runtime-apply][verification-envelope]")
{
    const auto unknown = static_cast<PartyQuestApplyAction>(1u << 31);
    REQUIRE_FALSE(PartyQuestVerificationPolicy::BuildExpected(
        unknown,
        0x1111,
        0x2222).has_value());
    REQUIRE_FALSE(PartyQuestVerificationPolicy::BuildExpected(
        PartyQuestApplyAction::StageTransition | unknown,
        0x1111,
        0x2222).has_value());
}
