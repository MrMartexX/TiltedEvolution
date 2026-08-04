#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <fstream>

namespace
{
const PartyQuestCampaignId kMirrorCampaign{
    0xA1A2A3A4A5A6A7A8ull,
    0xB1B2B3B4B5B6B7B8ull};
const PartyQuestPlayerProfileId kMirrorPlayer{
    0xC1C2C3C4C5C6C7C8ull,
    0xD1D2D3D4D5D6D7D8ull};
constexpr uint64_t kTransactionId = 0x1010101010101010ull;
constexpr uint64_t kWorldRevision = 0x2020202020202020ull;

struct MirrorSandbox
{
    std::filesystem::path TempRoot;
    PartyQuestCoopSavePaths Paths;

    MirrorSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        TempRoot = std::filesystem::temp_directory_path() /
            ("tp_party_quest_sidecar_mirror_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(TempRoot, ec);

        const auto paths = PartyQuestCoopSaveLayout::Build(
            TempRoot / "CoopCampaigns",
            kMirrorCampaign,
            kMirrorPlayer);
        REQUIRE(paths.has_value());
        Paths = *paths;

        std::filesystem::create_directories(Paths.PlayerDirectory, ec);
        REQUIRE_FALSE(ec);
        std::filesystem::create_directories(Paths.SidecarsDirectory / "external", ec);
        REQUIRE_FALSE(ec);
    }

    ~MirrorSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(TempRoot, ec);
    }
};

PartyQuestCheckpointSidecarRequirement BuildMirrorRequirement(
    PartyQuestCheckpointSidecarRequirementMode aMode =
        PartyQuestCheckpointSidecarRequirementMode::Required)
{
    PartyQuestCheckpointSidecarRequirement requirement;
    requirement.CapabilityId = 0x5349444543415202ull;
    requirement.SchemaVersion = 7;
    requirement.ProviderFingerprint = 0x1234567890ABCDEFull;
    requirement.RestoreAdapterFingerprint = 0x0FEDCBA098765432ull;
    requirement.Mode = aMode;
    return requirement;
}

PartyQuestCheckpointSidecarAuthorization Authorize(
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

std::filesystem::path WriteMirrorFile(
    MirrorSandbox& aSandbox,
    uint64_t aCapabilityId,
    const char* acName,
    const char* acBytes)
{
    const std::filesystem::path relative =
        std::filesystem::path(
            PartyQuestCheckpointSidecarMirrorCollector::FormatCapabilityDirectory(
                aCapabilityId)) /
        acName;
    const auto source = aSandbox.Paths.SidecarsDirectory / "external" / relative;

    std::error_code ec;
    std::filesystem::create_directories(source.parent_path(), ec);
    REQUIRE_FALSE(ec);

    std::ofstream stream(source, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(acBytes, static_cast<std::streamsize>(std::char_traits<char>::length(acBytes)));
    stream.close();
    REQUIRE(stream.good());
    return relative;
}
} // namespace

TEST_CASE("Verified sidecar mirror capture becomes planner-ready file evidence", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    const auto requirement = BuildMirrorRequirement();
    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(requirement));

    PartyQuestCheckpointSidecarCapture capture;
    capture.Authorization = Authorize(requirement);
    capture.TransactionId = kTransactionId;
    capture.TargetWorldRevision = kWorldRevision;
    capture.MirrorRelativeFiles.push_back(
        WriteMirrorFile(
            sandbox,
            requirement.CapabilityId,
            "provider_state.bin",
            "sidecar-state-v7"));

    const auto result = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        manifest,
        kTransactionId,
        kWorldRevision,
        {capture});

    REQUIRE(result.Status == PartyQuestCheckpointSidecarMirrorStatus::Ready);
    REQUIRE(result.IsReady());
    REQUIRE(result.Files.size() == 1);
    REQUIRE(result.Files[0].Kind == PartyQuestReplicaFileKind::ExternalSidecar);
    REQUIRE(result.Files[0].RelativePath == capture.MirrorRelativeFiles[0]);
    REQUIRE(result.Files[0].Size > 0);
    REQUIRE(result.Files[0].Digest != 0);

    const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        sandbox.Paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision,
        result.Files);
    // Sidecar-only coverage is intentionally not a complete checkpoint: the
    // planner still requires exactly one main .ess from the controlled core
    // save source before the final composition step.
    REQUIRE(plan.Status == PartyQuestReplicaCopyPlanStatus::MissingMainSave);
}

