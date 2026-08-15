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

#include <vector>

namespace
{
const PartyQuestCampaignId kDeferredConsumeCampaign{
    0x7111222233334444ull,
    0x7555666677778888ull};
const PartyQuestPlayerProfileId kDeferredConsumePlayer{
    0x8111222233334444ull,
    0x8555666677778888ull};

struct DeferredConsumeDurability
{
    PartyQuestSaveGuard* Guard{};
    bool Allow{true};
    std::vector<bool> GuardActiveDuringPersist;

    bool Persist(const PartyQuestRuntimeRecoveryState&)
    {
        GuardActiveDuringPersist.push_back(Guard && Guard->IsActive());
        return Allow;
    }
};

PartyQuestRuntimeApplySession BuildDeferredConsumeSession(
    DeferredConsumeDurability& aDurability)
{
    return PartyQuestRuntimeApplySession(
        kDeferredConsumeCampaign,
        kDeferredConsumePlayer,
        [&aDurability](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aDurability.Persist(acState);
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
}

PartyQuestRuntimeApplyRequest BuildDeferredConsumeRequest(
    uint64_t aTransactionId,
    uint64_t aQuestRevision,
    uint32_t aReferenceBaseId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(
        73,
        static_cast<uint32_t>(0x1000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = aQuestRevision;
    snapshot.ReferenceAliases = {{1, GameId(0, aReferenceBaseId), false}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 12000 + aTransactionId;
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
    REQUIRE(PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request).has_value());
    return request;
}

PartyQuestDeferredWorldRuntimeReadinessSources BuildDeferredConsumeSources()
{
    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    sources.ResolveReferenceFormId = [](const GameId& acId)
    {
        return acId.BaseId;
    };
    return sources;
}
} // namespace

TEST_CASE(
    "Production deferred world consumption rejects stale generation and publishes only under process fence",
    "[quest.party-state.deferred-world][runtime-readiness][lifecycle][runtime-guard]")
{
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE_FALSE(processFence.IsLifecycleTransitionPending());

    DeferredConsumeDurability durability{&processGuard};
    auto session = BuildDeferredConsumeSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session);
    PartyQuestDeferredWorldQueue queue;
    PartyQuestRuntimeReferenceReadiness readiness(processFence);

    const auto request = BuildDeferredConsumeRequest(33001, 7, 0x6100);
    const auto sources = BuildDeferredConsumeSources();
    const auto revisionObserver = [revision = request.CanonicalSnapshot.Revision](
        const GameId&)
    {
        return revision;
    };

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6100, true));

    const auto marked = queue.TryMarkRuntimeReady(
        request,
        request.CanonicalSnapshot.Revision,
        processFence,
        readiness,
        sources);
    REQUIRE(marked.IsReady());
    REQUIRE(queue.FindByTransaction(request.TransactionId)->Ready);

    // The old extraction and direct process-guard transition surfaces are both
    // fail-closed. Production must use ConsumeRuntimeReady.
    REQUIRE(queue.TakeRuntimeReady(
                processFence,
                readiness,
                sources,
                revisionObserver).empty());
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE(guarded.MarkWorldReady(request).Status ==
        PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);

    const uint64_t markedGeneration = marked.RuntimeGeneration;
    const uint64_t nextGeneration = processFence.Invalidate();
    REQUIRE(nextGeneration != markedGeneration);

    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(processGuard.IsActive());

    REQUIRE(readiness.Observe(0x6100, true));
    const auto remarked = queue.TryMarkRuntimeReady(
        request,
        request.CanonicalSnapshot.Revision,
        processFence,
        readiness,
        sources);
    REQUIRE(remarked.IsReady());
    REQUIRE(remarked.RuntimeGeneration == nextGeneration);

    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 1);
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE(processGuard.GetTransactionId() == request.TransactionId);

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(processGuard.IsActive());
}

TEST_CASE(
    "Deferred world consume retains queue entry when durable world-ready publication fails",
    "[quest.party-state.deferred-world][runtime-readiness][persistence][runtime-guard]")
{
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE_FALSE(processFence.IsLifecycleTransitionPending());

    DeferredConsumeDurability durability{&processGuard};
    auto session = BuildDeferredConsumeSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session);
    PartyQuestDeferredWorldQueue queue;
    PartyQuestRuntimeReferenceReadiness readiness(processFence);

    const auto request = BuildDeferredConsumeRequest(33002, 8, 0x6200);
    const auto sources = BuildDeferredConsumeSources();
    const auto revisionObserver = [revision = request.CanonicalSnapshot.Revision](
        const GameId&)
    {
        return revision;
    };

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6200, true));
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                processFence,
                readiness,
                sources).IsReady());

    durability.Allow = false;
    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 0);

    const auto* retained = queue.FindByTransaction(request.TransactionId);
    REQUIRE(retained != nullptr);
    REQUIRE_FALSE(retained->Ready);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->SaveGuardActive);
    REQUIRE_FALSE(processGuard.IsActive());

    durability.Allow = true;
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                processFence,
                readiness,
                sources).IsReady());
    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 1);
    REQUIRE(queue.FindByTransaction(request.TransactionId) == nullptr);
    REQUIRE(processGuard.GetTransactionId() == request.TransactionId);

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(processGuard.IsActive());
}