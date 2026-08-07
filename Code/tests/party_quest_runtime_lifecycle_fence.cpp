#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleFence.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <array>

namespace
{
const PartyQuestCampaignId kLifecycleCampaign{
    0x1112131415161718ull,
    0x2122232425262728ull};
const PartyQuestPlayerProfileId kLifecyclePlayer{
    0x3132333435363738ull,
    0x4142434445464748ull};

constexpr std::array<PartyQuestRuntimeLifecycleEvent, 7> kLifecycleEvents{
    PartyQuestRuntimeLifecycleEvent::LoadGame,
    PartyQuestRuntimeLifecycleEvent::NewGame,
    PartyQuestRuntimeLifecycleEvent::MainMenu,
    PartyQuestRuntimeLifecycleEvent::ProfileSwitch,
    PartyQuestRuntimeLifecycleEvent::CampaignSwitch,
    PartyQuestRuntimeLifecycleEvent::Disconnect,
    PartyQuestRuntimeLifecycleEvent::Shutdown};

struct LifecycleDurability
{
    bool Allow{true};
    uint32_t Calls{};

    bool Persist(const PartyQuestRuntimeRecoveryState&)
    {
        ++Calls;
        return Allow;
    }
};

PartyQuestRuntimeApplySession BuildLifecycleSession(LifecycleDurability& aDurability)
{
    return PartyQuestRuntimeApplySession(
        kLifecycleCampaign,
        kLifecyclePlayer,
        [&aDurability](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aDurability.Persist(acState);
        });
}

PartyQuestRuntimeApplyRequest BuildLifecycleRequest(
    uint64_t aTransactionId,
    bool aDeferred = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(98, static_cast<uint32_t>(0x3200 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 4;
    snapshot.InitiatorPlayerId = 3;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    if (aDeferred)
        snapshot.ReferenceAliases = {{1, GameId(0, 0x1234), false}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 3200 + aTransactionId;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    if (aDeferred)
        request.Plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

PartyQuestRuntimeRecoveryState BuildBlockedLifecycleRecoveryState(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = 6400;
    active.QuestId = GameId(98, 0x6400);
    active.CanonicalDigest = 0xAABBCCDDEEFF0011ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions, active.CanonicalDigest, 0x64000001);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kLifecycleCampaign;
    state.PlayerProfileId = kLifecyclePlayer;
    state.Active = active;
    return state;
}
} // namespace

TEST_CASE("Lifecycle fence allows all lifecycle events when runtime is clean", "[quest.party-state.runtime-lifecycle]")
{
    LifecycleDurability durability;
    auto session = BuildLifecycleSession(durability);
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    for (const auto event : kLifecycleEvents)
    {
        const auto result = PartyQuestRuntimeLifecycleFence::Prepare(guarded, event);
        REQUIRE(result.Status == PartyQuestRuntimeLifecycleFenceStatus::Allowed);
        REQUIRE(result.CanProceed());
        REQUIRE(result.TransactionId == 0);
        REQUIRE_FALSE(result.GuardHeld);
    }
    REQUIRE(durability.Calls == 0);
}

TEST_CASE("Lifecycle fence durably aborts pre-mutation work before proceeding", "[quest.party-state.runtime-lifecycle]")
{
    SECTION("deferred world transaction")
    {
        LifecycleDurability durability;
        auto session = BuildLifecycleSession(durability);
        PartyQuestSaveGuard saveGuard;
        PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
        const auto request = BuildLifecycleRequest(32001, true);
        REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Deferred);
        REQUIRE_FALSE(saveGuard.IsActive());

        const auto result = PartyQuestRuntimeLifecycleFence::Prepare(
            guarded,
            PartyQuestRuntimeLifecycleEvent::LoadGame);
        REQUIRE(result.Status ==
            PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied);
        REQUIRE(result.CanProceed());
        REQUIRE(result.TransactionId == request.TransactionId);
        REQUIRE_FALSE(result.GuardHeld);
        REQUIRE(session.GetCoordinator().GetActive() == nullptr);
        REQUIRE_FALSE(saveGuard.IsActive());
        REQUIRE(durability.Calls == 2);
    }

    SECTION("guarded AwaitingCheckpoint transaction")
    {
        LifecycleDurability durability;
        auto session = BuildLifecycleSession(durability);
        PartyQuestSaveGuard saveGuard;
        PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
        const auto request = BuildLifecycleRequest(32002);
        REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
        REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);

        const auto result = PartyQuestRuntimeLifecycleFence::Prepare(
            guarded,
            PartyQuestRuntimeLifecycleEvent::MainMenu);
        REQUIRE(result.Status ==
            PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied);
        REQUIRE(result.CanProceed());
        REQUIRE_FALSE(result.GuardHeld);
        REQUIRE(session.GetCoordinator().GetActive() == nullptr);
        REQUIRE_FALSE(saveGuard.IsActive());
        REQUIRE(durability.Calls == 2);
    }
}

TEST_CASE("Lifecycle fence blocks when safe pre-mutation abort cannot be persisted", "[quest.party-state.runtime-lifecycle]")
{
    LifecycleDurability durability;
    auto session = BuildLifecycleSession(durability);
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildLifecycleRequest(32003);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    durability.Allow = false;
    const auto result = PartyQuestRuntimeLifecycleFence::Prepare(
        guarded,
        PartyQuestRuntimeLifecycleEvent::ProfileSwitch);
    REQUIRE(result.Status ==
        PartyQuestRuntimeLifecycleFenceStatus::PersistenceFailure);
    REQUIRE_FALSE(result.CanProceed());
    REQUIRE(result.GuardHeld);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->TransactionId == request.TransactionId);
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
}

