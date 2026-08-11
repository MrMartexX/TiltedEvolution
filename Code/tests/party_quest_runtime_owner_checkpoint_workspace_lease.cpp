#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_pre_repair_checkpoint_test_access.h>
#include <party_quest_runtime_safety_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kOwnerCheckpointCampaign{
    0xC1C2C3C4C5C6C7C8ull,
    0xD1D2D3D4D5D6D7D8ull};
const PartyQuestPlayerProfileId kOwnerCheckpointPlayer{
    0xE1E2E3E4E5E6E7E8ull,
    0xF1F2F3F4F5F6F7F8ull};

struct OwnerCheckpointSandbox
{
    std::filesystem::path Root;

    OwnerCheckpointSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_owner_checkpoint_lease_" +
             std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~OwnerCheckpointSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildOwnerCheckpointPaths(
    const OwnerCheckpointSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kOwnerCheckpointCampaign,
        kOwnerCheckpointPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

void WriteOwnerCheckpointFile(
    const std::filesystem::path& acPath,
    const std::string& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(acBytes.data(), static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}

PartyQuestRuntimeApplyRequest BuildOwnerCheckpointRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(99, 0x3500);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 3;
    snapshot.InitiatorPlayerId = 4;
    snapshot.CompletedStages = {10, 20};
    snapshot.Objectives = {{20, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    return request;
}

PartyQuestReplicaCopyPlan BuildOwnerCheckpointPlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    const auto source = acPaths.SavesDirectory / "Hero.ess";
    WriteOwnerCheckpointFile(source, "OWNER_BOUND_CHECKPOINT_ESS");
    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        source,
        "Hero.ess");
    REQUIRE(ess.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        acPaths,
        PartyQuestCheckpointKind::PreRepair,
        aWorldRevision,
        {*ess});
    REQUIRE(plan.IsReady());
    return plan;
}
} // namespace

TEST_CASE("Runtime owner reuses its exact workspace lease for PreRepair publication", "[quest.party-state.runtime-owner][runtime-checkpoint][workspace-lease]")
{
    OwnerCheckpointSandbox sandbox;
    const auto paths = BuildOwnerCheckpointPaths(sandbox);
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());

    PartyQuestRuntimeSessionOwner owner;
    const auto bound = owner.Bind(
        kOwnerCheckpointCampaign,
        kOwnerCheckpointPlayer,
        paths);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(bound.LeaseStatus ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    REQUIRE(owner.IsBound());

    auto* guarded = owner.GetGuardedSession();
    REQUIRE(guarded != nullptr);
    const auto request = BuildOwnerCheckpointRequest(13501, 1350);
    const auto begun = guarded->Begin(request);
    REQUIRE(begun.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(begun.GuardHeld);
    REQUIRE(guarded->GetRuntimeSession().GetCoordinator().GetActive() != nullptr);
    REQUIRE(guarded->GetRuntimeSession().GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);

    const auto plan = BuildOwnerCheckpointPlan(
        paths,
        request.TargetWorldRevision);
    const auto coverage =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoverageAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            plan);
    REQUIRE(coverage.IsVerified());

    // The RuntimeSessionOwner already holds the workspace kernel lease. This
    // call must therefore use the exact session-bound publication capability;
    // a second standalone Acquire would return WorkspaceBusy.
    const auto checkpoint = guarded->EnsurePreRepairCheckpoint(
        paths,
        plan,
        coverage);
    REQUIRE(checkpoint.Status == PartyQuestRuntimeCheckpointStatus::Ready);
    REQUIRE(checkpoint.SnapshotStatus == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(checkpoint.RuntimeTransition ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded->GetRuntimeSession().GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::ReadyToApply);

    const auto aborted = guarded->AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE_FALSE(aborted.GuardHeld);

    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Disconnect);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
}
