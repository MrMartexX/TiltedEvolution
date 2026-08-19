#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableRestoreExecutor.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableRestorePreparation.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

class PartyQuestReplicaDurableRestoreExecutorTestAccess final
{
public:
    [[nodiscard]] static PartyQuestReplicaDurableRestoreReport ContinueAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        const PartyQuestReplicaWorkspacePublicationCapability& acCapability,
        PartyQuestReplicaDurableRestoreHooks aHooks = {}) noexcept
    {
        return PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            acJournalPath,
            acCapability,
            aHooks);
    }

    [[nodiscard]] static PartyQuestReplicaDurableRestoreReport RecoverAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        const PartyQuestReplicaWorkspacePublicationCapability& acCapability,
        PartyQuestReplicaDurableRestoreHooks aHooks = {}) noexcept
    {
        return PartyQuestReplicaDurableRestoreExecutor::RecoverAuthorized(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            acJournalPath,
            acCapability,
            aHooks);
    }
};

namespace
{
const PartyQuestCampaignId kCampaign{
    0xD101D101D101D101ull,
    0xD202D202D202D202ull};
const PartyQuestPlayerProfileId kPlayer{
    0xD303D303D303D303ull,
    0xD404D404D404D404ull};
constexpr uint64_t kRevision = 8701;
constexpr uint64_t kSuccessRestoreId = 0xD501;
constexpr uint64_t kRollbackRestoreId = 0xD502;

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_durable_executor_capability_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~Sandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteText(const std::filesystem::path& acPath, const std::string& acText)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(acText.data(), static_cast<std::streamsize>(acText.size()));
    file.flush();
    REQUIRE(file.good());
}

std::string ReadText(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

PartyQuestReplicaRestorePlan BuildRestorePlan(
    const Sandbox& acSandbox,
    PartyQuestCoopSavePaths& aPaths,
    const std::string& acOriginalEss,
    const std::string& acOriginalSkse)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns", kCampaign, kPlayer);
    REQUIRE(paths.has_value());
    aPaths = *paths;

    const auto solo = acSandbox.Root / "Solo";
    WriteText(solo / "Hero.ess", "CHECKPOINT_ESS");
    WriteText(solo / "Hero.skse", "CHECKPOINT_SKSE");

    const auto soloEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        solo / "Hero.ess",
        "Hero.ess");
    const auto soloSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        solo / "Hero.skse",
        "Hero.skse");
    REQUIRE(soloEss.has_value());
    REQUIRE(soloSkse.has_value());

    const auto importPlan = PartyQuestReplicaFilePlanner::BuildImportPlan(
        aPaths, {*soloEss, *soloSkse});
    REQUIRE(importPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(aPaths, kCampaign, kPlayer);
    REQUIRE(manager.EnsureImportedReplica(kRevision, importPlan).IsReady());

    const auto liveEss = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        aPaths.SavesDirectory / "Hero.ess",
        "Hero.ess");
    const auto liveSkse = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkseCosave,
        aPaths.SavesDirectory / "Hero.skse",
        "Hero.skse");
    REQUIRE(liveEss.has_value());
    REQUIRE(liveSkse.has_value());

    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        aPaths,
        PartyQuestCheckpointKind::PreRepair,
        kRevision,
        {*liveEss, *liveSkse});
    REQUIRE(checkpointPlan.IsReady());
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                kRevision,
                checkpointPlan).IsReady());

    WriteText(aPaths.SavesDirectory / "Hero.ess", acOriginalEss);
    WriteText(aPaths.SavesDirectory / "Hero.skse", acOriginalSkse);

    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            aPaths,
            PartyQuestCheckpointKind::PreRepair,
            kRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());

    const auto restorePlan = PartyQuestReplicaRestorePlanner::Build(
        aPaths, kCampaign, kPlayer, *loaded.Manifest);
    REQUIRE(restorePlan.IsReady());
    return restorePlan;
}

struct FailAtMutationStarted
{
    static PartyQuestReplicaDurableRestoreDirective Invoke(
        PartyQuestReplicaDurableRestoreBoundary aBoundary,
        size_t,
        void*) noexcept
    {
        return aBoundary == PartyQuestReplicaDurableRestoreBoundary::MutationStartedDurable
            ? PartyQuestReplicaDurableRestoreDirective::FailClosed
            : PartyQuestReplicaDurableRestoreDirective::Continue;
    }
};
} // namespace

TEST_CASE(
    "strong restore continuation reuses one exact held workspace capability",
    "[quest.party-state.replica-restore][durability][workspace-lease]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildRestorePlan(
        sandbox,
        paths,
        "LIVE_ORIGINAL_ESS",
        "LIVE_ORIGINAL_SKSE");

    const auto prepared = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths, plan, kSuccessRestoreId);

