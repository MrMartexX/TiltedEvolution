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
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_session_owner_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kDeferredConsumeCampaign{
    0x7111222233334444ull,
    0x7555666677778888ull};
const PartyQuestPlayerProfileId kDeferredConsumePlayer{
    0x8111222233334444ull,
    0x8555666677778888ull};

struct DeferredConsumeSandbox
{
    std::filesystem::path Root;

    DeferredConsumeSandbox()
    {
        const auto nonce =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_deferred_owner_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~DeferredConsumeSandbox()
    {
        // Keep the process singleton from contaminating later TPTests even when
        // an assertion exits the test before the explicit release below.
        auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        (void)owner.PrepareAndRelease(PartyQuestRuntimeLifecycleEvent::Shutdown);

        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildDeferredConsumePaths(
    const std::filesystem::path& acRoot)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acRoot / "CoopCampaigns",
        kDeferredConsumeCampaign,
        kDeferredConsumePlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestRuntimeSessionOwner& ResetProcessOwner()
{
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    return owner;
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

PartyQuestRuntimeGuardedSession& BindDeferredConsumeOwner(
    DeferredConsumeSandbox& aSandbox,
    PartyQuestCoopSavePaths& aPaths)
{
    auto& owner = ResetProcessOwner();
    aPaths = BuildDeferredConsumePaths(aSandbox.Root);
    PartyQuestRuntimeSessionOwnerTestAccess::AuthorizeNextDirectProcessBindForTesting();
    const auto bound = owner.Bind(
        kDeferredConsumeCampaign,
        kDeferredConsumePlayer,
        aPaths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.IsBound());
    REQUIRE(owner.IsBound());
    REQUIRE(owner.GetGuardedSession() != nullptr);
    return *owner.GetGuardedSession();
}

void ReleaseDeferredConsumeOwner()
{
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
}
} // namespace

TEST_CASE(
    "Production deferred world consumption rejects stale generation and publishes only through the process owner",
    "[quest.party-state.deferred-world][runtime-readiness][lifecycle][runtime-guard][runtime-owner]")
{
    DeferredConsumeSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    auto& guarded = BindDeferredConsumeOwner(sandbox, paths);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE_FALSE(processFence.IsLifecycleTransitionPending());

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
    REQUIRE(queue.EnqueueRuntime(request, guarded, processFence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6100, true));

    const auto marked = queue.TryMarkRuntimeReady(
        request,
        request.CanonicalSnapshot.Revision,
        guarded,
        processFence,
        readiness,
        sources);
    REQUIRE(marked.IsReady());
    REQUIRE(queue.FindByTransaction(request.TransactionId)->Ready);

    // The extraction and direct process-guard transition surfaces are both
    // fail-closed. Production must consume through the exact bound process owner.
    REQUIRE(queue.TakeRuntimeReady(
                guarded,
                processFence,
                readiness,
                sources,
                revisionObserver).empty());
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE(guarded.MarkWorldReady(request).Status ==
        PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE(guarded.GetRuntimeSession().GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);

    const uint64_t markedGeneration = marked.RuntimeGeneration;
    const uint64_t nextGeneration = processFence.Invalidate();
    REQUIRE(nextGeneration != markedGeneration);

    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) == nullptr);
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE(guarded.GetRuntimeSession().GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(processGuard.IsActive());

    // New readiness in the replacement generation cannot resurrect work that
    // was authorized by the old generation, even when all FormIDs match.
    REQUIRE(readiness.Observe(0x6100, true));
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                guarded,
                processFence,
                readiness,
                sources).Status ==
        PartyQuestDeferredWorldRuntimeReadinessStatus::InvalidRequest);

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(processGuard.IsActive());
    ReleaseDeferredConsumeOwner();
}

TEST_CASE(
    "Deferred world consume retains queue entry when owner journal publication fails",
    "[quest.party-state.deferred-world][runtime-readiness][persistence][runtime-guard][runtime-owner]")
{
    DeferredConsumeSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    auto& guarded = BindDeferredConsumeOwner(sandbox, paths);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE_FALSE(processFence.IsLifecycleTransitionPending());

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
    REQUIRE(queue.EnqueueRuntime(request, guarded, processFence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6200, true));
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                guarded,
                processFence,
                readiness,
                sources).IsReady());

    auto blockedTemporary = paths.RuntimeApplySidecar;
    blockedTemporary += ".tmp";
    std::error_code ec;
    std::filesystem::remove_all(blockedTemporary, ec);
    ec.clear();
    std::filesystem::create_directories(blockedTemporary, ec);
    REQUIRE_FALSE(ec);
    std::ofstream blocker(blockedTemporary / "blocker", std::ios::binary | std::ios::trunc);
    REQUIRE(blocker.is_open());
    blocker.put('x');
    blocker.flush();
    REQUIRE(blocker.good());
    blocker.close();

    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 0);

    const auto* retained = queue.FindByTransaction(request.TransactionId);
    REQUIRE(retained != nullptr);
    REQUIRE_FALSE(retained->Ready);
    REQUIRE(guarded.GetRuntimeSession().GetCoordinator().GetActive() != nullptr);
    REQUIRE(guarded.GetRuntimeSession().GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(guarded.GetRuntimeSession().GetCoordinator().GetActive()->SaveGuardActive);
    REQUIRE_FALSE(processGuard.IsActive());

    std::filesystem::remove_all(blockedTemporary, ec);
    REQUIRE_FALSE(ec);

    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                guarded,
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
    ReleaseDeferredConsumeOwner();
}

