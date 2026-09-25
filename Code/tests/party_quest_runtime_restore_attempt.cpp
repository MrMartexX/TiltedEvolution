#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kCampaign{0xA101A101A101A101ull, 0xA202A202A202A202ull};
const PartyQuestPlayerProfileId kPlayer{0xA303A303A303A303ull, 0xA404A404A404A404ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_restore_attempt_" + std::to_string(nonce));
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

PartyQuestCoopSavePaths BuildPaths(const Sandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns", kCampaign, kPlayer);
    REQUIRE(paths.has_value());
    std::error_code ec;
    std::filesystem::create_directories(paths->PlayerDirectory, ec);
    REQUIRE_FALSE(ec);
    return *paths;
}

PartyQuestReplicaWorkspacePublicationCapability AcquireCapability(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaWorkspaceLease& aLease)
{
    REQUIRE(aLease.Acquire(acPaths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    auto capability = aLease.CreatePublicationCapability(acPaths, kCampaign, kPlayer);
    REQUIRE(capability.IsVerified());
    REQUIRE(capability.Protects(acPaths, kCampaign, kPlayer));
    return capability;
}

PartyQuestReplicaRestoreJournalState BuildJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint32_t aOrdinal,
    PartyQuestReplicaRestoreJournalPhase aPhase)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = aRestoreId;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 9000 + aOrdinal;
    state.Phase = aPhase;
    state.TransactionDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            acPaths, aRestoreId);

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath =
        acPaths.CheckpointsDirectory / "PreRepair" / "source.ess";
    operation.ReplicaDestinationPath = acPaths.SavesDirectory / "Hero.ess";
    operation.RollbackPath = state.TransactionDirectory / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 10;
    operation.ExpectedRestoredDigest = 0x1111111111111111ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 11;
    operation.OriginalDigest = 0x2222222222222222ull;
    state.Operations.push_back(std::move(operation));
    return state;
}

void PersistJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint32_t aOrdinal,
    PartyQuestReplicaRestoreJournalPhase aPhase)
{
    const auto journal = BuildJournal(acPaths, aRestoreId, aOrdinal, aPhase);
    std::error_code ec;
    std::filesystem::create_directories(journal.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    const auto path = PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
        acPaths, aRestoreId);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                path, journal) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
}
} // namespace

TEST_CASE(
    "runtime restore attempt keeps exact transaction identity separate from physical restore id",
    "[quest.party-state.runtime-recovery][restore-attempt]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);

    constexpr uint64_t txA = 0x1001;
    constexpr uint64_t txB = 0x1002;
    const auto stateA = PartyQuestRuntimeRestoreAttemptStore::GetStatePath(paths, txA);
    const auto stateB = PartyQuestRuntimeRestoreAttemptStore::GetStatePath(paths, txB);
    const auto restoreA = PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(paths, 1);
    const auto restoreB = PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(paths, 2);

    REQUIRE_FALSE(stateA.empty());
    REQUIRE(stateA != stateB);
    REQUIRE(stateA.filename().string().find("0000000000001001") != std::string::npos);
    REQUIRE(restoreA != restoreB);
    REQUIRE(restoreA.filename() == "Transaction_0000000000000001");
    REQUIRE(restoreB.filename() == "Transaction_0000000000000002");
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(paths, 0).empty());
}

TEST_CASE(
    "runtime restore id allocation requires workspace authority and skips occupied tombstones",
    "[quest.party-state.runtime-recovery][restore-attempt][durability]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 0x2101;
    constexpr uint64_t secondTransactionId = 0x2102;

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto unauthorized =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, missingCapability);
    REQUIRE(unauthorized.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::WorkspaceCapabilityRequired);

    // Simulate a retained legacy/strong tombstone occupying RestoreId 1 before
    // the new allocator has ever been initialized.
    const auto occupied = PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(paths, 1);
    std::error_code ec;
    std::filesystem::create_directories(occupied, ec);
    REQUIRE_FALSE(ec);

    PartyQuestReplicaWorkspaceLease lease;
    const auto capability = AcquireCapability(paths, lease);
    const auto initialized =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, capability);

    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(initialized.State.has_value());
    REQUIRE(initialized.State->TransactionId == transactionId);
    REQUIRE(initialized.State->CurrentOrdinal == 0);
    REQUIRE(initialized.State->CurrentRestoreId == 2);
    REQUIRE(initialized.State->LastRolledBackRestoreId == 0);
    REQUIRE(initialized.RestoreId == 2);
    REQUIRE(initialized.JournalPath ==
        PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(paths, 2));

    const auto reloaded = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths, kCampaign, kPlayer, transactionId);
    REQUIRE(reloaded.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(reloaded.State == initialized.State);

    const auto repeated =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, capability);
    REQUIRE(repeated.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(repeated.State == initialized.State);

    const auto second =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, secondTransactionId, capability);
    REQUIRE(second.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(second.State.has_value());
    REQUIRE(second.State->TransactionId == secondTransactionId);
    REQUIRE(second.State->CurrentRestoreId == 3);
    REQUIRE(second.RestoreId != initialized.RestoreId);
}

TEST_CASE(
    "terminal rollback advances a restore attempt exactly once across restart",
    "[quest.party-state.runtime-recovery][restore-attempt][retry]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 0x3101;

    PartyQuestReplicaWorkspaceLease lease;
    const auto capability = AcquireCapability(paths, lease);
    const auto initialized =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, capability);

    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(initialized.State.has_value());
    const uint64_t firstRestoreId = initialized.State->CurrentRestoreId;
    PersistJournal(
        paths,
        firstRestoreId,
        0,
        PartyQuestReplicaRestoreJournalPhase::RolledBack);

    const auto advanced =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 0, capability);
    REQUIRE(advanced.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(advanced.State.has_value());
    REQUIRE(advanced.State->CurrentOrdinal == 1);
    REQUIRE(advanced.State->LastRolledBackRestoreId == firstRestoreId);
    REQUIRE(advanced.State->CurrentRestoreId != firstRestoreId);
    const uint64_t secondRestoreId = advanced.State->CurrentRestoreId;

    const auto afterRestart = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths, kCampaign, kPlayer, transactionId);
    REQUIRE(afterRestart.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(afterRestart.State.has_value());
    REQUIRE(afterRestart.State->CurrentOrdinal == 1);
    REQUIRE(afterRestart.State->CurrentRestoreId == secondRestoreId);

    const auto replay =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 0, capability);
    REQUIRE(replay.Status == PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced);
    REQUIRE(replay.State.has_value());
    REQUIRE(replay.State->CurrentOrdinal == 1);
    REQUIRE(replay.State->CurrentRestoreId == secondRestoreId);

    const auto noSecondJournal =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 1, capability);
    REQUIRE(noSecondJournal.Status == PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);
    REQUIRE(noSecondJournal.State.has_value());
    REQUIRE(noSecondJournal.State->CurrentOrdinal == 1);
    REQUIRE(noSecondJournal.State->CurrentRestoreId == secondRestoreId);

    PersistJournal(
        paths,
        secondRestoreId,
        1,
        PartyQuestReplicaRestoreJournalPhase::Prepared);
    const auto nonterminal =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 1, capability);
    REQUIRE(nonterminal.Status == PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch);
    REQUIRE(nonterminal.State.has_value());
    REQUIRE(nonterminal.State->CurrentOrdinal == 1);
    REQUIRE(nonterminal.State->CurrentRestoreId == secondRestoreId);
}
