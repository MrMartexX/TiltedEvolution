#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <catch2/catch.hpp>

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

    PartyQuestRuntimeSafetyProfile profile;
    profile.HasVerifiedNativeAdapter = true;

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 50;
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRuntimeAdmission(aQuestId),
        snapshot,
        profile);

    REQUIRE(request.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(request.Plan.DryRunOnly);
    return request;
}

void AdvanceToVerification(
    PartyQuestRuntimeApplyCoordinator& aCoordinator,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    const auto begin = aCoordinator.Begin(acRequest);
    if (begin == PartyQuestRuntimeApplyBeginStatus::Deferred)
        REQUIRE(aCoordinator.MarkWorldReady(acRequest.TransactionId));
    else
        REQUIRE(begin == PartyQuestRuntimeApplyBeginStatus::Started);

    REQUIRE(aCoordinator.MarkCheckpointCreated(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkApplyDispatched(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkPapyrusQuiescent(acRequest.TransactionId));
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
    REQUIRE(coordinator.MarkPapyrusQuiescent(request.TransactionId));

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
    REQUIRE(coordinator.MarkPapyrusQuiescent(request.TransactionId));
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

    REQUIRE(coordinator.MarkWorldReady(request.TransactionId));
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
