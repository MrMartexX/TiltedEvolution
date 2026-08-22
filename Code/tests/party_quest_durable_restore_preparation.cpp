#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaDurableRestorePreparation.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class PartyQuestReplicaDurableRestorePreparationTestAccess final
{
public:
    [[nodiscard]] static PartyQuestReplicaDurableRestorePreparationReport PrepareAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept
    {
        return PartyQuestReplicaDurableRestorePreparation::PrepareAuthorized(
            acPaths,
            acPlan,
            aRestoreId,
            acWorkspaceCapability);
    }
};

namespace
{
const PartyQuestCampaignId kCampaign{
    0x5152535455565758ull,
    0x6162636465666768ull};
const PartyQuestPlayerProfileId kPlayer{
    0x7172737475767778ull,
    0x8182838485868788ull};
constexpr uint64_t kRevision = 5101;
constexpr uint64_t kRestoreId = 0x51510001;

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_durable_restore_prepare_" + std::to_string(nonce));
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

PartyQuestReplicaRestorePlan BuildPlan(
    const Sandbox& acSandbox,
    PartyQuestCoopSavePaths& aPaths)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
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
        aPaths,
        {*soloEss, *soloSkse});
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

    WriteText(aPaths.SavesDirectory / "Hero.ess", "LIVE_DIVERGED_ESS");
    WriteText(aPaths.SavesDirectory / "Hero.skse", "LIVE_DIVERGED_SKSE");

    const auto manifestPath =
        PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
            aPaths,
            PartyQuestCheckpointKind::PreRepair,
            kRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    REQUIRE(loaded.Status == PartyQuestReplicaManifestPersistenceStatus::Success);
    REQUIRE(loaded.Manifest.has_value());

    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        aPaths,
        kCampaign,
        kPlayer,
        *loaded.Manifest);
    REQUIRE(plan.IsReady());
    return plan;
}
} // namespace

TEST_CASE(
    "durable restore preparation reaches and idempotently revalidates BackupsReady without live replacement",
    "[quest.party-state.replica-restore][durability][pre-mutation]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildPlan(sandbox, paths);

    const auto preview = PartyQuestReplicaRestoreJournal::Prepare(
        paths,
        plan,
        kRestoreId);
    REQUIRE(preview.IsReady());
    const auto transaction = preview.State->TransactionDirectory;

    const auto prepared = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId);

    REQUIRE(prepared.IsBackupsReady());
    REQUIRE(prepared.State.has_value());
    REQUIRE(prepared.CompletedBackups == 2);
    REQUIRE(prepared.State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE(std::filesystem::exists(prepared.JournalPath));
    REQUIRE(std::filesystem::exists(transaction));

    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    for (const auto& operation : prepared.State->Operations)
    {
        REQUIRE(operation.DestinationExisted);
        const auto rollback = PartyQuestReplicaFileExecutor::ObserveRegularFile(
            operation.RollbackPath);
        REQUIRE(rollback.has_value());
        REQUIRE(rollback->Size == operation.OriginalSize);
        REQUIRE(rollback->Digest == operation.OriginalDigest);
    }

    const auto journal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            prepared.JournalPath);
    REQUIRE(journal.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(journal.State.has_value());
    REQUIRE(journal.State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*journal.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::ResumeBeforeMutation);

    const auto duplicate = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId);
    REQUIRE(duplicate.IsBackupsReady());
    REQUIRE(duplicate.CompletedBackups == 2);
    REQUIRE(duplicate.State == prepared.State);

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable restore preparation reuses an exact held workspace capability without weakening lease exclusivity",
    "[quest.party-state.replica-restore][durability][workspace-lease]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildPlan(sandbox, paths);
    constexpr uint64_t authorizedRestoreId = kRestoreId + 10;
    constexpr uint64_t rejectedRestoreId = kRestoreId + 11;

    PartyQuestReplicaWorkspaceLease heldLease;
    REQUIRE(heldLease.Acquire(paths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = heldLease.CreatePublicationCapability(
        paths,
        kCampaign,
        kPlayer);
    REQUIRE(capability.Protects(paths, kCampaign, kPlayer));

    const auto recursivePublic = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        authorizedRestoreId);
    REQUIRE(recursivePublic.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceBusy);

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto rejected =
        PartyQuestReplicaDurableRestorePreparationTestAccess::PrepareAuthorized(
            paths,
            plan,
            rejectedRestoreId,
            missingCapability);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::WorkspaceLeaseFailure);

    const auto rejectedPreview = PartyQuestReplicaRestoreJournal::Prepare(
        paths,
        plan,
        rejectedRestoreId);
    REQUIRE(rejectedPreview.IsReady());
    REQUIRE_FALSE(std::filesystem::exists(
        rejectedPreview.State->TransactionDirectory));

    const auto authorized =
        PartyQuestReplicaDurableRestorePreparationTestAccess::PrepareAuthorized(
            paths,
            plan,
            authorizedRestoreId,
            capability);
    REQUIRE(authorized.IsBackupsReady());
    REQUIRE(authorized.State.has_value());
    REQUIRE(authorized.State->RestoreId == authorizedRestoreId);
    REQUIRE(authorized.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");
    REQUIRE(heldLease.IsHeld());
    REQUIRE(heldLease.Protects(paths, kCampaign, kPlayer));
}

