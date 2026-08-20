#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kAttemptLimitCampaign{
    0xB721B721B721B721ull,
    0xC832C832C832C832ull};
const PartyQuestPlayerProfileId kAttemptLimitPlayer{
    0xD943D943D943D943ull,
    0xEA54EA54EA54EA54ull};

struct AttemptLimitSandbox
{
    std::filesystem::path Root;

    AttemptLimitSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_attempt_limit_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~AttemptLimitSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestReplicaWorkspacePublicationCapability AcquireCapability(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaWorkspaceLease& aLease)
{
    REQUIRE(aLease.Acquire(
                acPaths,
                kAttemptLimitCampaign,
                kAttemptLimitPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = aLease.CreatePublicationCapability(
        acPaths,
        kAttemptLimitCampaign,
        kAttemptLimitPlayer);
    REQUIRE(capability.Protects(
        acPaths,
        kAttemptLimitCampaign,
        kAttemptLimitPlayer));
    return capability;
}

void PersistRolledBackJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint32_t aOrdinal)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kAttemptLimitCampaign;
    state.PlayerProfileId = kAttemptLimitPlayer;
    state.RestoreId = aRestoreId;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 20000 + aOrdinal;
    state.Phase = PartyQuestReplicaRestoreJournalPhase::RolledBack;
    state.TransactionDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            acPaths,
            aRestoreId);

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath =
        acPaths.CheckpointsDirectory / "PreRepair" / "source.ess";
    operation.ReplicaDestinationPath = acPaths.SavesDirectory / "Hero.ess";
    operation.RollbackPath = state.TransactionDirectory / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 10;
    operation.ExpectedRestoredDigest = 0x3333333333333333ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 11;
    operation.OriginalDigest = 0x4444444444444444ull;
    state.Operations.push_back(operation);

    std::error_code ec;
    std::filesystem::create_directories(state.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                    acPaths,
                    aRestoreId),
                state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
}

void RequireRolledBack(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId)
{
    const auto loaded =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                acPaths,
                aRestoreId));
    REQUIRE(loaded.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->RestoreId == aRestoreId);
    REQUIRE(loaded.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::RolledBack);
}
} // namespace

TEST_CASE(
    "restore attempt limit is terminal and does not consume another restore id",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][limit]")
{
    AttemptLimitSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kAttemptLimitCampaign,
        kAttemptLimitPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 29301;
    constexpr uint64_t nextTransactionId = 29302;

    PartyQuestReplicaWorkspaceLease lease;
    const auto capability = AcquireCapability(*paths, lease);
    auto current = PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        *paths,
        kAttemptLimitCampaign,
        kAttemptLimitPlayer,
        transactionId,
        capability);

#ifdef _WIN32
    REQUIRE(current.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    REQUIRE(current.IsUsable());
    REQUIRE(current.State.has_value());
    REQUIRE(current.State->CurrentOrdinal == 0);
    const uint64_t firstRestoreId = current.State->CurrentRestoreId;

    // Advance through every valid retry ordinal. Each terminal journal remains
    // a permanent tombstone while the transaction mapping moves monotonically.
    for (uint32_t ordinal = 0;
         ordinal + 1 < PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction;
         ++ordinal)
    {
        REQUIRE(current.State.has_value());
        REQUIRE(current.State->CurrentOrdinal == ordinal);
        PersistRolledBackJournal(
            *paths,
            current.State->CurrentRestoreId,
            ordinal);

        current =
            PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
                *paths,
                kAttemptLimitCampaign,
                kAttemptLimitPlayer,
                transactionId,
                ordinal,
                capability);
        REQUIRE(current.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
        REQUIRE(current.State.has_value());
        REQUIRE(current.State->CurrentOrdinal == ordinal + 1);
    }

    REQUIRE(current.State.has_value());
    REQUIRE(current.State->CurrentOrdinal + 1 ==
        PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction);
    const uint32_t terminalOrdinal = current.State->CurrentOrdinal;
    const uint64_t terminalRestoreId = current.State->CurrentRestoreId;
    PersistRolledBackJournal(*paths, terminalRestoreId, terminalOrdinal);

    const auto limited =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            *paths,
            kAttemptLimitCampaign,
            kAttemptLimitPlayer,
            transactionId,
            terminalOrdinal,
            capability);
    REQUIRE(limited.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::AttemptLimitReached);
    REQUIRE(limited.State.has_value());
    REQUIRE(limited.State == current.State);
    REQUIRE(limited.RestoreId == terminalRestoreId);

    // Replaying the same terminal evidence at the exhausted boundary must be
    // deterministic and must not consume another allocator identity.
    const auto repeated =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            *paths,
            kAttemptLimitCampaign,
            kAttemptLimitPlayer,
            transactionId,
            terminalOrdinal,
            capability);
    REQUIRE(repeated.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::AttemptLimitReached);
    REQUIRE(repeated.State == current.State);
    REQUIRE(repeated.RestoreId == terminalRestoreId);

    const auto reloaded = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kAttemptLimitCampaign,
        kAttemptLimitPlayer,
        transactionId);
    REQUIRE(reloaded.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(reloaded.State == current.State);

    RequireRolledBack(*paths, firstRestoreId);
    RequireRolledBack(*paths, terminalRestoreId);

    // A distinct server transaction proves the exhausted transaction did not
    // allocate or burn a hidden RestoreId after reaching its retry ceiling.
    const auto next = PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        *paths,
        kAttemptLimitCampaign,
        kAttemptLimitPlayer,
        nextTransactionId,
        capability);
    REQUIRE(next.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(next.State.has_value());
    REQUIRE(next.State->CurrentOrdinal == 0);
    REQUIRE(next.State->CurrentRestoreId == terminalRestoreId + 1);
    REQUIRE(next.State->CurrentRestoreId != terminalRestoreId);

    RequireRolledBack(*paths, firstRestoreId);
    RequireRolledBack(*paths, terminalRestoreId);
#endif
}
