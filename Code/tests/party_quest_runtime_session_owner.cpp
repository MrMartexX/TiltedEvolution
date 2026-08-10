#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

const std::filesystem::path& GetTPTestsExecutablePath() noexcept;

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

void WriteOwnerBytes(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(
        reinterpret_cast<const char*>(acBytes.data()),
        static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}

void WriteOwnerFile(
    const std::filesystem::path& acPath,
    const std::string& acBytes)
{
    WriteOwnerBytes(
        acPath,
        std::vector<uint8_t>(acBytes.begin(), acBytes.end()));
}

bool SetWorkspaceLeaseEnvironment(const char* apName, const std::string& acValue)
{
#ifdef _WIN32
    return _putenv_s(apName, acValue.c_str()) == 0;
#else
    return setenv(apName, acValue.c_str(), 1) == 0;
#endif
}

void ClearWorkspaceLeaseEnvironment(const char* apName)
{
#ifdef _WIN32
    _putenv_s(apName, "");
#else
    unsetenv(apName);
#endif
}

bool WaitForWorkspaceLeaseMarker(
    const std::filesystem::path& acMarker,
    std::chrono::steady_clock::duration aTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    do
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(acMarker, ec) && !ec)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

struct ExpiringWorkspaceRecoveryClock
{
    size_t Calls{};
};

uint64_t ExpireWorkspaceRecovery(void* apContext) noexcept
{
    auto& clock = *static_cast<ExpiringWorkspaceRecoveryClock*>(apContext);
    ++clock.Calls;
    return clock.Calls == 1
        ? 1
        : PartyQuestReplicaWorkspaceRecovery::MaxRecoveryNanoseconds + 1;
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
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions, active.CanonicalDigest, 0x99003301);
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
    committed.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        committed.Actions, committed.CanonicalDigest, 0x99003302);
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

TEST_CASE("Runtime session owner holds an exclusive kernel-backed workspace lease", "[quest.party-state.runtime-owner][workspace-lease]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);

    PartyQuestRuntimeSessionOwner first;
    const auto firstBind = first.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(firstBind.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(firstBind.LeaseStatus ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

    PartyQuestRuntimeSessionOwner competing;
    const auto blocked = competing.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceBusy);
    REQUIRE(blocked.LeaseStatus == PartyQuestReplicaWorkspaceLeaseStatus::Busy);
    REQUIRE_FALSE(competing.IsBound());

    REQUIRE(first.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::Disconnect).CanProceed());

    const auto acquiredAfterRelease = competing.Bind(
        kOwnerCampaign,
        kOwnerPlayer,
        paths);
    REQUIRE(acquiredAfterRelease.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(acquiredAfterRelease.LeaseStatus ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    REQUIRE(competing.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::Shutdown).CanProceed());

    REQUIRE(std::filesystem::exists(
        paths.MetadataDirectory / "party_quest_workspace.lock"));
}

TEST_CASE("Workspace lease rejects a hard-linked lock file", "[quest.party-state.runtime-owner][workspace-lease][confinement]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    std::error_code ec;
    std::filesystem::create_directories(paths.MetadataDirectory, ec);
    REQUIRE_FALSE(ec);

    const auto external = sandbox.Root / "external.lock";
    std::ofstream(external).put('x');
    REQUIRE(std::filesystem::is_regular_file(external));

    const auto lockPath =
        paths.MetadataDirectory / "party_quest_workspace.lock";
    std::filesystem::create_hard_link(external, lockPath, ec);
    REQUIRE_FALSE(ec);

    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kOwnerCampaign, kOwnerPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::InvalidNamespace);
    REQUIRE_FALSE(lease.IsHeld());
}

TEST_CASE("Workspace lease subprocess crash helper", "[.][quest.party-state.runtime-owner][workspace-lease][fault-helper]")
{
    const char* rootValue = std::getenv("TP_WORKSPACE_LEASE_CRASH_ROOT");
    REQUIRE(rootValue != nullptr);

    const auto root = std::filesystem::path(rootValue);
    const auto paths = BuildOwnerPaths(root);
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kOwnerCampaign, kOwnerPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    WriteOwnerFile(root / "lease-ready", "ready");

    if (WaitForWorkspaceLeaseMarker(
            root / "lease-crash",
            std::chrono::seconds(15)))
    {
        // Deliberately bypass all C++ destructors. The operating system must
        // release the native lease when this process terminates.
        std::_Exit(97);
    }
    FAIL("Workspace lease crash helper timed out");
}