TEST_CASE(
    "durable restore preparation resumes an exact partial Prepared journal after restart",
    "[quest.party-state.replica-restore][durability][pre-mutation][recovery]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildPlan(sandbox, paths);

    const auto initial = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId + 1);

    REQUIRE(initial.IsBackupsReady());
    REQUIRE(initial.State.has_value());
    REQUIRE(initial.State->Operations.size() == 2);

    PartyQuestReplicaRestoreJournalState interrupted = *initial.State;
    interrupted.Phase = PartyQuestReplicaRestoreJournalPhase::Prepared;
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                initial.JournalPath,
                interrupted) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto missingBackup = interrupted.Operations[1].RollbackPath;
    std::error_code ec;
    REQUIRE(std::filesystem::remove(missingBackup, ec));
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(std::filesystem::exists(missingBackup));

    const auto resumed = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId + 1);
    REQUIRE(resumed.IsBackupsReady());
    REQUIRE(resumed.CompletedBackups == 2);
    REQUIRE(resumed.State.has_value());
    REQUIRE(resumed.State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
    REQUIRE(std::filesystem::exists(missingBackup));
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.ess") == "LIVE_DIVERGED_ESS");
    REQUIRE(ReadText(paths.SavesDirectory / "Hero.skse") == "LIVE_DIVERGED_SKSE");

    const auto persisted =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            resumed.JournalPath);
    REQUIRE(persisted.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(persisted.State.has_value());
    REQUIRE(persisted.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::BackupsReady);
}

TEST_CASE(
    "durable restore preparation never overwrites mismatched Prepared rollback evidence",
    "[quest.party-state.replica-restore][durability][pre-mutation][recovery]")
{
    Sandbox sandbox;
    PartyQuestCoopSavePaths paths;
    const auto plan = BuildPlan(sandbox, paths);

    const auto initial = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId + 2);

    REQUIRE(initial.IsBackupsReady());
    REQUIRE(initial.State.has_value());
    PartyQuestReplicaRestoreJournalState interrupted = *initial.State;
    interrupted.Phase = PartyQuestReplicaRestoreJournalPhase::Prepared;
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                initial.JournalPath,
                interrupted) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto corruptPath = interrupted.Operations[0].RollbackPath;
    WriteText(corruptPath, "CORRUPT_ROLLBACK_EVIDENCE");
    const auto corruptBytes = ReadText(corruptPath);

    const auto refused = PartyQuestReplicaDurableRestorePreparation::Prepare(
        paths,
        plan,
        kRestoreId + 2);
    REQUIRE(refused.Status ==
        PartyQuestReplicaDurableRestorePreparationStatus::BackupVerificationFailed);
    REQUIRE(refused.State.has_value());
    REQUIRE(refused.State->Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
    REQUIRE(refused.FailedPath == corruptPath);
    REQUIRE(ReadText(corruptPath) == corruptBytes);

    const auto persisted =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            initial.JournalPath);
    REQUIRE(persisted.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(persisted.State.has_value());
    REQUIRE(persisted.State->Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
}