TEST_CASE("Every lifecycle event requires exact recovery after the mutation barrier", "[quest.party-state.runtime-lifecycle][recovery]")
{
    LifecycleDurability durability;
    auto session = BuildLifecycleSession(durability);
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildLifecycleRequest(32004);

    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(request.TransactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);

    const uint32_t persistedBeforeFence = durability.Calls;
    for (const auto event : kLifecycleEvents)
    {
        const auto result = PartyQuestRuntimeLifecycleFence::Prepare(guarded, event);
        REQUIRE(result.Status ==
            PartyQuestRuntimeLifecycleFenceStatus::CheckpointRestoreRequired);
        REQUIRE_FALSE(result.CanProceed());
        REQUIRE(result.TransactionId == request.TransactionId);
        REQUIRE(result.GuardHeld);
        REQUIRE(session.GetCoordinator().GetActive() != nullptr);
        REQUIRE(session.GetCoordinator().GetActive()->TransactionId == request.TransactionId);
        REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
        REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
    }

    // Post-mutation AbortBeforeMutation deliberately does not publish an aborted
    // candidate. No lifecycle attempt can erase the exact recovery evidence.
    REQUIRE(durability.Calls == persistedBeforeFence);
}

TEST_CASE("Crash recovery barrier blocks lifecycle until exact recovery is resolved", "[quest.party-state.runtime-lifecycle][recovery]")
{
    LifecycleDurability durability;
    auto session = BuildLifecycleSession(durability);
    constexpr uint64_t transactionId = 32005;
    REQUIRE(session.RestoreRecoveryState(
                BuildBlockedLifecycleRecoveryState(transactionId)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);

    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto reconciled = guarded.ReconcileLoadedState();
    REQUIRE(reconciled.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(reconciled.GuardHeld);

    const auto result = PartyQuestRuntimeLifecycleFence::Prepare(
        guarded,
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(result.Status == PartyQuestRuntimeLifecycleFenceStatus::RecoveryBlocked);
    REQUIRE_FALSE(result.CanProceed());
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.GuardHeld);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(saveGuard.GetTransactionId() == transactionId);
}

TEST_CASE("Orphan physical guard blocks lifecycle instead of being silently released", "[quest.party-state.runtime-lifecycle]")
{
    LifecycleDurability durability;
    auto session = BuildLifecycleSession(durability);
    PartyQuestSaveGuard saveGuard;
    REQUIRE(saveGuard.Acquire(32999) == PartyQuestSaveGuardAcquireStatus::Acquired);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    const auto result = PartyQuestRuntimeLifecycleFence::Prepare(
        guarded,
        PartyQuestRuntimeLifecycleEvent::Disconnect);
    REQUIRE(result.Status == PartyQuestRuntimeLifecycleFenceStatus::GuardMismatch);
    REQUIRE_FALSE(result.CanProceed());
    REQUIRE(result.TransactionId == 32999);
    REQUIRE(result.GuardHeld);
    REQUIRE(saveGuard.GetTransactionId() == 32999);
    REQUIRE(saveGuard.Release(32999));
}
