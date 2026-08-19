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
    uint64_t aTransactionId,
    uint32_t aOrdinal,
    PartyQuestReplicaRestoreJournalPhase aPhase)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = PartyQuestRuntimeRestoreAttemptStore::GetRestoreId(aOrdinal);
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 9000 + aOrdinal;
    state.Phase = aPhase;
    state.TransactionDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(
            acPaths, aTransactionId, aOrdinal);

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
    uint64_t aTransactionId,
    uint32_t aOrdinal,
    PartyQuestReplicaRestoreJournalPhase aPhase)
{
    const auto journal = BuildJournal(acPaths, aTransactionId, aOrdinal, aPhase);
    std::error_code ec;
    std::filesystem::create_directories(journal.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    const auto path = PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
        acPaths, aTransactionId, aOrdinal);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                path, journal) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
}
} // namespace

TEST_CASE(
    "runtime restore attempt identity is structural and transaction namespaced",
    "[quest.party-state.runtime-recovery][restore-attempt]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);

    constexpr uint64_t txA = 0x1001;
    constexpr uint64_t txB = 0x1002;
    const auto a0 = PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(paths, txA, 0);
    const auto a1 = PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(paths, txA, 1);
    const auto b0 = PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(paths, txB, 0);

    REQUIRE_FALSE(a0.empty());
    REQUIRE(a0 != a1);
    REQUIRE(a0 != b0);
    REQUIRE(a0.parent_path().filename() != b0.parent_path().filename());
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::GetRestoreId(0) == 1);
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::GetRestoreId(1) == 2);
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(
                paths,
                txA,
                PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction).empty());
}

TEST_CASE(
    "runtime restore attempt initialization requires exact workspace authority and strong platform support",
    "[quest.party-state.runtime-recovery][restore-attempt][durability]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 0x2101;

    PartyQuestReplicaWorkspacePublicationCapability missingCapability;
    const auto unauthorized =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, missingCapability);
    REQUIRE(unauthorized.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::WorkspaceCapabilityRequired);

    PartyQuestReplicaWorkspaceLease lease;
    const auto capability = AcquireCapability(paths, lease);
    const auto initialized =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, capability);

#ifdef _WIN32
    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetStatePath(paths, transactionId)));
#else
    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(initialized.State.has_value());
    REQUIRE(initialized.State->TransactionId == transactionId);
    REQUIRE(initialized.State->CurrentOrdinal == 0);
    REQUIRE(initialized.RestoreId == 1);
    REQUIRE(initialized.JournalPath ==
        PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(paths, transactionId, 0));

    const auto reloaded = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths, kCampaign, kPlayer, transactionId);
    REQUIRE(reloaded.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(reloaded.State == initialized.State);

    const auto repeated =
        PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
            paths, kCampaign, kPlayer, transactionId, capability);
    REQUIRE(repeated.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(repeated.State == initialized.State);
#endif
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

#ifdef _WIN32
    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(initialized.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    PersistJournal(
        paths,
        transactionId,
        0,
        PartyQuestReplicaRestoreJournalPhase::RolledBack);

    const auto advanced =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 0, capability);
    REQUIRE(advanced.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(advanced.State.has_value());
    REQUIRE(advanced.State->CurrentOrdinal == 1);
    REQUIRE(advanced.RestoreId == 2);

    const auto afterRestart = PartyQuestRuntimeRestoreAttemptStore::Load(
        paths, kCampaign, kPlayer, transactionId);
    REQUIRE(afterRestart.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(afterRestart.State.has_value());
    REQUIRE(afterRestart.State->CurrentOrdinal == 1);

    const auto replay =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 0, capability);
    REQUIRE(replay.Status == PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced);
    REQUIRE(replay.State.has_value());
    REQUIRE(replay.State->CurrentOrdinal == 1);

    const auto noSecondJournal =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 1, capability);
    REQUIRE(noSecondJournal.Status == PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);
    REQUIRE(noSecondJournal.State.has_value());
    REQUIRE(noSecondJournal.State->CurrentOrdinal == 1);

    PersistJournal(
        paths,
        transactionId,
        1,
        PartyQuestReplicaRestoreJournalPhase::Prepared);
    const auto nonterminal =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            paths, kCampaign, kPlayer, transactionId, 1, capability);
    REQUIRE(nonterminal.Status == PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch);
    REQUIRE(nonterminal.State.has_value());
    REQUIRE(nonterminal.State->CurrentOrdinal == 1);
#endif
}
