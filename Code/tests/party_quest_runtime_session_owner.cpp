#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <type_traits>

namespace
{
const PartyQuestCampaignId kOwnerCampaign{
    0x5152535455565758ull,
    0x6162636465666768ull};
const PartyQuestPlayerProfileId kOwnerPlayer{
    0x7172737475767778ull,
    0x8182838485868788ull};

struct OwnerSandbox
{
    std::filesystem::path Root;

    OwnerSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_owner_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~OwnerSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildOwnerPaths(
    const std::filesystem::path& acRoot,
    const PartyQuestCampaignId& acCampaign = kOwnerCampaign,
    const PartyQuestPlayerProfileId& acPlayer = kOwnerPlayer)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acRoot / "CoopCampaigns",
        acCampaign,
        acPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestRuntimeRecoveryState BuildOwnerState()
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kOwnerCampaign;
    state.PlayerProfileId = kOwnerPlayer;
    return state;
}

PartyQuestRuntimeApplyEntry BuildOwnerRecoveryEntry(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(99, 0x3300);
    active.CanonicalDigest = 0x1234ABCDEF987654ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;
    return active;
}

PartyQuestRuntimeApplyRequest BuildOwnerRequest(uint64_t aTransactionId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(99, 0x3400);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 3;
    snapshot.InitiatorPlayerId = 4;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 3400;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}
} // namespace

TEST_CASE("Runtime session owner binds only after exact store hydration", "[quest.party-state.runtime-owner]")
{
    static_assert(!std::is_copy_constructible_v<PartyQuestRuntimeSessionOwner>);
    static_assert(!std::is_move_constructible_v<PartyQuestRuntimeSessionOwner>);

    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());

    PartyQuestRuntimeSessionOwner owner;
    REQUIRE_FALSE(owner.IsBound());

    const auto bound = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.Store.Status == PartyQuestRuntimeSessionStoreStatus::NewSession);
    REQUIRE(bound.IsBound());
    REQUIRE_FALSE(bound.RecoveryRequired());
    REQUIRE_FALSE(bound.GuardHeld);
    REQUIRE(owner.IsBound());
    REQUIRE(owner.GetRuntimeSession() != nullptr);
    REQUIRE(owner.GetGuardedSession() != nullptr);
    REQUIRE(owner.GetPaths() != nullptr);
    REQUIRE(*owner.GetPaths() == paths);
    REQUIRE_FALSE(std::filesystem::exists(paths.RuntimeApplySidecar));

    const auto duplicate = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(duplicate.Status == PartyQuestRuntimeSessionOwnerBindStatus::AlreadyBound);
    REQUIRE(duplicate.IsBound());
    REQUIRE(owner.IsBound());

    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Disconnect);
    REQUIRE(released.Status == PartyQuestRuntimeLifecycleFenceStatus::Allowed);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE("Runtime session owner preserves committed transaction idempotency across bind", "[quest.party-state.runtime-owner][durability]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    auto state = BuildOwnerState();

    PartyQuestRuntimeCommittedRecord committed;
    committed.TransactionId = 33001;
    committed.TargetWorldRevision = 3300;
    committed.QuestId = GameId(99, 0x3301);
    committed.CanonicalDigest = 0xABCDEF1234567890ull;
    committed.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    committed.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    state.Committed.push_back(committed);

    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    PartyQuestRuntimeSessionOwner owner;
    const auto bound = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.Store.Status == PartyQuestRuntimeSessionStoreStatus::Clean);
    REQUIRE(owner.GetRuntimeSession() != nullptr);
    REQUIRE(owner.GetRuntimeSession()->GetCoordinator().IsCommitted(
        committed.TransactionId));

    REQUIRE(owner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::Shutdown).CanProceed());
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE("Runtime session owner refuses invalid layout and mismatched durable identity", "[quest.party-state.runtime-owner][identity]")
{
    OwnerSandbox sandbox;

    SECTION("supplied path bundle must match exact campaign and player")
    {
        auto paths = BuildOwnerPaths(sandbox.Root);
        paths.RuntimeApplySidecar += ".wrong";

        PartyQuestRuntimeSessionOwner owner;
        const auto result = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
        REQUIRE(result.Status == PartyQuestRuntimeSessionOwnerBindStatus::InvalidLayout);
        REQUIRE_FALSE(result.IsBound());
        REQUIRE_FALSE(owner.IsBound());
    }

    SECTION("journal from another player is not accepted")
    {
        const auto paths = BuildOwnerPaths(sandbox.Root);
        auto state = BuildOwnerState();
        state.PlayerProfileId.Low ^= 1;
        REQUIRE(state.PlayerProfileId.IsValid());
        REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                    paths.RuntimeApplySidecar,
                    state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

        PartyQuestRuntimeSessionOwner owner;
        const auto result = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
        REQUIRE(result.Status == PartyQuestRuntimeSessionOwnerBindStatus::StoreRejected);
        REQUIRE(result.Store.Status ==
            PartyQuestRuntimeSessionStoreStatus::JournalIdentityMismatch);
        REQUIRE_FALSE(owner.IsBound());
        REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    }
}

