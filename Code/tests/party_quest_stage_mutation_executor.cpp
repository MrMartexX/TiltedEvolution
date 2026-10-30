#include <Structs/Skyrim/PartyQuestStageMutationAdapter.h>

#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReviewedQuestProfiles.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <party_quest_runtime_apply_session_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <optional>
#include <vector>

class PartyQuestRuntimeGenerationFenceTestAccess final
{
public:
    static void Poison(const PartyQuestRuntimeGenerationFence& acFence) noexcept { acFence.Poison(); }
};

namespace
{
const PartyQuestCampaignId kTestCampaign{0x1111222233334444ull, 0x5555666677778888ull};
const PartyQuestPlayerProfileId kTestPlayer{0xAAAABBBBCCCCDDDDull, 0xEEEEFFFF00001111ull};

PartyQuestRuntimeApplyRequest BuildNorthwatchRequest(uint64_t aTransactionId, uint16_t aTargetStage = 100)
{
    const GameId questId(PartyQuestReviewedQuestProfiles::NorthwatchModId, PartyQuestReviewedQuestProfiles::NorthwatchBaseId);

    QuestSnapshot snapshot;
    snapshot.QuestId = questId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aTargetStage;
    snapshot.Revision = 1;
    snapshot.InitiatorPlayerId = 1;
    snapshot.CompletedStages = {0, aTargetStage};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 100 + aTransactionId;
    request.SidecarManifestFingerprint = 0xAA11BB22CC33DD44ull;
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition | PartyQuestApplyAction::WaitForPapyrusQuiescence | PartyQuestApplyAction::ResnapshotAndVerify;

    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);

    return request;
}

PartyQuestRuntimeApplyRequest BuildKarthspireRequest(uint64_t aTransactionId, uint16_t aTargetStage = 100)
{
    const GameId questId(PartyQuestReviewedQuestProfiles::KarthspireModId, PartyQuestReviewedQuestProfiles::KarthspireBaseId);

    QuestSnapshot snapshot;
    snapshot.QuestId = questId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aTargetStage;
    snapshot.Revision = 1;
    snapshot.InitiatorPlayerId = 1;
    snapshot.CompletedStages = {10, aTargetStage};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 200 + aTransactionId;
    request.SidecarManifestFingerprint = 0xBB22CC33DD44EE55ull;
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition | PartyQuestApplyAction::WaitForPapyrusQuiescence | PartyQuestApplyAction::ResnapshotAndVerify;

    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);

    return request;
}

struct ControlledActivationScope
{
    explicit ControlledActivationScope(bool aEnabled = true)
    {
        PartyQuestStageMutationAdapter::ResetExecutedTransactionsForTest();
        PartyQuestStageMutationAdapter::SetControlledActivationEnabled(aEnabled);
    }

    ~ControlledActivationScope()
    {
        PartyQuestStageMutationAdapter::SetControlledActivationEnabled(false);
        PartyQuestStageMutationAdapter::ResetExecutedTransactionsForTest();
    }
};

