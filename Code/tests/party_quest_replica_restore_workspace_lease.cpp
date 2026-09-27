#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <catch2/catch.hpp>

#include "party_quest_replica_restore_executor_test_access.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

namespace
{
const PartyQuestCampaignId kRestoreLeaseCampaign{
    0xD101D101D101D101ull,
    0xD202D202D202D202ull};
const PartyQuestPlayerProfileId kRestoreLeasePlayer{
    0xD303D303D303D303ull,
    0xD404D404D404D404ull};
const PartyQuestPlayerProfileId kRestoreLeaseOtherPlayer{
    0xD505D505D505D505ull,
    0xD606D606D606D606ull};

struct RestoreLeaseSandbox
{
    std::filesystem::path Root;

    RestoreLeaseSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_workspace_lease_" +
             std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RestoreLeaseSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildRestoreLeasePaths(
    const RestoreLeaseSandbox& acSandbox,
    const PartyQuestPlayerProfileId& acPlayer = kRestoreLeasePlayer)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kRestoreLeaseCampaign,
        acPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

void WriteRestoreLeaseBytes(
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

std::string ReadRestoreLeaseBytes(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    REQUIRE(file.is_open());
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::filesystem::path BuildRestoreLeaseTransactionDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return acPaths.MetadataDirectory / "restore" / stream.str();
}

PartyQuestReplicaRestorePlan BuildRestoreLeasePlan(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestPlayerProfileId& acPlayer,
    uint64_t aWorldRevision,
    const std::string& acCanonical,
    const std::string& acOriginal)
{
    const auto checkpointRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        acPaths,
        PartyQuestCheckpointKind::PreRepair,
        aWorldRevision);
    const auto source = checkpointRoot / "saves" / "Hero.ess";
    const auto destination = acPaths.SavesDirectory / "Hero.ess";
    WriteRestoreLeaseBytes(source, acCanonical);
    WriteRestoreLeaseBytes(destination, acOriginal);

    const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(source);
    REQUIRE(observation.has_value());

    PartyQuestReplicaRestorePlan plan;
    plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
    plan.CampaignId = kRestoreLeaseCampaign;
    plan.PlayerProfileId = acPlayer;
    plan.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    plan.CampaignWorldRevision = aWorldRevision;
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        source,
        destination,
        observation->Size,
        observation->Digest});
    return plan;
}
} // namespace

