#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeCheckpoint.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <party_quest_pre_repair_checkpoint_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCheckpointCampaign{0x1357246813572468ull, 0x2468135724681357ull};
const PartyQuestPlayerProfileId kCheckpointPlayer{0xABCDEF0123456789ull, 0x9876543210FEDCBAull};

struct CheckpointSandbox
{
    std::filesystem::path Root;

    CheckpointSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_checkpoint_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~CheckpointSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteCheckpointBytes(
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

PartyQuestCoopSavePaths BuildCheckpointPaths(const CheckpointSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCheckpointCampaign,
        kCheckpointPlayer);
    REQUIRE(paths.has_value());
    REQUIRE(PartyQuestCoopSaveLayout::Matches(
        *paths,
        kCheckpointCampaign,
        kCheckpointPlayer));
    return *paths;
}

PartyQuestRuntimeSafetyProfile BuildCheckpointAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 11;
    requirement.ResolvedRecordFingerprint = 0x1111222233334444ull;
    requirement.WinningOverrideFingerprint = 0x2222333344445555ull;
    requirement.ScriptFingerprint = 0x3333444455556666ull;
    requirement.NativeAdapterFingerprint = 0x4444555566667777ull;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestRuntimeApplyRequest BuildCheckpointRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision,
    GameId aQuestId)
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(admission.IsAdmitted());

    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 4;
    snapshot.InitiatorPlayerId = 3;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        admission,
        snapshot,
        BuildCheckpointAuthorization(aQuestId));
    REQUIRE(request.Plan.Safety.IsRuntimeSafe());
    return request;
}

struct DurableCapture
{
    bool Allow{true};
    std::vector<PartyQuestRuntimeRecoveryState> States;

    bool Persist(const PartyQuestRuntimeRecoveryState& acState)
    {
        if (!Allow)
            return false;
        States.push_back(acState);
        return true;
    }
};

PartyQuestRuntimeApplySession BuildCheckpointSession(DurableCapture& aCapture)
{
    return PartyQuestRuntimeApplySession(
        kCheckpointCampaign,
        kCheckpointPlayer,
        [&aCapture](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aCapture.Persist(acState);
        });
}

PartyQuestReplicaCopyPlan BuildPreRepairPlan(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aWorldRevision)
{
    WriteCheckpointBytes(acPaths.SavesDirectory / "Hero.ess", "RUNTIME_GATE_ESS");
    WriteCheckpointBytes(acPaths.SavesDirectory / "Hero.skse", "RUNTIME_GATE_SKSE");
    WriteCheckpointBytes(
        acPaths.SidecarsDirectory / "external" / "Plugin" / "Hero.dat",
        "RUNTIME_GATE_SIDECAR");

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        acPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        acPaths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    const auto sidecar = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::ExternalSidecar,
        acPaths.SidecarsDirectory / "external" / "Plugin" / "Hero.dat",
        "Plugin/Hero.dat");
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    REQUIRE(sidecar.has_value());

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        acPaths,
        PartyQuestCheckpointKind::PreRepair,
        aWorldRevision,
        {*ess, *skse, *sidecar});
    REQUIRE(plan.IsReady());
    return plan;
}

PartyQuestRuntimeCheckpointResult EnsureLowLevelCheckpoint(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision)
{
    const auto coverage =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoverageAuthorization(
            aTransactionId,
            aTargetWorldRevision,
            acPlan);
    REQUIRE(coverage.IsVerified());
    return PartyQuestRuntimeCheckpointCoordinator::EnsurePreRepairCheckpoint(
        aSession,
        acPaths,
        acPlan,
        coverage);
}
} // namespace