TEST_CASE("Workspace lease is released by abrupt subprocess termination", "[quest.party-state.runtime-owner][workspace-lease][process-crash]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    REQUIRE(SetWorkspaceLeaseEnvironment(
        "TP_WORKSPACE_LEASE_CRASH_ROOT",
        sandbox.Root.string()));

    const auto& executable = GetTPTestsExecutablePath();
    REQUIRE_FALSE(executable.empty());
    const std::string command =
        "\"" + executable.string() +
        "\" \"Workspace lease subprocess crash helper\" --reporter compact";
    auto child = std::async(std::launch::async, [command]
    {
        return std::system(command.c_str());
    });

    const bool ready = WaitForWorkspaceLeaseMarker(
        sandbox.Root / "lease-ready",
        std::chrono::seconds(10));
    PartyQuestReplicaWorkspaceLease competing;
    const auto whileChildAlive = ready
        ? competing.Acquire(paths, kOwnerCampaign, kOwnerPlayer)
        : PartyQuestReplicaWorkspaceLeaseStatus::IoError;
    WriteOwnerFile(sandbox.Root / "lease-crash", "crash");
    const int childExitCode = child.get();
    ClearWorkspaceLeaseEnvironment("TP_WORKSPACE_LEASE_CRASH_ROOT");

    REQUIRE(ready);
    REQUIRE(whileChildAlive == PartyQuestReplicaWorkspaceLeaseStatus::Busy);
    REQUIRE(childExitCode != 0);
    REQUIRE(competing.Acquire(paths, kOwnerCampaign, kOwnerPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    REQUIRE(std::filesystem::exists(
        paths.MetadataDirectory / "party_quest_workspace.lock"));
}

TEST_CASE("Runtime session owner quarantines only exact orphan copy temporaries", "[quest.party-state.runtime-owner][workspace-recovery]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto saveTemporary =
        paths.SavesDirectory / "Hero.ess.tpqtmp-123-0";
    const auto checkpointTemporary = paths.CheckpointsDirectory /
        "PreRepair" / "Revision_0000000000000001" / "saves" /
        "Hero.skse.tpqtmp-456-1";
    const auto malformedTemporary =
        paths.SidecarsDirectory / "provider.bin.tpqtmp-456-invalid";
    WriteOwnerFile(saveTemporary, "UNPUBLISHED_SAVE_BYTES");
    WriteOwnerFile(checkpointTemporary, "UNPUBLISHED_CHECKPOINT_BYTES");
    WriteOwnerFile(malformedTemporary, "NOT_AN_EXACT_TEMPORARY");

    auto runtimeTemporary = paths.RuntimeApplySidecar;
    runtimeTemporary += ".tmp";
    WriteOwnerBytes(
        runtimeTemporary,
        PartyQuestRuntimeApplyPersistence::Encode(BuildOwnerState()));

    PartyQuestRuntimeSessionOwner owner;
    const auto bound = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.WorkspaceRecovery.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::Quarantined);
    REQUIRE(bound.WorkspaceRecovery.QuarantinedFiles == 2);
    REQUIRE(bound.WorkspaceRecovery.QuarantineFiles == 2);
    REQUIRE(bound.WorkspaceRecovery.QuarantineBytes ==
        std::string("UNPUBLISHED_SAVE_BYTES").size() +
            std::string("UNPUBLISHED_CHECKPOINT_BYTES").size());
    REQUIRE(owner.IsBound());

    const auto quarantine =
        paths.MetadataDirectory / "orphan_copy_quarantine";
    REQUIRE_FALSE(std::filesystem::exists(saveTemporary));
    REQUIRE_FALSE(std::filesystem::exists(checkpointTemporary));
    REQUIRE(std::filesystem::exists(
        quarantine / "saves" / saveTemporary.filename()));
    REQUIRE(std::filesystem::exists(
        quarantine / checkpointTemporary.lexically_relative(
            paths.PlayerDirectory)));
    REQUIRE(std::filesystem::exists(malformedTemporary));
    REQUIRE(std::filesystem::exists(runtimeTemporary));

    REQUIRE(owner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::Shutdown).CanProceed());
}

TEST_CASE("Workspace recovery requires the exact live lease capability", "[quest.party-state.runtime-owner][workspace-recovery][capability]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto temporary =
        paths.SavesDirectory / "Hero.ess.tpqtmp-654-0";
    WriteOwnerFile(temporary, "UNOWNED_ORPHAN_BYTES");

    PartyQuestReplicaWorkspaceLease unheldLease;
    const auto rejected =
        PartyQuestReplicaWorkspaceRecovery::QuarantineOrphanCopyTemporaries(
            paths,
            kOwnerCampaign,
            kOwnerPlayer,
            unheldLease);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::InvalidLease);
    REQUIRE(std::filesystem::exists(temporary));
    REQUIRE_FALSE(std::filesystem::exists(
        paths.MetadataDirectory / "orphan_copy_quarantine"));
}