TEST_CASE("Research activation cannot enable the production mutation policy", "[quest.party-state.stage-mutation][durability]")
{
    ControlledActivationScope scope(true);
    REQUIRE(PartyQuestStageMutationAdapter::IsControlledActivationEnabled());
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

struct MockEngine
{
    uint16_t LiveStage{0};
    uint32_t NativeCallCount{0};
    bool FailNativeCall{false};
    bool ThrowOnNativeCall{false};
    bool CorruptPostcondition{false};
    bool PapyrusBusy{false};
    bool UpdateThread{true};
    std::vector<uint16_t> Stages{0, 10, 100};
    std::vector<std::pair<uint64_t, PartyQuestStageMutationStatus>> RecoveryCalls;

    PartyQuestStageMutationContext MakeContext(const GameId& aQuestId)
    {
        PartyQuestStageMutationContext ctx;
        ctx.CollectSnapshot = [this, aQuestId]() -> std::optional<QuestSnapshot>
        {
            QuestSnapshot s;
            s.QuestId = aQuestId;
            s.Status = QuestSnapshotStatus::Running;
            s.CurrentStage = LiveStage;
            s.Revision = 1;
            s.Canonicalize();
            if (CorruptPostcondition && NativeCallCount > 0)
            {
                // Surface leak: scene participant present violates HasOnlyStageMutationSurface
                s.SceneParticipantPlayerId = 999;
            }
            return s;
        };

        ctx.ContainsStage = [this](uint16_t aStage) noexcept -> bool
        {
            for (uint16_t s : Stages)
            {
                if (s == aStage)
                    return true;
            }
            return false;
        };

        ctx.NativeSetStage = [this](uint16_t aStage) -> bool
        {
            ++NativeCallCount;
            if (ThrowOnNativeCall)
                throw std::runtime_error("Engine crash");
            if (FailNativeCall)
                return false;
            LiveStage = aStage;
            return true;
        };

        ctx.IsPapyrusQuiescent = [this](uint64_t) noexcept -> bool
        {
            return !PapyrusBusy;
        };

        ctx.IsCurrentUpdateThread = [this]() noexcept -> bool
        {
            return UpdateThread;
        };

        ctx.OnRecoveryRequired = [this](uint64_t aTx, PartyQuestStageMutationStatus aReason)
        {
            RecoveryCalls.emplace_back(aTx, aReason);
        };

        return ctx;
    }
};

void ArmGuardedSession(PartyQuestRuntimeGuardedSession& aGuarded, PartyQuestRuntimeApplySession& aSession, const PartyQuestRuntimeApplyRequest& acRequest)
{
    REQUIRE(aGuarded.Begin(acRequest).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::MarkCheckpointCreated(aSession, acRequest.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(PartyQuestRuntimeApplySessionTestAccess::ArmRuntimeMutation(aSession, acRequest.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
}
} // namespace

TEST_CASE("Controlled activation is off by default and guards shipping binaries", "[quest.party-state.stage-mutation][controlled-activation]")
{
    ControlledActivationScope scope(false);
    REQUIRE_FALSE(PartyQuestStageMutationAdapter::IsControlledActivationEnabled());
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());

    MockEngine engine;
    engine.LiveStage = 0;
    const auto request = BuildNorthwatchRequest(1001, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::ControlledActivationDisabled);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Exact success: authorized transition calls native SetStage and verifies postconditions", "[quest.party-state.stage-mutation][success]")
{
    ControlledActivationScope scope(true);
    REQUIRE(PartyQuestStageMutationAdapter::IsControlledActivationEnabled());

    MockEngine engine;
    engine.LiveStage = 0;
    const auto request = BuildNorthwatchRequest(2001, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    PartyQuestSaveGuard guard;
    PartyQuestRuntimeApplySession session(kTestCampaign, kTestPlayer, [](const auto&) { return true; }, PartyQuestPersistenceGuarantee::PowerLossDurable);
    PartyQuestRuntimeGuardedSession guarded(session, guard);
    PartyQuestRuntimeGenerationFence fence;
    ArmGuardedSession(guarded, session, request);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx, guarded, fence);

    REQUIRE(result.Status == PartyQuestStageMutationStatus::Applied);
    REQUIRE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);
    REQUIRE(engine.LiveStage == 100);
    REQUIRE(engine.RecoveryCalls.empty());
    REQUIRE(PartyQuestStageMutationAdapter::IsTransactionExecuted(2001));
}

TEST_CASE("Equal stage target is an idempotent no-op without engine call", "[quest.party-state.stage-mutation][idempotent]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 100; // already at target
    const auto request = BuildNorthwatchRequest(2002, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::IdempotentNoOp);
    REQUIRE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
    REQUIRE(engine.LiveStage == 100);
}

TEST_CASE("Second candidate dunKarthspireRedoubtQST succeeds across valid edge 10->100", "[quest.party-state.stage-mutation][reviewed-edge]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 10;
    const auto request = BuildKarthspireRequest(2003, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::Applied);
    REQUIRE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);
    REQUIRE(engine.LiveStage == 100);
}

TEST_CASE("Unreviewed quest fails closed without engine invocation", "[quest.party-state.stage-mutation][unreviewed-quest]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    auto request = BuildNorthwatchRequest(2004, 100);
    const GameId unreviewedQuestId(0, 0x00012345);
    request.CanonicalSnapshot.QuestId = unreviewedQuestId;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, request.CanonicalSnapshot);

    auto ctx = engine.MakeContext(unreviewedQuestId);
    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);

    REQUIRE(result.Status == PartyQuestStageMutationStatus::UnreviewedQuest);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Unreviewed transition edge fails closed", "[quest.party-state.stage-mutation][unreviewed-edge]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 50; // Northwatch is only reviewed for 0 -> 100
    engine.Stages.push_back(50);
    const auto request = BuildNorthwatchRequest(2005, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::UnreviewedTransition);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Backward transition is rejected fail-closed", "[quest.party-state.stage-mutation][backward]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 100;
    const auto request = BuildNorthwatchRequest(2006, 0); // attempting 100 -> 0
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::BackwardTransitionRejected);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Nonexistent stage on quest fails closed", "[quest.party-state.stage-mutation][missing-stage]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.Stages = {0}; // stage 100 not in quest
    const auto request = BuildNorthwatchRequest(2007, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::StageNotFound);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Papyrus busy fails closed without engine invocation", "[quest.party-state.stage-mutation][quiescence]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.PapyrusBusy = true;
    const auto request = BuildNorthwatchRequest(2008, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::PapyrusBusy);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Non-update thread invocation fails closed", "[quest.party-state.stage-mutation][thread-affinity]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.UpdateThread = false;
    const auto request = BuildNorthwatchRequest(2009, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::InvalidThread);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Duplicate or replayed transaction cannot call engine twice", "[quest.party-state.stage-mutation][duplicate]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    const auto request = BuildNorthwatchRequest(2010, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto first = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(first.Status == PartyQuestStageMutationStatus::Applied);
    REQUIRE(first.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);

    // Replay of the same transaction
    const auto second = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(second.Status == PartyQuestStageMutationStatus::DuplicateTransactionReplayed);
    REQUIRE_FALSE(second.WasApplied());
    REQUIRE(engine.NativeCallCount == 1); // No double call!
}

