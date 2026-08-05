#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

#include <party_quest_pre_repair_checkpoint_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kAssemblerCampaign{
    0x0102030405060708ull,
    0x1112131415161718ull};
const PartyQuestPlayerProfileId kAssemblerPlayer{
    0x2122232425262728ull,
    0x3132333435363738ull};

struct AssemblerSandbox
{
    std::filesystem::path TempRoot;
    PartyQuestCoopSavePaths Paths;

    AssemblerSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        TempRoot = std::filesystem::temp_directory_path() /
            ("tp_party_quest_pre_repair_assembler_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(TempRoot, ec);

        const auto paths = PartyQuestCoopSaveLayout::Build(
            TempRoot / "CoopCampaigns",
            kAssemblerCampaign,
            kAssemblerPlayer);
        REQUIRE(paths.has_value());
        Paths = *paths;

        std::filesystem::create_directories(Paths.SavesDirectory, ec);
        REQUIRE_FALSE(ec);
        std::filesystem::create_directories(Paths.SidecarsDirectory / "external", ec);
        REQUIRE_FALSE(ec);
    }

    ~AssemblerSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(TempRoot, ec);
    }
};

void WriteBytes(const std::filesystem::path& acPath, const char* acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);

    std::ofstream stream(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(acBytes, static_cast<std::streamsize>(std::char_traits<char>::length(acBytes)));
    stream.close();
    REQUIRE(stream.good());
}

PartyQuestRuntimeApplyRequest BuildAssemblerRequest(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(81, static_cast<uint32_t>(0x4000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 4;
    snapshot.InitiatorPlayerId = 12;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    return request;
}

PartyQuestRuntimeApplySession BuildAssemblerSession()
{
    return PartyQuestRuntimeApplySession(
        kAssemblerCampaign,
        kAssemblerPlayer,
        [](const PartyQuestRuntimeRecoveryState&)
        {
            return true;
        });
}

std::vector<PartyQuestReplicaFileSpec> BuildCoreFiles(
    AssemblerSandbox& aSandbox)
{
    const auto essPath = aSandbox.Paths.SavesDirectory / "Controlled.ess";
    const auto sksePath = aSandbox.Paths.SavesDirectory / "Controlled.skse";
    WriteBytes(essPath, "CONTROLLED_CORE_ESS");
    WriteBytes(sksePath, "CONTROLLED_CORE_SKSE");

    const auto ess = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        essPath,
        essPath.filename());
    const auto skse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        sksePath,
        sksePath.filename());
    REQUIRE(ess.has_value());
    REQUIRE(skse.has_value());
    return {*ess, *skse};
}

PartyQuestCheckpointSidecarRequirement BuildRequiredSidecarRequirement()
{
    PartyQuestCheckpointSidecarRequirement requirement;
    requirement.CapabilityId = 0x53494445434152AAull;
    requirement.SchemaVersion = 2;
    requirement.ProviderFingerprint = 0xAAAABBBBCCCCDDDDull;
    requirement.RestoreAdapterFingerprint = 0x1111222233334444ull;
    requirement.Mode = PartyQuestCheckpointSidecarRequirementMode::Required;
    return requirement;
}

PartyQuestCheckpointSidecarAuthorization AuthorizeSidecar(
    const PartyQuestCheckpointSidecarRequirement& acRequirement)
{
    PartyQuestCheckpointSidecarFacts facts;
    facts.CapabilityId = acRequirement.CapabilityId;
    facts.SchemaVersion = acRequirement.SchemaVersion;
    facts.ProviderFingerprint = acRequirement.ProviderFingerprint;
    facts.RestoreAdapterFingerprint = acRequirement.RestoreAdapterFingerprint;
    facts.CaptureAvailable = true;
    facts.RestoreAvailable = true;
    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        acRequirement,
        &facts);
    REQUIRE(decision.IsAuthorized());
    return decision.Authorization;
}

PartyQuestCheckpointSidecarMirrorResult CollectRequiredSidecar(
    AssemblerSandbox& aSandbox,
    const PartyQuestCheckpointSidecarManifest& acManifest,
    const PartyQuestCheckpointSidecarRequirement& acRequirement,
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    const std::filesystem::path relative =
        std::filesystem::path(
            PartyQuestCheckpointSidecarMirrorCollector::FormatCapabilityDirectory(
                acRequirement.CapabilityId)) /
        "state.bin";
    WriteBytes(
        aSandbox.Paths.SidecarsDirectory / "external" / relative,
        "RESTORABLE_EXTERNAL_STATE");

    PartyQuestCheckpointSidecarCapture capture;
    capture.Authorization = AuthorizeSidecar(acRequirement);
    capture.TransactionId = aTransactionId;
    capture.TargetWorldRevision = aWorldRevision;
    capture.MirrorRelativeFiles = {relative};

    return PartyQuestCheckpointSidecarMirrorCollector::Collect(
        aSandbox.Paths,
        acManifest,
        aTransactionId,
        aWorldRevision,
        {capture});
}
} // namespace

