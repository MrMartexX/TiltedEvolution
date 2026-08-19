#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <catch2/catch.hpp>

#include <filesystem>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x1112131415161718ull,
    0x2122232425262728ull};
const PartyQuestPlayerProfileId kPlayer{
    0x3132333435363738ull,
    0x4142434445464748ull};

PartyQuestReplicaRestoreJournalState MakeState(
    PartyQuestReplicaRestoreJournalPhase aPhase)
{
    const auto root = std::filesystem::temp_directory_path() /
        "tp_party_quest_restore_journal_terminal";

    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = 0x70010001;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 7001;
    state.Phase = aPhase;
    state.TransactionDirectory =
        root / "metadata" / "restore" / "Transaction_0000000070010001";

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath = root / "checkpoint" / "Hero.ess";
    operation.ReplicaDestinationPath = root / "saves" / "Hero.ess";
    operation.RollbackPath =
        state.TransactionDirectory / "rollback" / "saves" / "Hero.ess";
    operation.ExpectedRestoredSize = 17;
    operation.ExpectedRestoredDigest = 0x5152535455565758ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 19;
    operation.OriginalDigest = 0x6162636465666768ull;
    state.Operations.push_back(std::move(operation));
    return state;
}
} // namespace

TEST_CASE(
    "restore journal v2 preserves v1 phase meanings and reads legacy archives",
    "[quest.party-state.replica-restore][journal][compatibility]")
{
    const auto state = MakeState(PartyQuestReplicaRestoreJournalPhase::Committed);
    const auto v2 = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE(v2.size() > 10);
    REQUIRE(v2[8] == 2);
    REQUIRE(v2[9] == 0);

    const auto current = PartyQuestReplicaRestoreJournalPersistence::Decode(v2);
    REQUIRE(current.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(current.State.has_value());
    REQUIRE(*current.State == state);

    auto v1 = v2;
    v1[8] = 1;
    v1[9] = 0;
    const auto legacy = PartyQuestReplicaRestoreJournalPersistence::Decode(v1);
    REQUIRE(legacy.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacy.State.has_value());
    REQUIRE(*legacy.State == state);
}

TEST_CASE(
    "RolledBack is v2-only terminal recovery state",
    "[quest.party-state.replica-restore][journal][rollback-terminal]")
{
    const auto state = MakeState(PartyQuestReplicaRestoreJournalPhase::RolledBack);
    const auto v2 = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE_FALSE(v2.empty());

    const auto current = PartyQuestReplicaRestoreJournalPersistence::Decode(v2);
    REQUIRE(current.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(current.State.has_value());
    REQUIRE(current.State->Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*current.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RolledBackClean);

    auto forgedLegacy = v2;
    forgedLegacy[8] = 1;
    forgedLegacy[9] = 0;
    const auto rejected =
        PartyQuestReplicaRestoreJournalPersistence::Decode(forgedLegacy);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData);
    REQUIRE_FALSE(rejected.State.has_value());
}