TEST_CASE("Native SetStage failure triggers deterministic recovery without retry", "[quest.party-state.stage-mutation][native-failure]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.FailNativeCall = true;
    const auto request = BuildNorthwatchRequest(2011, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::NativeInvocationFailed);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);
    REQUIRE(engine.RecoveryCalls.size() == 1);
    REQUIRE(engine.RecoveryCalls[0].first == 2011);
    REQUIRE(engine.RecoveryCalls[0].second == PartyQuestStageMutationStatus::NativeInvocationFailed);
    REQUIRE_FALSE(PartyQuestStageMutationAdapter::IsTransactionExecuted(2011));
}

TEST_CASE("Native SetStage exception triggers deterministic recovery safely", "[quest.party-state.stage-mutation][native-exception]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.ThrowOnNativeCall = true;
    const auto request = BuildNorthwatchRequest(2012, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::NativeInvocationFailed);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);
    REQUIRE(engine.RecoveryCalls.size() == 1);
    REQUIRE(engine.RecoveryCalls[0].first == 2012);
}

TEST_CASE("Postcondition verification mismatch triggers deterministic recovery", "[quest.party-state.stage-mutation][postcondition-mismatch]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    engine.CorruptPostcondition = true; // adds scene participant to post snapshot
    const auto request = BuildNorthwatchRequest(2013, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx);
    REQUIRE(result.Status == PartyQuestStageMutationStatus::PostconditionMismatch);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 1);
    REQUIRE(engine.RecoveryCalls.size() == 1);
    REQUIRE(engine.RecoveryCalls[0].first == 2013);
    REQUIRE(engine.RecoveryCalls[0].second == PartyQuestStageMutationStatus::PostconditionMismatch);
}

TEST_CASE("Missing PreRepair checkpoint or unarmed durable barrier fails closed", "[quest.party-state.stage-mutation][checkpoint-guard]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    const auto request = BuildNorthwatchRequest(2014, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    PartyQuestSaveGuard guard;
    PartyQuestRuntimeApplySession session(kTestCampaign, kTestPlayer, [](const auto&) { return true; }, PartyQuestPersistenceGuarantee::PowerLossDurable);
    PartyQuestRuntimeGuardedSession guarded(session, guard);

    // Guarded session begun, but checkpoint NOT created
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx, guarded);

    REQUIRE(result.Status == PartyQuestStageMutationStatus::CheckpointNotCommitted);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Poisoned generation fence fails closed immediately", "[quest.party-state.stage-mutation][generation-fence]")
{
    ControlledActivationScope scope(true);

    MockEngine engine;
    engine.LiveStage = 0;
    const auto request = BuildNorthwatchRequest(2015, 100);
    auto ctx = engine.MakeContext(request.CanonicalSnapshot.QuestId);

    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeGenerationFenceTestAccess::Poison(fence);

    const auto result = PartyQuestStageMutationAdapter::Execute(request, ctx, std::nullopt, fence);

    REQUIRE(result.Status == PartyQuestStageMutationStatus::GenerationChanged);
    REQUIRE_FALSE(result.WasApplied());
    REQUIRE(engine.NativeCallCount == 0);
}

TEST_CASE("Competing targets N and N+1 in both arrival orders", "[quest.party-state.stage-mutation][competing-orders]")
{
    ControlledActivationScope scope(true);

    // Order 1: N=0->100 arrives first, then redundant N=100->100
    {
        MockEngine engine;
        engine.LiveStage = 0;
        const auto req1 = BuildNorthwatchRequest(3001, 100);
        auto ctx1 = engine.MakeContext(req1.CanonicalSnapshot.QuestId);

        const auto res1 = PartyQuestStageMutationAdapter::Execute(req1, ctx1);
        REQUIRE(res1.Status == PartyQuestStageMutationStatus::Applied);
        REQUIRE(engine.LiveStage == 100);
        REQUIRE(engine.NativeCallCount == 1);

        const auto req2 = BuildNorthwatchRequest(3002, 100);
        auto ctx2 = engine.MakeContext(req2.CanonicalSnapshot.QuestId);
        const auto res2 = PartyQuestStageMutationAdapter::Execute(req2, ctx2);
        REQUIRE(res2.Status == PartyQuestStageMutationStatus::IdempotentNoOp);
        REQUIRE(engine.NativeCallCount == 1); // No second call!
    }

    // Order 2: Already at 100, late candidate arrives
    {
        MockEngine engine;
        engine.LiveStage = 100;
        const auto req1 = BuildNorthwatchRequest(3003, 100);
        auto ctx1 = engine.MakeContext(req1.CanonicalSnapshot.QuestId);

        const auto res1 = PartyQuestStageMutationAdapter::Execute(req1, ctx1);
        REQUIRE(res1.Status == PartyQuestStageMutationStatus::IdempotentNoOp);
        REQUIRE(engine.NativeCallCount == 0);
    }
}