TEST_CASE("Runtime session owner reconstructs process guard for durable recovery", "[quest.party-state.runtime-owner][recovery]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    constexpr uint64_t transactionId = 33002;
    auto state = BuildOwnerState();
    state.Active = BuildOwnerRecoveryEntry(transactionId, 3310);
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    {
        PartyQuestRuntimeSessionOwner owner;
        const auto bound = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
        REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
        REQUIRE(bound.Store.Status ==
            PartyQuestRuntimeSessionStoreStatus::RecoveryRequired);
        REQUIRE(bound.RecoveryRequired());
        REQUIRE(bound.GuardHeld);
        REQUIRE(owner.IsBound());
        REQUIRE(owner.IsRecoveryBlocked());
        REQUIRE(processGuard.GetTransactionId() == transactionId);

        const auto blocked = owner.PrepareAndRelease(
            PartyQuestRuntimeLifecycleEvent::Shutdown);
        REQUIRE(blocked.Status ==
            PartyQuestRuntimeLifecycleFenceStatus::RecoveryBlocked);
        REQUIRE_FALSE(blocked.CanProceed());
        REQUIRE(blocked.GuardHeld);
        REQUIRE(owner.IsBound());
        REQUIRE(owner.IsRecoveryBlocked());
    }

    // Test cleanup only: production must resolve the exact checkpoint instead of
    // releasing this guard directly.
    REQUIRE(processGuard.Release(transactionId));
}

TEST_CASE("Runtime session owner forbids silent identity or root rebinding", "[quest.party-state.runtime-owner][identity]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root / "first");
    const auto otherRoot = BuildOwnerPaths(sandbox.Root / "second");

    PartyQuestRuntimeSessionOwner owner;
    REQUIRE(owner.Bind(kOwnerCampaign, kOwnerPlayer, paths).Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::Bound);

    const auto conflict = owner.Bind(kOwnerCampaign, kOwnerPlayer, otherRoot);
    REQUIRE(conflict.Status == PartyQuestRuntimeSessionOwnerBindStatus::BindConflict);
    REQUIRE(conflict.IsBound() == false);
    REQUIRE(owner.IsBound());
    REQUIRE(owner.GetPaths() != nullptr);
    REQUIRE(*owner.GetPaths() == paths);

    REQUIRE(owner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::CampaignSwitch).CanProceed());
    REQUIRE_FALSE(owner.IsBound());

    const auto rebound = owner.Bind(kOwnerCampaign, kOwnerPlayer, otherRoot);
    REQUIRE(rebound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(owner.GetPaths() != nullptr);
    REQUIRE(*owner.GetPaths() == otherRoot);
    REQUIRE(owner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::Shutdown).CanProceed());
}

TEST_CASE("Runtime session owner releases pre-mutation work only through lifecycle fence", "[quest.party-state.runtime-owner][lifecycle]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());

    PartyQuestRuntimeSessionOwner owner;
    REQUIRE(owner.Bind(kOwnerCampaign, kOwnerPlayer, paths).Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    auto* guarded = owner.GetGuardedSession();
    REQUIRE(guarded != nullptr);

    const auto request = BuildOwnerRequest(33003);
    REQUIRE(guarded->Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(processGuard.GetTransactionId() == request.TransactionId);
    REQUIRE(std::filesystem::exists(paths.RuntimeApplySidecar));

    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::MainMenu);
    REQUIRE(released.Status ==
        PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(released.GuardHeld);
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE_FALSE(processGuard.IsActive());

    const auto durable = PartyQuestRuntimeApplyPersistence::Load(
        paths.RuntimeApplySidecar);
    REQUIRE(durable.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(durable.State.has_value());
    REQUIRE_FALSE(durable.State->Active.has_value());
}

TEST_CASE("Orphan process guard prevents a new runtime owner from binding", "[quest.party-state.runtime-owner][save-guard]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE(processGuard.Acquire(33999) == PartyQuestSaveGuardAcquireStatus::Acquired);

    PartyQuestRuntimeSessionOwner owner;
    const auto result = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(result.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::ProcessGuardBusy);
    REQUIRE_FALSE(result.IsBound());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE(result.GuardHeld);
    REQUIRE(processGuard.GetTransactionId() == 33999);
    REQUIRE(processGuard.Release(33999));
}
