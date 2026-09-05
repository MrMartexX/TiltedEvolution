#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

class PartyQuestRuntimeOwnerTestAccess final
{
public:
    static bool PublishBoundSession(
        PartyQuestRuntimeOwner& aOwner,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acProfileId,
        const PartyQuestRuntimeSessionOwnerBindResult& acBindResult) noexcept
    {
        auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
        const uint64_t generation = fence.GetGeneration();
        auto lease = fence.TryAcquire(generation);
        return lease && lease->IsValid() &&
            aOwner.PublishRuntimeSessionBound(
                generation,
                acCampaignId,
                acProfileId,
                acBindResult);
    }

    static PartyQuestDeferredWorldEnqueueStatus EnqueueDiagnostic(
        PartyQuestRuntimeOwner& aOwner,
        PartyQuestRuntimeApplyRequest aRequest)
    {
        std::lock_guard lock(aOwner.m_mutex);
        if (!aOwner.CanAcceptLocked())
            return PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerRequired;
        return aOwner.m_deferredWorld.Enqueue(std::move(aRequest));
    }
};

namespace
{
const PartyQuestCampaignId kRuntimeOwnerCampaign{
    0xA101A102A103A104ull,
    0xA105A106A107A108ull};
const PartyQuestPlayerProfileId kRuntimeOwnerProfile{
    0xB101B102B103B104ull,
    0xB105B106B107B108ull};

struct RuntimeOwnerSandbox
{
    std::filesystem::path Root;

    RuntimeOwnerSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_aggregate_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root / "CoopCampaigns", ec);
        REQUIRE_FALSE(ec);
    }

    ~RuntimeOwnerSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestRuntimeSafetyProfile BuildDeferredAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 1;
    requirement.ResolvedRecordFingerprint = 0x11;
    requirement.WinningOverrideFingerprint = 0x22;
    requirement.ScriptFingerprint = 0x33;
    requirement.NativeAdapterFingerprint = 0x44;
    requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = requirement.AdapterMutationComponents;

    const auto decision =
        PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestRuntimeApplyRequest BuildDiagnosticDeferredRequest(
    uint64_t aTransactionId,
    uint32_t aQuestBaseId)
{
    const GameId questId(40, aQuestBaseId);
    QuestSnapshot snapshot;
    snapshot.QuestId = questId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 1;
    snapshot.ReferenceAliases = {{1, GameId(0, aQuestBaseId + 1), false}};
    snapshot.Canonicalize();

    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(questId, facts);
    REQUIRE(admission.IsAdmitted());

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 1;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        admission,
        snapshot,
        BuildDeferredAuthorization(questId));
    REQUIRE(request.Plan.DryRunOnly);
    REQUIRE(HasPartyQuestApplyAction(
        request.Plan.Actions,
        PartyQuestApplyAction::WaitForWorldTargets));
    return request;
}

void MakeRuntimeOwnerReady(
    PartyQuestRuntimeOwner& aOwner,
    const std::filesystem::path& acRoot)
{
    REQUIRE(aOwner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::Connected) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(aOwner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::PartyJoined) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    const auto paths = PartyQuestCoopSaveLayout::Build(
        acRoot / "CoopCampaigns",
        kRuntimeOwnerCampaign,
        kRuntimeOwnerProfile);
    REQUIRE(paths.has_value());

    const auto bound = aOwner.GetSessionOwner().Bind(
        kRuntimeOwnerCampaign,
        kRuntimeOwnerProfile,
        *paths);
    REQUIRE(bound.IsReadyForAdmission());
    REQUIRE(PartyQuestRuntimeOwnerTestAccess::PublishBoundSession(
        aOwner,
        kRuntimeOwnerCampaign,
        kRuntimeOwnerProfile,
        bound));
    REQUIRE(aOwner.IsAcceptingOperations());
}
} // namespace

TEST_CASE(
    "Runtime aggregate owns the typed deferred identity queue and retires it at disconnect",
    "[quest.party-state.runtime-owner][deferred][identity][lifecycle]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    REQUIRE(PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
                owner,
                BuildDiagnosticDeferredRequest(1001, 0x1000)) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(owner.GetPendingOperationCount() == 1);

    REQUIRE(owner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::Disconnected) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(owner.GetPendingOperationCount() == 0);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
}

TEST_CASE(
    "Runtime typed enqueue racing disconnect cannot survive the owner boundary",
    "[quest.party-state.runtime-owner][deferred][lifecycle][race]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::atomic_bool start{};
    PartyQuestDeferredWorldEnqueueStatus enqueueStatus{
        PartyQuestDeferredWorldEnqueueStatus::InvalidRequest};
    std::thread enqueueThread([&]
    {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        enqueueStatus = PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
            owner,
            BuildDiagnosticDeferredRequest(1002, 0x1100));
    });

    start.store(true, std::memory_order_release);
    const auto boundary = owner.ApplyClientBoundary(
        PartyQuestRuntimeOwner::ClientBoundary::Disconnected);
    enqueueThread.join();

    REQUIRE(boundary == PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE((enqueueStatus == PartyQuestDeferredWorldEnqueueStatus::Queued ||
             enqueueStatus ==
                 PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerRequired));
    REQUIRE(owner.GetPendingOperationCount() == 0);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
}

