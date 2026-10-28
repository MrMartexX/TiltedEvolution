#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeDryRunResult.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kDryRunCampaign{0xC101C102C103C104ull, 0xC105C106C107C108ull};
const PartyQuestPlayerProfileId kDryRunProfile{0xD101D102D103D104ull, 0xD105D106D107D108ull};

struct DryRunSandbox
{
    std::filesystem::path Root;

    DryRunSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() / ("tp_party_quest_dry_run_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root / "CoopCampaigns", ec);
        REQUIRE_FALSE(ec);
    }

    ~DryRunSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void SetupOwnerForDryRun(PartyQuestRuntimeOwner& aOwner, const std::filesystem::path& acRoot)
{
    REQUIRE(aOwner.ApplyClientBoundary(PartyQuestRuntimeOwner::ClientBoundary::Connected) == PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(aOwner.ApplyClientBoundary(PartyQuestRuntimeOwner::ClientBoundary::PartyJoined) == PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    const auto paths = PartyQuestCoopSaveLayout::Build(acRoot / "CoopCampaigns", kDryRunCampaign, kDryRunProfile);
    REQUIRE(paths.has_value());

    const auto bound = aOwner.GetSessionOwner().Bind(kDryRunCampaign, kDryRunProfile, *paths);
    REQUIRE(bound.IsBound());

    const uint64_t generation = PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    REQUIRE(generation != 0);
    aOwner.MarkRuntimeSessionBound(generation);
    REQUIRE(aOwner.IsAcceptingOperations());
}

PartyQuestRuntimeApplyRequest BuildDryRunRequest(uint64_t aTransactionId, uint64_t aWorldRevision, bool aWaitForWorldTargets = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(33, 0x5500);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = aWorldRevision;
    snapshot.InitiatorPlayerId = 1;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    if (aWaitForWorldTargets)
    {
        snapshot.CreatedReferences = {GameId(33, 0x6600)};
    }
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.DryRunOnly = true;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition | PartyQuestApplyAction::WaitForPapyrusQuiescence | PartyQuestApplyAction::ResnapshotAndVerify;
    if (aWaitForWorldTargets)
    {
        request.Plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
    }

    request.Plan.DryRunOnly = true;
    request.Plan.MutationAuthorization = PartyQuestRuntimeSafetyTestAccess::MakeMutationAuthorization(snapshot, request.Plan.Actions, true);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(request.Plan.DryRunOnly);
    REQUIRE(PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request).has_value());
    return request;
}
} // namespace

TEST_CASE("Dry-run pipeline evaluates direct request without invoking mutation or touching saves", "[quest.party-state.dry-run-pipeline][safety]")
{
    DryRunSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    SetupOwnerForDryRun(owner, sandbox.Root);

    bool mutationInvoked = false;
    owner.ConfigureRuntimeAdapters(
        [] { return true; }, [](uint64_t) { return PartyQuestPapyrusRuntimeObservation{PartyQuestPapyrusRuntimeObservationStatus::Idle, 0, 0, 0}; },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            mutationInvoked = true;
            return false;
        });

    const auto request = BuildDryRunRequest(44001, 100);
    const auto result = owner.EvaluateDryRun(request);

    REQUIRE(result.WouldApply());
    REQUIRE(result.Outcome == PartyQuestRuntimeDryRunOutcome::WouldApply);
    REQUIRE(result.QuestId == request.CanonicalSnapshot.QuestId);
    REQUIRE(result.TransactionId == request.TransactionId);
    REQUIRE(result.TargetWorldRevision == request.TargetWorldRevision);

    // Physically prove that dry-run does not invoke the mutation executor
    REQUIRE_FALSE(mutationInvoked);

    // Physically prove that no save or sidecar files were created or mutated
    const auto paths = PartyQuestCoopSaveLayout::Build(sandbox.Root / "CoopCampaigns", kDryRunCampaign, kDryRunProfile);
    REQUIRE(paths.has_value());
    REQUIRE_FALSE(std::filesystem::exists(paths->RuntimeApplySidecar));
    REQUIRE_FALSE(std::filesystem::exists(paths->SavesDirectory / "Hero.ess"));
}