#ifdef _WIN32
    REQUIRE(prepared.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::UnsupportedPlatform);

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto unsupported =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::ContinueAuthorized(
            paths,
            kCampaign,
            kPlayer,
            paths.MetadataDirectory / "restore" / "missing" / "journal.bin",
            missingCapability);
    REQUIRE(unsupported.Status == PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform);
#else
    REQUIRE(prepared.IsBackupsReady());
    REQUIRE(prepared.State.has_value());

    PartyQuestReplicaWorkspaceLease heldLease;
    REQUIRE(heldLease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = heldLease.CreatePublicationCapability(
        paths, kCampaign, kPlayer);
    REQUIRE(capability.Protects(paths, kCampaign, kPlayer));

    const auto recursivePublic = PartyQuestReplicaDurableRestoreExecutor::Continue(
        paths,
        kCampaign,
        kPlayer,
        prepared.JournalPath);
    REQUIRE(recursivePublic.Status == PartyQuestReplicaDurableRestoreStatus::WorkspaceBusy);
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "LIVE_ORIGINAL_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "LIVE_ORIGINAL_SKSE");

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto rejected =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::ContinueAuthorized(
            paths,
            kCampaign,
            kPlayer,
            prepared.JournalPath,
            missingCapability);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure);
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "LIVE_ORIGINAL_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "LIVE_ORIGINAL_SKSE");

    const auto continued =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::ContinueAuthorized(
            paths,
            kCampaign,
            kPlayer,
            prepared.JournalPath,
            capability);
    REQUIRE(continued.Status == PartyQuestReplicaDurableRestoreStatus::Success);
    REQUIRE(continued.Phase == PartyQuestReplicaRestoreJournalPhase::Committed);
    REQUIRE_FALSE(continued.RequiresRecovery);
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "CHECKPOINT_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "CHECKPOINT_SKSE");
    REQUIRE(heldLease.IsHeld());
    REQUIRE(heldLease.Protects(paths, kCampaign, kPlayer));
#endif

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "strong restore recovery reuses held capability without reopening the workspace",
    "[quest.party-state.replica-restore][durability][workspace-lease][recovery]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildRestorePlan(
        sandbox,
        paths,
        "RECOVERY_ORIGINAL_ESS",
        "RECOVERY_ORIGINAL_SKSE");

    const auto prepared = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths, plan, kRollbackRestoreId);

#ifdef _WIN32
    REQUIRE(prepared.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::UnsupportedPlatform);

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto unsupported =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::RecoverAuthorized(
            paths,
            kCampaign,
            kPlayer,
            paths.MetadataDirectory / "restore" / "missing" / "journal.bin",
            missingCapability);
    REQUIRE(unsupported.Status == PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform);
#else
    REQUIRE(prepared.IsBackupsReady());

    PartyQuestReplicaWorkspaceLease heldLease;
    REQUIRE(heldLease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = heldLease.CreatePublicationCapability(
        paths, kCampaign, kPlayer);
    REQUIRE(capability.Protects(paths, kCampaign, kPlayer));

    PartyQuestReplicaDurableRestoreHooks hooks;
    hooks.OnBoundary = &FailAtMutationStarted::Invoke;
    const auto interrupted =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::ContinueAuthorized(
            paths,
            kCampaign,
            kPlayer,
            prepared.JournalPath,
            capability,
            hooks);
    REQUIRE(interrupted.Status == PartyQuestReplicaDurableRestoreStatus::FaultInjected);
    REQUIRE(interrupted.Phase == PartyQuestReplicaRestoreJournalPhase::MutationStarted);
    REQUIRE(interrupted.RequiresRecovery);

    const auto mutationJournal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            prepared.JournalPath);
    REQUIRE(mutationJournal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(mutationJournal.State.has_value());
    REQUIRE(mutationJournal.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::MutationStarted);

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto rejected =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::RecoverAuthorized(
            paths,
            kCampaign,
            kPlayer,
            prepared.JournalPath,
            missingCapability);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure);

    const auto recovered =
        PartyQuestReplicaDurableRestoreExecutorTestAccess::RecoverAuthorized(
            paths,
            kCampaign,
            kPlayer,
            prepared.JournalPath,
            capability);
    REQUIRE(recovered.Status ==
        PartyQuestReplicaDurableRestoreStatus::RecoveredRollback);
    REQUIRE(recovered.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(recovered.RollbackPerformed);
    REQUIRE_FALSE(recovered.RequiresRecovery);
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "RECOVERY_ORIGINAL_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "RECOVERY_ORIGINAL_SKSE");

    const auto terminal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            prepared.JournalPath);
    REQUIRE(terminal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(terminal.State.has_value());
    REQUIRE(terminal.State->Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(heldLease.IsHeld());
    REQUIRE(heldLease.Protects(paths, kCampaign, kPlayer));
#endif
}