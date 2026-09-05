#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestDeferredWorld.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kCampaignA{0xA001, 0xA002};
const PartyQuestCampaignId kCampaignB{0xB001, 0xB002};
const PartyQuestPlayerProfileId kPlayerA{0xC001, 0xC002};
const PartyQuestPlayerProfileId kPlayerB{0xD001, 0xD002};

struct OwnedSession
{
    OwnedSession(
        PartyQuestCampaignId aCampaign = kCampaignA,
        PartyQuestPlayerProfileId aPlayer = kPlayerA)
        : Session(
              aCampaign,
              aPlayer,
              [](const PartyQuestRuntimeRecoveryState&) { return true; },
              PartyQuestPersistenceGuarantee::ProcessCrashResilient)
        , Guarded(Session, Guard)
    {
    }

    PartyQuestRuntimeApplySession Session;
    PartyQuestSaveGuard Guard;
    PartyQuestRuntimeGuardedSession Guarded;
};

PartyQuestRuntimeApplyRequest BuildOwnedDeferredRequest(
    uint64_t aTransactionId = 41001,
    uint64_t aRevision = 11,
    uint32_t aReference = 0x7100)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(81, 0x4100);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = aRevision;
    snapshot.ReferenceAliases = {{1, GameId(0, aReference), false}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 51000 + aRevision;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::AdapterManaged |
        PartyQuestApplyAction::WaitForWorldTargets |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE_FALSE(request.Plan.DryRunOnly);
    return request;
}

PartyQuestDeferredWorldRuntimeReadinessSources BuildSources()
{
    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    sources.ResolveReferenceFormId = [](const GameId& acId)
    {
        return acId.BaseId;
    };
    return sources;
}

PartyQuestDeferredWorldRuntimeReadinessResult MarkReady(
    PartyQuestDeferredWorldQueue& aQueue,
    const PartyQuestRuntimeApplyRequest& acRequest,
    const OwnedSession& acOwner,
    PartyQuestRuntimeGenerationFence& aFence,
    PartyQuestRuntimeReferenceReadiness& aReadiness)
{
    return aQueue.TryMarkRuntimeReady(
        acRequest,
        acRequest.CanonicalSnapshot.Revision,
        acOwner.Guarded,
        aFence,
        aReadiness,
        BuildSources());
}

void RequireGenerationTransitionRejects(const char*)
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession owner;
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(fence.Invalidate() != 1);
    REQUIRE(MarkReady(queue, request, owner, fence, readiness).Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged);
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) == nullptr);
    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Duplicate);
}
} // namespace

TEST_CASE("Runtime deferred enqueue requires an explicit owner", "[quest.party-state.deferred-world][authority]")
{
    PartyQuestDeferredWorldQueue queue;
    REQUIRE(queue.Enqueue(BuildOwnedDeferredRequest()) ==
        PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerRequired);
    REQUIRE(queue.GetPendingCount() == 0);
}

TEST_CASE("Disconnect generation retires pending deferred work", "[quest.party-state.deferred-world][authority][disconnect]")
{
    RequireGenerationTransitionRejects("disconnect");
}

TEST_CASE("Party leave generation retires pending deferred work", "[quest.party-state.deferred-world][authority][party-leave]")
{
    RequireGenerationTransitionRejects("party-leave");
}

TEST_CASE("LoadGame generation retires pending deferred work", "[quest.party-state.deferred-world][authority][load-game]")
{
    RequireGenerationTransitionRejects("load-game");
}

TEST_CASE("Orderly shutdown generation retires pending deferred work", "[quest.party-state.deferred-world][authority][shutdown]")
{
    RequireGenerationTransitionRejects("shutdown");
}

TEST_CASE("Campaign switch rejects matching quest and reference ABA", "[quest.party-state.deferred-world][authority][campaign]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession campaignA(kCampaignA, kPlayerA);
    OwnedSession campaignB(kCampaignB, kPlayerA);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(queue.EnqueueRuntime(request, campaignA.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(MarkReady(queue, request, campaignB, fence, readiness).Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::IdentityMismatch);
    REQUIRE(queue.GetPendingCount() == 0);
}

TEST_CASE("Reconnect cannot revive old deferred authority with reused identities", "[quest.party-state.deferred-world][authority][reconnect][aba]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession oldSession;
    OwnedSession replacementSession;
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(queue.EnqueueRuntime(request, oldSession.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(fence.Invalidate() != 1);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(MarkReady(queue, request, replacementSession, fence, readiness).Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged);
    REQUIRE(queue.GetPendingCount() == 0);
}

TEST_CASE("Duplicate enqueue readiness and completed replay execute at most once", "[quest.party-state.deferred-world][authority][duplicate][replay]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession owner;
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Duplicate);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(MarkReady(queue, request, owner, fence, readiness).IsReady());
    REQUIRE(MarkReady(queue, request, owner, fence, readiness).IsReady());

    const auto revision = [expected = request.CanonicalSnapshot.Revision](const GameId&)
    {
        return expected;
    };
    REQUIRE(queue.TakeRuntimeReady(
                owner.Guarded,
                fence,
                readiness,
                BuildSources(),
                revision).size() == 1);
    REQUIRE(queue.TakeRuntimeReady(
                owner.Guarded,
                fence,
                readiness,
                BuildSources(),
                revision).empty());
    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Duplicate);
}

TEST_CASE("Unavailable canonical observer fails closed without partial transition", "[quest.party-state.deferred-world][authority][observer][fail-closed]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession owner;
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(owner.Guarded.Begin(request).Status ==
        PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.EnqueueRuntime(request, owner.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(MarkReady(queue, request, owner, fence, readiness).IsReady());
    REQUIRE(queue.TakeRuntimeReady(
                owner.Guarded,
                fence,
                readiness,
                BuildSources(),
                {}).empty());
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE(owner.Session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
}

TEST_CASE("Ready reference cannot authorize stale player ownership", "[quest.party-state.deferred-world][authority][readiness][player]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    OwnedSession playerA(kCampaignA, kPlayerA);
    OwnedSession playerB(kCampaignA, kPlayerB);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildOwnedDeferredRequest();

    REQUIRE(playerA.Guarded.Begin(request).Status ==
        PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.EnqueueRuntime(request, playerA.Guarded, fence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x7100, true));
    REQUIRE(MarkReady(queue, request, playerB, fence, readiness).Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::IdentityMismatch);
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE(playerA.Session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
}