TEST_CASE("Full PreRepair assembler is the gate from verified sources to ReadyToApply", "[quest.party-state.pre-repair-assembler]")
{
    AssemblerSandbox sandbox;
    auto session = BuildAssemblerSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildAssemblerRequest(26001, 36001);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto coreFiles = BuildCoreFiles(sandbox);
    const auto coreAuthorization =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoreAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            coreFiles);
    REQUIRE(coreAuthorization.IsVerified());

    PartyQuestCheckpointSidecarManifest emptyManifest;
    const auto sidecars = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        emptyManifest,
        request.TransactionId,
        request.TargetWorldRevision,
        {});
    REQUIRE(sidecars.IsReady());

    const auto result = PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
        guarded,
        sandbox.Paths,
        coreAuthorization,
        coreFiles,
        emptyManifest,
        sidecars);
    REQUIRE(result.Status == PartyQuestRuntimePreRepairCheckpointStatus::Ready);
    REQUIRE(result.PlanStatus == PartyQuestReplicaCopyPlanStatus::Ready);
    REQUIRE(result.Checkpoint.IsReady());
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE(session.GetCoordinator().GetActive()->CheckpointCreated);
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
}

TEST_CASE("Required sidecar absence cannot advance full PreRepair coverage", "[quest.party-state.pre-repair-assembler]")
{
    AssemblerSandbox sandbox;
    auto session = BuildAssemblerSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildAssemblerRequest(26002, 36002);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto coreFiles = BuildCoreFiles(sandbox);
    const auto coreAuthorization =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoreAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            coreFiles);

    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(BuildRequiredSidecarRequirement()));
    const auto sidecars = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        manifest,
        request.TransactionId,
        request.TargetWorldRevision,
        {});
    REQUIRE_FALSE(sidecars.IsReady());
    REQUIRE(sidecars.Status ==
        PartyQuestCheckpointSidecarMirrorStatus::MissingRequiredCapture);

    const auto result = PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
        guarded,
        sandbox.Paths,
        coreAuthorization,
        coreFiles,
        manifest,
        sidecars);
    REQUIRE(result.Status ==
        PartyQuestRuntimePreRepairCheckpointStatus::InvalidSidecarAuthorization);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
}

TEST_CASE("Exact required sidecar mirror is included before checkpoint publication", "[quest.party-state.pre-repair-assembler]")
{
    AssemblerSandbox sandbox;
    auto session = BuildAssemblerSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildAssemblerRequest(26003, 36003);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto coreFiles = BuildCoreFiles(sandbox);
    const auto coreAuthorization =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoreAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            coreFiles);

    const auto requirement = BuildRequiredSidecarRequirement();
    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(requirement));
    const auto sidecars = CollectRequiredSidecar(
        sandbox,
        manifest,
        requirement,
        request.TransactionId,
        request.TargetWorldRevision);
    REQUIRE(sidecars.IsReady());
    REQUIRE(sidecars.Files.size() == 1);

    const auto result = PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
        guarded,
        sandbox.Paths,
        coreAuthorization,
        coreFiles,
        manifest,
        sidecars);
    REQUIRE(result.IsReady());

    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        sandbox.Paths,
        PartyQuestCheckpointKind::PreRepair,
        request.TargetWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());
    REQUIRE(loaded.Manifest->Files.size() == 3);
}

TEST_CASE("Core authorization is invalidated by any file-spec change", "[quest.party-state.pre-repair-assembler]")
{
    AssemblerSandbox sandbox;
    auto session = BuildAssemblerSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildAssemblerRequest(26004, 36004);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    auto coreFiles = BuildCoreFiles(sandbox);
    const auto coreAuthorization =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoreAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            coreFiles);
    REQUIRE(coreAuthorization.IsVerified());

    PartyQuestCheckpointSidecarManifest emptyManifest;
    const auto sidecars = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        emptyManifest,
        request.TransactionId,
        request.TargetWorldRevision,
        {});
    REQUIRE(sidecars.IsReady());

    ++coreFiles.front().Digest;
    const auto result = PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
        guarded,
        sandbox.Paths,
        coreAuthorization,
        coreFiles,
        emptyManifest,
        sidecars);
    REQUIRE(result.Status ==
        PartyQuestRuntimePreRepairCheckpointStatus::InvalidCoreAuthorization);
    REQUIRE(session.GetCoordinator().GetActive()->State ==
        PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
}

TEST_CASE("Forged Ready sidecar status without collector token is rejected", "[quest.party-state.pre-repair-assembler]")
{
    AssemblerSandbox sandbox;
    auto session = BuildAssemblerSession();
    PartyQuestSaveGuard saveGuard;
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildAssemblerRequest(26005, 36005);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);

    const auto coreFiles = BuildCoreFiles(sandbox);
    const auto coreAuthorization =
        PartyQuestRuntimePreRepairCheckpointTestAccess::MakeCoreAuthorization(
            request.TransactionId,
            request.TargetWorldRevision,
            coreFiles);

    PartyQuestCheckpointSidecarManifest emptyManifest;
    PartyQuestCheckpointSidecarMirrorResult forged;
    forged.Status = PartyQuestCheckpointSidecarMirrorStatus::Ready;
    REQUIRE_FALSE(forged.IsReady());

    const auto result = PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
        guarded,
        sandbox.Paths,
        coreAuthorization,
        coreFiles,
        emptyManifest,
        forged);
    REQUIRE(result.Status ==
        PartyQuestRuntimePreRepairCheckpointStatus::InvalidSidecarAuthorization);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->CheckpointCreated);
}
