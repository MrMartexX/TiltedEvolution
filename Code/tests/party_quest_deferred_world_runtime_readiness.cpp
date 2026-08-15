#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestDeferredWorld.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestRuntimeApplyRequest BuildRuntimeDeferredRequest(
    uint64_t aTransactionId,
    uint64_t aQuestRevision,
    uint32_t aReferenceBaseId,
    bool aWithLocation = false,
    bool aWithScene = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(70, static_cast<uint32_t>(0x1000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = aQuestRevision;
    snapshot.ReferenceAliases = {{1, GameId(0, aReferenceBaseId), false}};
    if (aWithLocation)
        snapshot.LocationAliases = {{2, GameId(0, aReferenceBaseId + 1)}};
    if (aWithScene)
        snapshot.SceneParticipantPlayerId = 77;
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 9000 + aTransactionId;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::AdapterManaged |
        PartyQuestApplyAction::WaitForWorldTargets |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE_FALSE(request.Plan.DryRunOnly);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request).has_value());
    return request;
}

PartyQuestDeferredWorldRuntimeReadinessSources BuildReferenceSources()
{
    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    sources.ResolveReferenceFormId = [](const GameId& acId)
    {
        return acId.BaseId;
    };
    return sources;
}
} // namespace

TEST_CASE("Runtime deferred readiness separates references from location evidence", "[quest.party-state.deferred-world][runtime-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31001, 4, 0x5100, true, false);

    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    const auto* entry = queue.FindByTransaction(request.TransactionId);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->ReferenceTargets == std::vector<GameId>{GameId(0, 0x5100)});
    REQUIRE(entry->LocationTargets == std::vector<GameId>{GameId(0, 0x5101)});
    REQUIRE(entry->ReferencedWorldTargets.size() == 2);

    // The diagnostic bypass is closed for side-effecting plans.
    REQUIRE_FALSE(queue.MarkReady(request, request.CanonicalSnapshot.Revision));
    REQUIRE(queue.TakeReady().empty());

    auto sources = BuildReferenceSources();
    REQUIRE(readiness.Observe(0x5100, true));
    auto result = queue.TryMarkRuntimeReady(
        request,
        request.CanonicalSnapshot.Revision,
        fence,
        readiness,
        sources);
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::LocationReadinessUnavailable);
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);

    sources.IsLocationReady = [](const GameId&) { return true; };
    result = queue.TryMarkRuntimeReady(
        request,
        request.CanonicalSnapshot.Revision,
        fence,
        readiness,
        sources);
    REQUIRE(result.IsReady());
    REQUIRE(queue.FindByTransaction(request.TransactionId)->ReadyGeneration ==
        fence.GetGeneration());

    const auto ready = queue.TakeRuntimeReady(
        fence,
        readiness,
        sources,
        [revision = request.CanonicalSnapshot.Revision](const GameId&)
        {
            return revision;
        });
    REQUIRE(ready.size() == 1);
    REQUIRE(ready[0].TransactionId == request.TransactionId);
}

TEST_CASE("Runtime deferred reference readiness requires exact mapping and loaded evidence", "[quest.party-state.deferred-world][runtime-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31002, 5, 0x5200);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);

    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    auto result = queue.TryMarkRuntimeReady(
        request, 5, fence, readiness, sources);
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceMappingUnavailable);

    sources.ResolveReferenceFormId = [](const GameId&) { return 0u; };
    result = queue.TryMarkRuntimeReady(request, 5, fence, readiness, sources);
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceMappingUnavailable);

    sources = BuildReferenceSources();
    result = queue.TryMarkRuntimeReady(request, 5, fence, readiness, sources);
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceNotReady);

    REQUIRE(readiness.Observe(0x5200, true));
    result = queue.TryMarkRuntimeReady(request, 5, fence, readiness, sources);
    REQUIRE(result.IsReady());
}

TEST_CASE("Runtime deferred scene dependency fails closed without its own observer", "[quest.party-state.deferred-world][runtime-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31003, 6, 0x5300, false, true);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x5300, true));

    auto sources = BuildReferenceSources();
    auto result = queue.TryMarkRuntimeReady(request, 6, fence, readiness, sources);
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::SceneReadinessUnavailable);

    sources.IsSceneReady = [](uint32_t) { return false; };
    result = queue.TryMarkRuntimeReady(request, 6, fence, readiness, sources);
    REQUIRE(result.Status == PartyQuestDeferredWorldRuntimeReadinessStatus::SceneNotReady);

    sources.IsSceneReady = [](uint32_t playerId) { return playerId == 77; };
    result = queue.TryMarkRuntimeReady(request, 6, fence, readiness, sources);
    REQUIRE(result.IsReady());
}

TEST_CASE("Runtime deferred readiness cannot mix generation domains", "[quest.party-state.deferred-world][runtime-readiness][lifecycle]")
{
    PartyQuestRuntimeGenerationFence queueFence;
    PartyQuestRuntimeGenerationFence otherFence;
    PartyQuestRuntimeReferenceReadiness readiness(otherFence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31004, 7, 0x5400);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x5400, true));

    const auto result = queue.TryMarkRuntimeReady(
        request,
        7,
        queueFence,
        readiness,
        BuildReferenceSources());
    REQUIRE(result.Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceReadinessDomainMismatch);
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);
}

TEST_CASE("Lifecycle generation change clears runtime ready state until fresh evidence", "[quest.party-state.deferred-world][runtime-readiness][lifecycle]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31005, 8, 0x5500);
    const auto sources = BuildReferenceSources();
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x5500, true));

    const auto marked = queue.TryMarkRuntimeReady(
        request, 8, fence, readiness, sources);
    REQUIRE(marked.IsReady());
    const uint64_t firstGeneration = marked.RuntimeGeneration;

    const uint64_t nextGeneration = fence.Invalidate();
    REQUIRE(nextGeneration != firstGeneration);

    REQUIRE(queue.TakeRuntimeReady(
                fence,
                readiness,
                sources,
                [](const GameId&) { return 8ull; }).empty());
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);

    REQUIRE(readiness.Observe(0x5500, true));
    const auto remarked = queue.TryMarkRuntimeReady(
        request, 8, fence, readiness, sources);
    REQUIRE(remarked.IsReady());
    REQUIRE(remarked.RuntimeGeneration == nextGeneration);

    REQUIRE(queue.TakeRuntimeReady(
                fence,
                readiness,
                sources,
                [](const GameId&) { return 8ull; }).size() == 1);
}

TEST_CASE("New canonical revision after runtime readiness supersedes before extraction", "[quest.party-state.deferred-world][runtime-readiness][revision]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildRuntimeDeferredRequest(31006, 9, 0x5600);
    const auto sources = BuildReferenceSources();
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x5600, true));
    REQUIRE(queue.TryMarkRuntimeReady(request, 9, fence, readiness, sources).IsReady());

    REQUIRE(queue.TakeRuntimeReady(
                fence,
                readiness,
                sources,
                [](const GameId&) { return 10ull; }).empty());
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) == nullptr);
}