TEST_CASE("Runtime checkpoint gate durably publishes exact PreRepair revision before ReadyToApply", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11001, 1250, GameId(41, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);

    const auto plan = BuildPreRepairPlan(paths, request.TargetWorldRevision);
    const auto result = EnsureLowLevelCheckpoint(
        session,
        paths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(result.Status == PartyQuestRuntimeCheckpointStatus::Ready);
    REQUIRE(result.SnapshotStatus == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(result.RuntimeTransition == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(result.TransactionId == request.TransactionId);
    REQUIRE(result.TargetWorldRevision == request.TargetWorldRevision);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE(session.GetCoordinator().GetActive()->CheckpointCreated);

    const auto expectedManifest = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        request.TargetWorldRevision);
    REQUIRE(result.ManifestPath == expectedManifest);
    const auto loaded = PartyQuestReplicaManifestStore::Load(expectedManifest);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    REQUIRE(loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::RevisionCheckpoint);
    REQUIRE(loaded.Manifest->CheckpointKind == PartyQuestCheckpointKind::PreRepair);
    REQUIRE(loaded.Manifest->CampaignWorldRevision == request.TargetWorldRevision);
}

TEST_CASE("Checkpoint gate rejects an unverified coverage token", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11006, 1295, GameId(46, 0x1000));
    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    const auto plan = BuildPreRepairPlan(paths, request.TargetWorldRevision);
    const PartyQuestRuntimeCheckpointCoverageAuthorization unverified;
    const auto result = PartyQuestRuntimeCheckpointCoordinator::EnsurePreRepairCheckpoint(
        session,
        paths,
        plan,
        unverified);
    REQUIRE(result.Status == PartyQuestRuntimeCheckpointStatus::InvalidCoverageAuthorization);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
}

TEST_CASE("Checkpoint gate rejects a plan for a different world revision without advancing runtime state", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11002, 1260, GameId(42, 0x1000));
    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    const auto wrongRevisionPlan = BuildPreRepairPlan(paths, request.TargetWorldRevision + 1);
    const auto result = EnsureLowLevelCheckpoint(
        session,
        paths,
        wrongRevisionPlan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(result.Status == PartyQuestRuntimeCheckpointStatus::InvalidCheckpointPlan);
    REQUIRE(result.SnapshotStatus == PartyQuestReplicaSnapshotStatus::InvalidPlan);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            request.TargetWorldRevision)));
}

TEST_CASE("Checkpoint publication survives runtime-state persistence failure and retry adopts it", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11003, 1270, GameId(43, 0x1000));
    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    const auto plan = BuildPreRepairPlan(paths, request.TargetWorldRevision);
    capture.Allow = false;
    const auto failed = EnsureLowLevelCheckpoint(
        session,
        paths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(failed.Status == PartyQuestRuntimeCheckpointStatus::RuntimeStatePersistenceFailed);
    REQUIRE(failed.SnapshotStatus == PartyQuestReplicaSnapshotStatus::Ready);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
    REQUIRE(std::filesystem::exists(failed.ManifestPath));

    capture.Allow = true;
    const auto retried = EnsureLowLevelCheckpoint(
        session,
        paths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(retried.Status == PartyQuestRuntimeCheckpointStatus::AlreadyReady);
    REQUIRE(retried.SnapshotStatus == PartyQuestReplicaSnapshotStatus::AlreadyReady);
    REQUIRE(retried.RuntimeTransition == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE(session.GetCoordinator().GetActive()->CheckpointCreated);
}

TEST_CASE("Checkpoint gate rejects a path bundle that does not belong to the runtime identity", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11004, 1280, GameId(44, 0x1000));
    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    const auto plan = BuildPreRepairPlan(paths, request.TargetWorldRevision);
    auto wrongPaths = paths;
    wrongPaths.MetadataDirectory /= "OtherPlayer";
    REQUIRE_FALSE(PartyQuestCoopSaveLayout::Matches(
        wrongPaths,
        kCheckpointCampaign,
        kCheckpointPlayer));

    const auto result = EnsureLowLevelCheckpoint(
        session,
        wrongPaths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(result.Status == PartyQuestRuntimeCheckpointStatus::InvalidLayout);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
}

TEST_CASE("Already-ready runtime checkpoint is reverified instead of trusting the boolean", "[quest.party-state.runtime-checkpoint]")
{
    CheckpointSandbox sandbox;
    const auto paths = BuildCheckpointPaths(sandbox);
    DurableCapture capture;
    auto session = BuildCheckpointSession(capture);
    const auto request = BuildCheckpointRequest(11005, 1290, GameId(45, 0x1000));
    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);

    const auto plan = BuildPreRepairPlan(paths, request.TargetWorldRevision);
    REQUIRE(EnsureLowLevelCheckpoint(
                session,
                paths,
                plan,
                request.TransactionId,
                request.TargetWorldRevision).IsReady());

    const auto repeated = EnsureLowLevelCheckpoint(
        session,
        paths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(repeated.Status == PartyQuestRuntimeCheckpointStatus::AlreadyReady);
    REQUIRE(repeated.SnapshotStatus == PartyQuestReplicaSnapshotStatus::Ready);

    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths,
        PartyQuestCheckpointKind::PreRepair,
        request.TargetWorldRevision);
    WriteCheckpointBytes(checkpointRoot / "saves" / "Hero.ess", "CORRUPTED_CHECKPOINT");

    const auto corrupted = EnsureLowLevelCheckpoint(
        session,
        paths,
        plan,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(corrupted.Status == PartyQuestRuntimeCheckpointStatus::SnapshotFailed);
    REQUIRE(corrupted.SnapshotStatus == PartyQuestReplicaSnapshotStatus::FileVerificationFailed);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
}