TEST_CASE(
    "Production deferred world consumption rejects an unbound private process-guard session before observation",
    "[quest.party-state.deferred-world][runtime-readiness][runtime-owner][fail-closed]")
{
    auto& owner = ResetProcessOwner();
    REQUIRE_FALSE(owner.IsBound());

    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    PartyQuestRuntimeApplySession session(
        kDeferredConsumeCampaign,
        kDeferredConsumePlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession guarded(session);
    PartyQuestDeferredWorldQueue queue;
    PartyQuestRuntimeReferenceReadiness readiness(processFence);

    const auto request = BuildDeferredConsumeRequest(33003, 9, 0x6300);
    const auto sources = BuildDeferredConsumeSources();
    size_t revisionObservations{};
    const auto revisionObserver = [
        revision = request.CanonicalSnapshot.Revision,
        &revisionObservations](const GameId&)
    {
        ++revisionObservations;
        return revision;
    };

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.EnqueueRuntime(request, guarded, processFence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6300, true));
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                guarded,
                processFence,
                readiness,
                sources).IsReady());

    REQUIRE(queue.ConsumeRuntimeReady(
                guarded,
                readiness,
                sources,
                revisionObserver) == 0);
    REQUIRE(revisionObservations == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE(queue.FindByTransaction(request.TransactionId)->Ready);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(processGuard.IsActive());

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(processGuard.IsActive());
}

TEST_CASE(
    "Production deferred world consumption rejects a different process-guard session while owner is bound",
    "[quest.party-state.deferred-world][runtime-readiness][runtime-owner][process-domain]")
{
    DeferredConsumeSandbox sandbox;
    PartyQuestCoopSavePaths paths;
    auto& ownerGuarded = BindDeferredConsumeOwner(sandbox, paths);
    REQUIRE(PartyQuestRuntimeSessionOwner::GetProcessOwner().GetGuardedSession() ==
        &ownerGuarded);

    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    PartyQuestRuntimeApplySession privateSession(
        kDeferredConsumeCampaign,
        kDeferredConsumePlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession privateGuarded(privateSession);
    REQUIRE(&privateGuarded != &ownerGuarded);

    PartyQuestDeferredWorldQueue queue;
    PartyQuestRuntimeReferenceReadiness readiness(processFence);
    const auto request = BuildDeferredConsumeRequest(33004, 10, 0x6400);
    const auto sources = BuildDeferredConsumeSources();
    size_t revisionObservations{};
    const auto revisionObserver = [
        revision = request.CanonicalSnapshot.Revision,
        &revisionObservations](const GameId&)
    {
        ++revisionObservations;
        return revision;
    };

    REQUIRE(privateGuarded.Begin(request).Status ==
        PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(queue.EnqueueRuntime(request, privateGuarded, processFence) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(readiness.Observe(0x6400, true));
    REQUIRE(queue.TryMarkRuntimeReady(
                request,
                request.CanonicalSnapshot.Revision,
                privateGuarded,
                processFence,
                readiness,
                sources).IsReady());

    REQUIRE(queue.ConsumeRuntimeReady(
                privateGuarded,
                readiness,
                sources,
                revisionObserver) == 0);
    REQUIRE(revisionObservations == 0);
    REQUIRE(queue.FindByTransaction(request.TransactionId) != nullptr);
    REQUIRE(queue.FindByTransaction(request.TransactionId)->Ready);
    REQUIRE(privateSession.GetCoordinator().GetActive() != nullptr);
    REQUIRE(privateSession.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(processGuard.IsActive());

    const auto aborted = privateGuarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(processGuard.IsActive());
    ReleaseDeferredConsumeOwner();
}
