#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xA731A731A731A731ull,
    0xB842B842B842B842ull};
const PartyQuestPlayerProfileId kPlayer{
    0xC953C953C953C953ull,
    0xDA64DA64DA64DA64ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_attempt_limit_tombstone_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~Sandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestReplicaWorkspacePublicationCapability AcquireCapability(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaWorkspaceLease& aLease)
{
    REQUIRE(aLease.Acquire(acPaths, kCampaign, kPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = aLease.CreatePublicationCapability(
        acPaths,
        kCampaign,
        kPlayer);
    REQUIRE(capability.Protects(acPaths, kCampaign, kPlayer));
    return capability;
}

void PersistRolledBackJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint32_t aOrdinal)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = aRestoreId;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 21000 + aOrdinal;
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
    operation.RollbackPath =
        state.TransactionDirectory / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 10;
    operation.ExpectedRestoredDigest = 0x5555555555555555ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 11;
    operation.OriginalDigest = 0x6666666666666666ull;
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
} // namespace

TEST_CASE(
    "restore attempt exhaustion requires the current terminal rollback tombstone",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][limit][tombstone]")
{
    Sandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 29311;
    constexpr uint64_t nextTransactionId = 29312;

    PartyQuestReplicaWorkspaceLease lease;
    const auto capability = AcquireCapability(*paths, lease);
    auto current = PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        *paths,
        kCampaign,
        kPlayer,
        transactionId,
        capability);

    REQUIRE(current.IsUsable());
    REQUIRE(current.State.has_value());

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
                kCampaign,
                kPlayer,
                transactionId,
                ordinal,
                capability);
        REQUIRE(current.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
        REQUIRE(current.State.has_value());
    }

    REQUIRE(current.State.has_value());
    REQUIRE(current.State->CurrentOrdinal + 1 ==
        PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction);
    const uint32_t terminalOrdinal = current.State->CurrentOrdinal;
    const uint64_t terminalRestoreId = current.State->CurrentRestoreId;
    const auto terminalJournalPath =
        PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
            *paths,
            terminalRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(terminalJournalPath));

    const auto missingTerminal =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            *paths,
            kCampaign,
            kPlayer,
            transactionId,
            terminalOrdinal,
            capability);
    REQUIRE(missingTerminal.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);
    REQUIRE(missingTerminal.State == current.State);
    REQUIRE(missingTerminal.RestoreId == terminalRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(terminalJournalPath));

    const auto afterMissing = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kCampaign,
        kPlayer,
        transactionId);
    REQUIRE(afterMissing.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(afterMissing.State == current.State);

    PersistRolledBackJournal(*paths, terminalRestoreId, terminalOrdinal);
    const auto limited =
        PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
            *paths,
            kCampaign,
            kPlayer,
            transactionId,
            terminalOrdinal,
            capability);
    REQUIRE(limited.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::AttemptLimitReached);
    REQUIRE(limited.State == current.State);
    REQUIRE(limited.RestoreId == terminalRestoreId);

    const auto next = PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        *paths,
        kCampaign,
        kPlayer,
        nextTransactionId,
        capability);
    REQUIRE(next.Status == PartyQuestRuntimeRestoreAttemptStatus::Created);
    REQUIRE(next.State.has_value());
    REQUIRE(next.State->CurrentRestoreId == terminalRestoreId + 1);
}