TEST_CASE("Workspace recovery deadline fails before moving orphan bytes", "[quest.party-state.runtime-owner][workspace-recovery][deadline]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto temporary =
        paths.SavesDirectory / "Hero.ess.tpqtmp-655-0";
    WriteOwnerFile(temporary, "DEADLINE_PROTECTED_BYTES");

    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(paths, kOwnerCampaign, kOwnerPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

    ExpiringWorkspaceRecoveryClock clock;
    PartyQuestReplicaWorkspaceRecoveryHooks hooks;
    hooks.MonotonicNow = &ExpireWorkspaceRecovery;
    hooks.Context = &clock;
    const auto expired =
        PartyQuestReplicaWorkspaceRecovery::QuarantineOrphanCopyTemporaries(
            paths,
            kOwnerCampaign,
            kOwnerPlayer,
            lease,
            hooks);
    REQUIRE(expired.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::DeadlineExceeded);
    REQUIRE(std::filesystem::exists(temporary));
    REQUIRE_FALSE(std::filesystem::exists(
        paths.MetadataDirectory / "orphan_copy_quarantine"));
}

TEST_CASE("Workspace recovery blocks an over-quota evidence quarantine", "[quest.party-state.runtime-owner][workspace-recovery][quota]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto quarantine =
        paths.MetadataDirectory / "orphan_copy_quarantine" / "saves";
    for (size_t i = 0;
         i <= PartyQuestReplicaWorkspaceRecovery::MaxQuarantineFiles;
         ++i)
    {
        WriteOwnerFile(
            quarantine /
                ("Hero" + std::to_string(i) + ".ess.tpqtmp-" +
                    std::to_string(i + 1) + "-0"),
            "x");
    }

    PartyQuestRuntimeSessionOwner owner;
    const auto blocked = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceRecoveryFailure);
    REQUIRE(blocked.WorkspaceRecovery.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::QuarantineQuotaExceeded);
    REQUIRE(blocked.WorkspaceRecovery.QuarantineFiles ==
        PartyQuestReplicaWorkspaceRecovery::MaxQuarantineFiles);
    REQUIRE(blocked.WorkspaceRecovery.QuarantinedFiles == 0);
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE("Workspace recovery rejects oversized quarantine evidence", "[quest.party-state.runtime-owner][workspace-recovery][quota]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto evidence = paths.MetadataDirectory /
        "orphan_copy_quarantine" / "saves" /
        "Hero.ess.tpqtmp-900-0";
    WriteOwnerFile(evidence, "x");
    std::error_code ec;
    std::filesystem::resize_file(
        evidence,
        PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes + 1,
        ec);
    REQUIRE_FALSE(ec);

    PartyQuestRuntimeSessionOwner owner;
    const auto blocked = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceRecoveryFailure);
    REQUIRE(blocked.WorkspaceRecovery.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::QuarantineQuotaExceeded);
    REQUIRE(blocked.WorkspaceRecovery.QuarantinedFiles == 0);
    REQUIRE(std::filesystem::exists(evidence));
}

TEST_CASE("Workspace recovery does not adopt unknown quarantine content", "[quest.party-state.runtime-owner][workspace-recovery][quota][confinement]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto unknown = paths.MetadataDirectory /
        "orphan_copy_quarantine" / "saves" / "operator-notes.txt";
    WriteOwnerFile(unknown, "MUST_NOT_BE_ADOPTED_AS_ORPHAN_EVIDENCE");

    PartyQuestRuntimeSessionOwner owner;
    const auto blocked = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceRecoveryFailure);
    REQUIRE(blocked.WorkspaceRecovery.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::QuarantineInvalid);
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE(std::filesystem::exists(unknown));
}

TEST_CASE("Workspace recovery fails closed on quarantine destination conflict", "[quest.party-state.runtime-owner][workspace-recovery][confinement]")
{
    OwnerSandbox sandbox;
    const auto paths = BuildOwnerPaths(sandbox.Root);
    const auto temporary =
        paths.SavesDirectory / "Hero.ess.tpqtmp-777-0";
    const auto quarantined = paths.MetadataDirectory /
        "orphan_copy_quarantine" / "saves" / temporary.filename();
    WriteOwnerFile(temporary, "CURRENT_ORPHAN_BYTES");
    WriteOwnerFile(quarantined, "PRIOR_QUARANTINE_BYTES");

    PartyQuestRuntimeSessionOwner owner;
    const auto blocked = owner.Bind(kOwnerCampaign, kOwnerPlayer, paths);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceRecoveryFailure);
    REQUIRE(blocked.WorkspaceRecovery.Status ==
        PartyQuestReplicaWorkspaceRecoveryStatus::DestinationConflict);
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE(std::filesystem::exists(temporary));
    REQUIRE(std::filesystem::exists(quarantined));

    PartyQuestReplicaWorkspaceLease retryLease;
    REQUIRE(retryLease.Acquire(paths, kOwnerCampaign, kOwnerPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
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
