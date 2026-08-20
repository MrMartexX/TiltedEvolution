#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kStaleAttemptCampaign{
    0xB711B711B711B711ull,
    0xC822C822C822C822ull};
const PartyQuestPlayerProfileId kStaleAttemptPlayer{
    0xD933D933D933D933ull,
    0xEA44EA44EA44EA44ull};

struct StaleAttemptSandbox
{
    std::filesystem::path Root;

    StaleAttemptSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_attempt_stale_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~StaleAttemptSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestRuntimeRestoreAttemptResult EnsureAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId)
{
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(
                acPaths,
                kStaleAttemptCampaign,
                kStaleAttemptPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer);
    REQUIRE(capability.Protects(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer));
    return PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer,
        aTransactionId,
        capability);
}

PartyQuestRuntimeRestoreAttemptResult AdvanceAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId,
    uint32_t aRolledBackOrdinal)
{
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(
                acPaths,
                kStaleAttemptCampaign,
                kStaleAttemptPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer);
    REQUIRE(capability.Protects(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer));
    return PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
        acPaths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer,
        aTransactionId,
        aRolledBackOrdinal,
        capability);
}

void PersistRolledBackJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint64_t aWorldRevision)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kStaleAttemptCampaign;
    state.PlayerProfileId = kStaleAttemptPlayer;
    state.RestoreId = aRestoreId;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = aWorldRevision;
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
    operation.ExpectedRestoredDigest = 0x1111111111111111ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 11;
    operation.OriginalDigest = 0x2222222222222222ull;
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

void RequireTerminalRolledBack(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId)
{
    const auto terminal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                acPaths,
                aRestoreId));
    REQUIRE(terminal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(terminal.State.has_value());
    REQUIRE(terminal.State->RestoreId == aRestoreId);
    REQUIRE(terminal.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::RolledBack);
}
} // namespace

TEST_CASE(
    "Superseded rollback evidence is stale after multiple durable attempt advances",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][stale-evidence]")
{
    StaleAttemptSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kStaleAttemptCampaign,
        kStaleAttemptPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 29201;
    constexpr uint64_t worldRevision = 1920;

#ifdef _WIN32
    const auto unsupported = EnsureAttempt(*paths, transactionId);
    REQUIRE(unsupported.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    const auto first = EnsureAttempt(*paths, transactionId);
    REQUIRE(first.IsUsable());
    REQUIRE(first.State.has_value());
    REQUIRE(first.State->CurrentOrdinal == 0);
    const uint64_t firstRestoreId = first.State->CurrentRestoreId;

    PersistRolledBackJournal(*paths, firstRestoreId, worldRevision);
    const auto second = AdvanceAttempt(*paths, transactionId, 0);
    REQUIRE(second.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(second.State.has_value());
    REQUIRE(second.State->CurrentOrdinal == 1);
    REQUIRE(second.State->LastRolledBackRestoreId == firstRestoreId);
    const uint64_t secondRestoreId = second.State->CurrentRestoreId;
    REQUIRE(secondRestoreId > firstRestoreId);

    PersistRolledBackJournal(*paths, secondRestoreId, worldRevision);
    const auto third = AdvanceAttempt(*paths, transactionId, 1);
    REQUIRE(third.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(third.State.has_value());
    REQUIRE(third.State->CurrentOrdinal == 2);
    REQUIRE(third.State->LastRolledBackRestoreId == secondRestoreId);
    const uint64_t thirdRestoreId = third.State->CurrentRestoreId;
    REQUIRE(thirdRestoreId > secondRestoreId);

    RequireTerminalRolledBack(*paths, firstRestoreId);
    RequireTerminalRolledBack(*paths, secondRestoreId);

    // Ordinal zero is now superseded by two durable advances. Replaying its old
    // terminal evidence must fail closed without changing the authoritative
    // mapping and, critically, without consuming another restore id.
    const auto staleReplay = AdvanceAttempt(*paths, transactionId, 0);
    REQUIRE(staleReplay.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::StaleJournal);
    REQUIRE(staleReplay.State.has_value());
    REQUIRE(staleReplay.State->CurrentOrdinal == 2);
    REQUIRE(staleReplay.State->CurrentRestoreId == thirdRestoreId);
    REQUIRE(staleReplay.State->LastRolledBackRestoreId == secondRestoreId);

    const auto afterReplay = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kStaleAttemptCampaign,
        kStaleAttemptPlayer,
        transactionId);
    REQUIRE(afterReplay.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(afterReplay.State == third.State);
    RequireTerminalRolledBack(*paths, firstRestoreId);
    RequireTerminalRolledBack(*paths, secondRestoreId);

    // A legitimate next advance must receive the immediately following id. If
    // stale replay had allocated or burned an id, this equality would fail.
    PersistRolledBackJournal(*paths, thirdRestoreId, worldRevision);
    const auto fourth = AdvanceAttempt(*paths, transactionId, 2);
    REQUIRE(fourth.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(fourth.State.has_value());
    REQUIRE(fourth.State->CurrentOrdinal == 3);
    REQUIRE(fourth.State->LastRolledBackRestoreId == thirdRestoreId);
    REQUIRE(fourth.State->CurrentRestoreId == thirdRestoreId + 1);

    RequireTerminalRolledBack(*paths, firstRestoreId);
    RequireTerminalRolledBack(*paths, secondRestoreId);
    RequireTerminalRolledBack(*paths, thirdRestoreId);
#endif
}