TEST_CASE("Required sidecar capability cannot disappear from checkpoint coverage", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    PartyQuestCheckpointSidecarManifest manifest;
    const auto requirement = BuildMirrorRequirement();
    REQUIRE(manifest.AddRequirement(requirement));

    const auto result = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        manifest,
        kTransactionId,
        kWorldRevision,
        {});
    REQUIRE(result.Status ==
        PartyQuestCheckpointSidecarMirrorStatus::MissingRequiredCapture);
    REQUIRE(result.FailedCapabilityId == requirement.CapabilityId);
}

TEST_CASE("Absent optional sidecar capability needs no mirror filesystem evidence", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    std::error_code ec;
    std::filesystem::remove_all(sandbox.Paths.SidecarsDirectory / "external", ec);
    REQUIRE_FALSE(ec);

    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(BuildMirrorRequirement(
        PartyQuestCheckpointSidecarRequirementMode::Optional)));

    const auto result = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        manifest,
        kTransactionId,
        kWorldRevision,
        {});
    REQUIRE(result.Status == PartyQuestCheckpointSidecarMirrorStatus::Ready);
    REQUIRE(result.Files.empty());
}

TEST_CASE("Sidecar capture receipt is bound to exact repair transaction and revision", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    const auto requirement = BuildMirrorRequirement();
    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(requirement));

    PartyQuestCheckpointSidecarCapture capture;
    capture.Authorization = Authorize(requirement);
    capture.TransactionId = kTransactionId + 1;
    capture.TargetWorldRevision = kWorldRevision;
    capture.MirrorRelativeFiles.push_back(
        WriteMirrorFile(sandbox, requirement.CapabilityId, "state.bin", "state"));

    REQUIRE(PartyQuestCheckpointSidecarMirrorCollector::Collect(
                sandbox.Paths,
                manifest,
                kTransactionId,
                kWorldRevision,
                {capture}).Status ==
        PartyQuestCheckpointSidecarMirrorStatus::TransactionMismatch);

    capture.TransactionId = kTransactionId;
    ++capture.TargetWorldRevision;
    REQUIRE(PartyQuestCheckpointSidecarMirrorCollector::Collect(
                sandbox.Paths,
                manifest,
                kTransactionId,
                kWorldRevision,
                {capture}).Status ==
        PartyQuestCheckpointSidecarMirrorStatus::WorldRevisionMismatch);
}

TEST_CASE("Sidecar mirror path cannot escape or impersonate another capability", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    const auto requirement = BuildMirrorRequirement();
    PartyQuestCheckpointSidecarManifest manifest;
    REQUIRE(manifest.AddRequirement(requirement));

    PartyQuestCheckpointSidecarCapture capture;
    capture.Authorization = Authorize(requirement);
    capture.TransactionId = kTransactionId;
    capture.TargetWorldRevision = kWorldRevision;

    SECTION("traversal")
    {
        capture.MirrorRelativeFiles = {"../outside.bin"};
        REQUIRE(PartyQuestCheckpointSidecarMirrorCollector::Collect(
                    sandbox.Paths,
                    manifest,
                    kTransactionId,
                    kWorldRevision,
                    {capture}).Status ==
            PartyQuestCheckpointSidecarMirrorStatus::InvalidRelativePath);
    }

    SECTION("different capability namespace")
    {
        const auto wrongCapability = requirement.CapabilityId + 1;
        capture.MirrorRelativeFiles = {
            std::filesystem::path(
                PartyQuestCheckpointSidecarMirrorCollector::FormatCapabilityDirectory(
                    wrongCapability)) /
            "state.bin"};
        REQUIRE(PartyQuestCheckpointSidecarMirrorCollector::Collect(
                    sandbox.Paths,
                    manifest,
                    kTransactionId,
                    kWorldRevision,
                    {capture}).Status ==
            PartyQuestCheckpointSidecarMirrorStatus::CapabilityPathMismatch);
    }
}

TEST_CASE("Unlisted sidecar capability is rejected even with a verified local token", "[quest.party-state.sidecar-mirror]")
{
    MirrorSandbox sandbox;
    const auto requirement = BuildMirrorRequirement();
    PartyQuestCheckpointSidecarManifest emptyManifest;

    PartyQuestCheckpointSidecarCapture capture;
    capture.Authorization = Authorize(requirement);
    capture.TransactionId = kTransactionId;
    capture.TargetWorldRevision = kWorldRevision;
    capture.MirrorRelativeFiles.push_back(
        WriteMirrorFile(sandbox, requirement.CapabilityId, "state.bin", "state"));

    const auto result = PartyQuestCheckpointSidecarMirrorCollector::Collect(
        sandbox.Paths,
        emptyManifest,
        kTransactionId,
        kWorldRevision,
        {capture});
    REQUIRE(result.Status == PartyQuestCheckpointSidecarMirrorStatus::UnexpectedCapability);
}