TEST_CASE(
    "Standalone restore execute fails closed while exact workspace is leased",
    "[quest.party-state.restore][workspace-lease]")
{
    RestoreLeaseSandbox sandbox;
    const auto paths = BuildRestoreLeasePaths(sandbox);
    const uint64_t restoreId = 0xD701;
    const auto plan = BuildRestoreLeasePlan(
        paths,
        kRestoreLeasePlayer,
        1400,
        "CANONICAL_EXECUTE",
        "ORIGINAL_EXECUTE");

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(
                paths,
                kRestoreLeaseCampaign,
                kRestoreLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

    const auto report = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        restoreId);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::WorkspaceBusy);
    REQUIRE(ReadRestoreLeaseBytes(paths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_EXECUTE");
    REQUIRE_FALSE(std::filesystem::exists(
        BuildRestoreLeaseTransactionDirectory(paths, restoreId)));
}

TEST_CASE(
    "Standalone restore recovery fails closed while exact workspace is leased",
    "[quest.party-state.restore][workspace-lease][recovery]")
{
    RestoreLeaseSandbox sandbox;
    const auto paths = BuildRestoreLeasePaths(sandbox);
    const uint64_t restoreId = 0xD702;
    const auto plan = BuildRestoreLeasePlan(
        paths,
        kRestoreLeasePlayer,
        1401,
        "CANONICAL_RECOVER",
        "ORIGINAL_RECOVER");

    const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(
        paths,
        plan,
        restoreId);
    REQUIRE(prepared.IsReady());
    REQUIRE(prepared.State.has_value());
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(
        *prepared.State);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                journalPath,
                *prepared.State) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(
                paths,
                kRestoreLeaseCampaign,
                kRestoreLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);

    const auto report = PartyQuestReplicaRestoreExecutor::Recover(
        paths,
        kRestoreLeaseCampaign,
        kRestoreLeasePlayer,
        journalPath);
    REQUIRE(report.Status == PartyQuestReplicaRestoreExecutionStatus::WorkspaceBusy);
    REQUIRE(ReadRestoreLeaseBytes(paths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_RECOVER");

    const auto reloaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(reloaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(reloaded.State.has_value());
    REQUIRE(reloaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
}

TEST_CASE(
    "Capability restore reuses the held workspace lease without recursive acquire",
    "[quest.party-state.restore][workspace-lease][capability]")
{
    RestoreLeaseSandbox sandbox;
    const auto paths = BuildRestoreLeasePaths(sandbox);
    const uint64_t restoreId = 0xD703;
    const auto plan = BuildRestoreLeasePlan(
        paths,
        kRestoreLeasePlayer,
        1402,
        "CANONICAL_AUTHORIZED",
        "ORIGINAL_AUTHORIZED");

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(
                paths,
                kRestoreLeaseCampaign,
                kRestoreLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = ownerLease.CreatePublicationCapability(
        paths,
        kRestoreLeaseCampaign,
        kRestoreLeasePlayer);
    REQUIRE(capability.IsVerified());

    PartyQuestReplicaWorkspaceLease competingLease;
    REQUIRE(competingLease.Acquire(
                paths,
                kRestoreLeaseCampaign,
                kRestoreLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Busy);

    const auto standalone = PartyQuestReplicaRestoreExecutor::Execute(
        paths,
        plan,
        restoreId);
    REQUIRE(standalone.Status == PartyQuestReplicaRestoreExecutionStatus::WorkspaceBusy);

    const auto authorized =
        PartyQuestReplicaRestoreExecutorTestAccess::ExecuteAuthorized(
            paths,
            plan,
            restoreId,
            capability);
    REQUIRE(authorized.Status == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(ReadRestoreLeaseBytes(paths.SavesDirectory / "Hero.ess") ==
        "CANONICAL_AUTHORIZED");
}

TEST_CASE(
    "Restore capability is confined to its exact player workspace",
    "[quest.party-state.restore][workspace-lease][capability][confinement]")
{
    RestoreLeaseSandbox sandbox;
    const auto ownerPaths = BuildRestoreLeasePaths(sandbox);
    const auto otherPaths = BuildRestoreLeasePaths(
        sandbox,
        kRestoreLeaseOtherPlayer);
    const uint64_t restoreId = 0xD704;
    const auto otherPlan = BuildRestoreLeasePlan(
        otherPaths,
        kRestoreLeaseOtherPlayer,
        1403,
        "CANONICAL_OTHER",
        "ORIGINAL_OTHER");

    PartyQuestReplicaWorkspaceLease ownerLease;
    REQUIRE(ownerLease.Acquire(
                ownerPaths,
                kRestoreLeaseCampaign,
                kRestoreLeasePlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto ownerCapability = ownerLease.CreatePublicationCapability(
        ownerPaths,
        kRestoreLeaseCampaign,
        kRestoreLeasePlayer);
    REQUIRE(ownerCapability.IsVerified());
    REQUIRE_FALSE(ownerCapability.Protects(
        otherPaths,
        kRestoreLeaseCampaign,
        kRestoreLeaseOtherPlayer));

    const auto wrongWorkspace =
        PartyQuestReplicaRestoreExecutorTestAccess::ExecuteAuthorized(
            otherPaths,
            otherPlan,
            restoreId,
            ownerCapability);
    REQUIRE(wrongWorkspace.Status ==
        PartyQuestReplicaRestoreExecutionStatus::WorkspaceLeaseFailure);
    REQUIRE(ReadRestoreLeaseBytes(otherPaths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_OTHER");
    REQUIRE_FALSE(std::filesystem::exists(
        BuildRestoreLeaseTransactionDirectory(otherPaths, restoreId)));

    const PartyQuestReplicaWorkspacePublicationCapability noCapability;
    const auto missingAuthority =
        PartyQuestReplicaRestoreExecutorTestAccess::ExecuteAuthorized(
            ownerPaths,
            BuildRestoreLeasePlan(
                ownerPaths,
                kRestoreLeasePlayer,
                1404,
                "CANONICAL_NO_CAP",
                "ORIGINAL_NO_CAP"),
            restoreId + 1,
            noCapability);
    REQUIRE(missingAuthority.Status ==
        PartyQuestReplicaRestoreExecutionStatus::WorkspaceLeaseFailure);
    REQUIRE(ReadRestoreLeaseBytes(ownerPaths.SavesDirectory / "Hero.ess") ==
        "ORIGINAL_NO_CAP");
    REQUIRE_FALSE(std::filesystem::exists(
        BuildRestoreLeaseTransactionDirectory(ownerPaths, restoreId + 1)));
}