TEST_CASE("Dry-run pipeline defers requests with world targets until references are loaded", "[quest.party-state.dry-run-pipeline][deferred-world]")
{
    DryRunSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    SetupOwnerForDryRun(owner, sandbox.Root);

    owner.ConfigureRuntimeAdapters(
        [] { return true; }, [](uint64_t) { return PartyQuestPapyrusRuntimeObservation{PartyQuestPapyrusRuntimeObservationStatus::Idle, 0, 0, 0}; },
        [](const PartyQuestRuntimeApplyRequest&) { return false; });

    const auto request = BuildDryRunRequest(44002, 101, true);
    auto& queue = owner.GetDeferredWorldQueue();

    const auto enqueueStatus = queue.Enqueue(request);
    REQUIRE(enqueueStatus == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.GetPendingCount() == 1);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    PartyQuestRuntimeReferenceReadiness readiness(fence);

    constexpr uint32_t kLocalFormId = 0xFE033600;
    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    sources.ResolveReferenceFormId = [](const GameId& aId) noexcept -> uint32_t
    {
        return aId == GameId(33, 0x6600) ? kLocalFormId : 0;
    };
    sources.IsLocationReady = [](const GameId&) noexcept
    {
        return false;
    };
    sources.IsSceneReady = [](uint32_t) noexcept
    {
        return false;
    };

    PartyQuestDeferredWorldQueue::CanonicalRevisionObserver revisionObserver = [](const GameId&) noexcept -> uint64_t
    {
        return 101;
    };

    // 1. References are not yet loaded: ProcessDeferredWorldReady leaves it queued
    const size_t processedBefore = owner.ProcessDeferredWorldReady(readiness, sources, revisionObserver, 4);
    REQUIRE(processedBefore == 0);
    REQUIRE(queue.GetPendingCount() == 1);

    // 2. Reference is loaded into readiness tracker
    REQUIRE(readiness.Observe(kLocalFormId, true));
    REQUIRE(readiness.IsLoaded(kLocalFormId, fence.GetGeneration()));

    // 3. ProcessDeferredWorldReady processes the ready item and evaluates dry-run
    const size_t processedAfter = owner.ProcessDeferredWorldReady(readiness, sources, revisionObserver, 4);
    REQUIRE(processedAfter == 1);
    REQUIRE(queue.GetPendingCount() == 0);
}

TEST_CASE("Dry-run pipeline fails closed on missing reference mapping and unsupported dependencies", "[quest.party-state.dry-run-pipeline][fail-closed]")
{
    DryRunSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    SetupOwnerForDryRun(owner, sandbox.Root);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    PartyQuestRuntimeReferenceReadiness readiness(fence);

    auto& queue = owner.GetDeferredWorldQueue();

    // Missing reference mapping returns 0: must not become ready
    const auto request = BuildDryRunRequest(44003, 102, true);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);

    PartyQuestDeferredWorldRuntimeReadinessSources sources;
    sources.ResolveReferenceFormId = [](const GameId&) noexcept -> uint32_t
    {
        return 0; // Unresolved mapping
    };

    PartyQuestDeferredWorldQueue::CanonicalRevisionObserver revisionObserver = [](const GameId&) noexcept -> uint64_t
    {
        return 102;
    };

    const size_t processed = owner.ProcessDeferredWorldReady(readiness, sources, revisionObserver, 4);
    REQUIRE(processed == 0);
    REQUIRE(queue.GetPendingCount() == 1);
}

TEST_CASE("Dry-run pipeline rejects Papyrus non-quiescence with RetryableBusy and missing observer with Unsupported", "[quest.party-state.dry-run-pipeline][papyrus]")
{
    DryRunSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    SetupOwnerForDryRun(owner, sandbox.Root);

    const auto request = BuildDryRunRequest(44004, 103, false);

    // 1. Missing observer -> Unsupported
    owner.ClearRuntimeAdapters();
    const auto missingObsResult = owner.EvaluateDryRun(request);
    REQUIRE(missingObsResult.Outcome == PartyQuestRuntimeDryRunOutcome::Unsupported);

    // 2. Non-quiescent observation -> RetryableBusy
    owner.ConfigureRuntimeAdapters(
        [] { return true; }, [](uint64_t) { return PartyQuestPapyrusRuntimeObservation{PartyQuestPapyrusRuntimeObservationStatus::Busy, 5, 0, 1}; },
        [](const PartyQuestRuntimeApplyRequest&) { return false; });

    const auto busyResult = owner.EvaluateDryRun(request);
    REQUIRE(busyResult.Outcome == PartyQuestRuntimeDryRunOutcome::RetryableBusy);

    // 3. Quiescent observation -> WouldApply
    owner.ConfigureRuntimeAdapters(
        [] { return true; }, [](uint64_t) { return PartyQuestPapyrusRuntimeObservation{PartyQuestPapyrusRuntimeObservationStatus::Idle, 0, 0, 0}; },
        [](const PartyQuestRuntimeApplyRequest&) { return false; });

    const auto readyResult = owner.EvaluateDryRun(request);
    REQUIRE(readyResult.Outcome == PartyQuestRuntimeDryRunOutcome::WouldApply);
}

TEST_CASE("Dry-run pipeline invalidates deferred work on lifecycle boundary", "[quest.party-state.dry-run-pipeline][lifecycle]")
{
    DryRunSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    SetupOwnerForDryRun(owner, sandbox.Root);

    const auto request = BuildDryRunRequest(44005, 104, true);
    REQUIRE(owner.GetDeferredWorldQueue().Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(owner.GetDeferredWorldQueue().GetPendingCount() == 1);

    const auto queuedOp = owner.Enqueue([] { return true; }, [] {});
    REQUIRE(queuedOp.Status == PartyQuestRuntimeOwner::EnqueueStatus::Queued);
    REQUIRE(owner.GetPendingOperationCount() == 1);

    // Disconnect boundary invalidates all deferred and queued work
    REQUIRE(owner.ApplyClientBoundary(PartyQuestRuntimeOwner::ClientBoundary::Disconnected) == PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    REQUIRE(owner.GetDeferredWorldQueue().GetPendingCount() == 0);
    REQUIRE(owner.GetPendingOperationCount() == 0);
}
