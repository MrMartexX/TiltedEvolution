#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xB1B2B3B4B5B6B7B8ull,
    0x0102030405060708ull};
const PartyQuestPlayerProfileId kPlayer{
    0xC1C2C3C4C5C6C7C8ull,
    0x1112131415161718ull};

struct RecoverySandbox
{
    std::filesystem::path Root;
    std::filesystem::path Transaction;
    std::filesystem::path Journal;

    RecoverySandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_durable_recovery_" + std::to_string(nonce));
        Transaction = Root / "restore" / "Transaction_1";
        Journal = Transaction / "journal.bin";
        std::error_code ec;
        std::filesystem::create_directories(Transaction / "rollback", ec);
        REQUIRE_FALSE(ec);
    }

    ~RecoverySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestReplicaRestoreJournalState BuildState(
    const RecoverySandbox& acSandbox,
    PartyQuestReplicaRestoreJournalPhase aPhase,
    uint64_t aRevision)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = 0x7000 + aRevision;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = aRevision;
    state.Phase = aPhase;
    state.TransactionDirectory = acSandbox.Transaction;

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath = acSandbox.Root / "checkpoint" / "Hero.ess";
    operation.ReplicaDestinationPath = acSandbox.Root / "replica" / "Hero.ess";
    operation.RollbackPath = acSandbox.Transaction / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 4;
    operation.ExpectedRestoredDigest = 0xAA00 + aRevision;
    state.Operations.push_back(std::move(operation));
    REQUIRE_FALSE(PartyQuestReplicaRestoreJournalPersistence::Encode(state).empty());
    return state;
}

struct Fault
{
    PartyQuestReplicaRestoreJournalPersistenceBoundary Boundary;
};

PartyQuestReplicaRestoreJournalPersistenceDirective FailAt(
    PartyQuestReplicaRestoreJournalPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const Fault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed
        : PartyQuestReplicaRestoreJournalPersistenceDirective::Continue;
}

std::filesystem::path TemporaryPath(std::filesystem::path aPath)
{
    aPath += ".tmp";
    return aPath;
}
}

TEST_CASE(
    "durable restore recovery promotes valid temporary with the stable rename primitive",
    "[quest.party-state.replica-restore][durability][recovery]")
{
    RecoverySandbox sandbox;
    const auto oldState = BuildState(
        sandbox,
        PartyQuestReplicaRestoreJournalPhase::Prepared,
        4101);
    const auto newState = BuildState(
        sandbox,
        PartyQuestReplicaRestoreJournalPhase::MutationStarted,
        4102);

    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                sandbox.Journal,
                oldState) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    Fault fault{
        PartyQuestReplicaRestoreJournalPersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newState,
                {FailAt, &fault}) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(sandbox.Journal));
    REQUIRE(std::filesystem::exists(TemporaryPath(sandbox.Journal)));

    const auto recovered =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            sandbox.Journal);
    REQUIRE(recovered.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(*recovered.State == newState);
    REQUIRE(recovered.UsedTemporary);
    REQUIRE(std::filesystem::exists(sandbox.Journal));
    REQUIRE_FALSE(std::filesystem::exists(TemporaryPath(sandbox.Journal)));
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*recovered.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable restore recovery preserves conflicting invalid primary and valid temporary",
    "[quest.party-state.replica-restore][durability][recovery][fail-closed]")
{
    RecoverySandbox sandbox;
    const auto oldState = BuildState(
        sandbox,
        PartyQuestReplicaRestoreJournalPhase::Prepared,
        4201);
    const auto newState = BuildState(
        sandbox,
        PartyQuestReplicaRestoreJournalPhase::BackupsReady,
        4202);

    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                sandbox.Journal,
                oldState) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    Fault fault{
        PartyQuestReplicaRestoreJournalPersistenceBoundary::TemporaryVerified};
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newState,
                {FailAt, &fault}) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::IoError);
    REQUIRE(std::filesystem::exists(sandbox.Journal));
    REQUIRE(std::filesystem::exists(TemporaryPath(sandbox.Journal)));

    {
        std::ofstream corrupt(sandbox.Journal, std::ios::binary | std::ios::trunc);
        REQUIRE(corrupt.is_open());
        corrupt.write("bad", 3);
        corrupt.flush();
        REQUIRE(corrupt.good());
    }

    const auto recovered =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            sandbox.Journal);
    REQUIRE(recovered.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::IoError);
    REQUIRE_FALSE(recovered.State.has_value());
    REQUIRE(std::filesystem::exists(sandbox.Journal));
    REQUIRE(std::filesystem::exists(TemporaryPath(sandbox.Journal)));

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