TEST_CASE(
    "Reentrant lifecycle fence failure still revokes process aggregate admission",
    "[quest.party-state.runtime-owner][lifecycle][reentrant][race]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& owner = PartyQuestRuntimeOwner::GetProcessOwner();
    MakeRuntimeOwnerReady(owner, sandbox.Root);
    REQUIRE(PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
                owner,
                BuildDiagnosticDeferredRequest(1003, 0x1200)) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto executionLease = fence.TryAcquire(generation);
    REQUIRE(executionLease.has_value());
    REQUIRE(executionLease->IsValid());

    const auto lifecycle = owner.GetSessionOwner().PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::LoadGame);
    REQUIRE_FALSE(lifecycle.CanProceed());
    REQUIRE(lifecycle.Status == PartyQuestRuntimeLifecycleFenceStatus::InvalidState);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(owner.GetPendingOperationCount() == 0);

    executionLease.reset();
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
}

TEST_CASE(
    "Runtime aggregate reconstruction cannot inherit typed deferred identities",
    "[quest.party-state.runtime-owner][deferred][lifetime]")
{
    RuntimeOwnerSandbox sandbox;
    {
        PartyQuestRuntimeOwner first;
        MakeRuntimeOwnerReady(first, sandbox.Root / "first");
        REQUIRE(PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
                    first,
                    BuildDiagnosticDeferredRequest(1004, 0x1300)) ==
            PartyQuestDeferredWorldEnqueueStatus::Queued);
        REQUIRE(first.GetPendingOperationCount() == 1);
    }

    PartyQuestRuntimeOwner second;
    MakeRuntimeOwnerReady(second, sandbox.Root / "second");
    REQUIRE(second.IsAcceptingOperations());
    REQUIRE(second.GetPendingOperationCount() == 0);
}

TEST_CASE(
    "Runtime aggregate shutdown clears typed work and permanently closes admission",
    "[quest.party-state.runtime-owner][deferred][shutdown]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    REQUIRE(PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
                owner,
                BuildDiagnosticDeferredRequest(1005, 0x1400)) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(owner.GetPendingOperationCount() == 1);

    REQUIRE(owner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::Shutdown) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);
    REQUIRE(owner.IsShutdown());
    REQUIRE(owner.GetPendingOperationCount() == 0);
    REQUIRE_FALSE(owner.IsAcceptingOperations());
    REQUIRE(PartyQuestRuntimeOwnerTestAccess::EnqueueDiagnostic(
                owner,
                BuildDiagnosticDeferredRequest(1006, 0x1500)) ==
        PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerRequired);
}

TEST_CASE(
    "Retained recovery ownership is distinct from runtime admission readiness",
    "[quest.party-state.runtime-owner][bootstrap][recovery]")
{
    PartyQuestRuntimeSessionOwnerBindResult retained;
    retained.Status = PartyQuestRuntimeSessionOwnerBindStatus::Bound;
    retained.ReconcileStatus = PartyQuestRuntimeGuardStatus::Ready;
    retained.Store.Status = PartyQuestRuntimeSessionStoreStatus::RecoveryRequired;
    retained.GuardHeld = true;

    REQUIRE(retained.IsBound());
    REQUIRE(retained.RecoveryRequired());
    REQUIRE_FALSE(retained.IsReadyForAdmission());

    retained.GuardHeld = false;
    REQUIRE(retained.IsReadyForAdmission());
    retained.Status = PartyQuestRuntimeSessionOwnerBindStatus::ReconcileBlocked;
    REQUIRE_FALSE(retained.IsReadyForAdmission());
}

TEST_CASE(
    "Runtime aggregate never touches Skyrim after lifecycle gate closes",
    "[quest.party-state.runtime-owner][lifecycle][safety]")
{
    RuntimeOwnerSandbox sandbox;
    PartyQuestRuntimeOwner owner;
    MakeRuntimeOwnerReady(owner, sandbox.Root);

    std::atomic_uint32_t skyrimAccess{};
    owner.ConfigureRuntimeAdapters(
        [&]
        {
            ++skyrimAccess;
            return true;
        },
        [](uint64_t)
        {
            return PartyQuestPapyrusRuntimeObservation{};
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++skyrimAccess;
            return true;
        });

    REQUIRE(owner.ApplyClientBoundary(
                PartyQuestRuntimeOwner::ClientBoundary::RuntimeIdentityChanged) ==
        PartyQuestRuntimeOwner::BoundaryStatus::Applied);

    REQUIRE_FALSE(owner.ObserveSkyrimRuntime());
    REQUIRE_FALSE(owner.ObservePapyrusRuntime(1).has_value());
    REQUIRE(skyrimAccess.load() == 0);
}
