#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x1112131415161718ull,
    0x2122232425262728ull};
const PartyQuestPlayerProfileId kPlayer{
    0x3132333435363738ull,
    0x4142434445464748ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_journal_terminal_" + std::to_string(nonce));
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

PartyQuestReplicaRestoreJournalState MakeState(
    PartyQuestReplicaRestoreJournalPhase aPhase,
    const std::filesystem::path& acRoot =
        std::filesystem::temp_directory_path() /
            "tp_party_quest_restore_journal_terminal")
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.RestoreId = 0x70010001;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = 7001;
    state.Phase = aPhase;
    state.TransactionDirectory =
        acRoot / "metadata" / "restore" / "Transaction_0000000070010001";

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath = acRoot / "checkpoint" / "Hero.ess";
    operation.ReplicaDestinationPath = acRoot / "saves" / "Hero.ess";
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

void WriteBytes(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(
        reinterpret_cast<const char*>(acBytes.data()),
        static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}
} // namespace

TEST_CASE(
    "restore journal v3 identifies process-crash protocol and v1-v2 stay ambiguous",
    "[quest.party-state.replica-restore][journal][compatibility][durability-domain]")
{
    const auto state = MakeState(PartyQuestReplicaRestoreJournalPhase::Committed);
    const auto v3 = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE(v3.size() > 10);
    REQUIRE(v3[8] == 3);
    REQUIRE(v3[9] == 0);

    const auto current = PartyQuestReplicaRestoreJournalPersistence::Decode(v3);
    REQUIRE(current.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(current.State.has_value());
    REQUIRE(*current.State == state);
    REQUIRE(current.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);

    auto v2 = v3;
    v2[8] = 2;
    v2[9] = 0;
    const auto rolledBackEra = PartyQuestReplicaRestoreJournalPersistence::Decode(v2);
    REQUIRE(rolledBackEra.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(rolledBackEra.State.has_value());
    REQUIRE(*rolledBackEra.State == state);
    REQUIRE(rolledBackEra.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding);

    auto v1 = v3;
    v1[8] = 1;
    v1[9] = 0;
    const auto legacy = PartyQuestReplicaRestoreJournalPersistence::Decode(v1);
    REQUIRE(legacy.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(legacy.State.has_value());
    REQUIRE(*legacy.State == state);
    REQUIRE(legacy.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding);
}

TEST_CASE(
    "RolledBack keeps phase compatibility without granting ambiguous archives an executor",
    "[quest.party-state.replica-restore][journal][rollback-terminal][durability-domain]")
{
    const auto state = MakeState(PartyQuestReplicaRestoreJournalPhase::RolledBack);
    const auto v3 = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE_FALSE(v3.empty());

    const auto current = PartyQuestReplicaRestoreJournalPersistence::Decode(v3);
    REQUIRE(current.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(current.State.has_value());
    REQUIRE(current.State->Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(current.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*current.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RolledBackClean);

    auto v2 = v3;
    v2[8] = 2;
    v2[9] = 0;
    const auto ambiguous = PartyQuestReplicaRestoreJournalPersistence::Decode(v2);
    REQUIRE(ambiguous.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(ambiguous.State.has_value());
    REQUIRE(ambiguous.State->Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack);
    REQUIRE(ambiguous.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding);

    auto forgedLegacy = v3;
    forgedLegacy[8] = 1;
    forgedLegacy[9] = 0;
    const auto rejected =
        PartyQuestReplicaRestoreJournalPersistence::Decode(forgedLegacy);
    REQUIRE(rejected.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData);
    REQUIRE_FALSE(rejected.State.has_value());
}

TEST_CASE(
    "legacy loader refuses an explicit power-loss restore journal",
    "[quest.party-state.replica-restore][journal][durability-domain][fail-closed]")
{
    Sandbox sandbox;
    const auto state = MakeState(
        PartyQuestReplicaRestoreJournalPhase::Prepared,
        sandbox.Root);
    std::error_code ec;
    std::filesystem::create_directories(state.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    const auto path = PartyQuestReplicaRestoreJournal::GetJournalPath(state);

    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                path,
                state) == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto strong =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(path);
    REQUIRE(strong.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(strong.State.has_value());
    REQUIRE(strong.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable);

    const auto legacy = PartyQuestReplicaRestoreJournalPersistence::Load(path);
    REQUIRE(legacy.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityMismatch);
    REQUIRE(legacy.State.has_value());
    REQUIRE(legacy.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable);
    REQUIRE(std::filesystem::exists(path));
}

TEST_CASE(
    "strong loader refuses process-crash and ambiguous legacy restore journals",
    "[quest.party-state.replica-restore][journal][durability-domain][migration]")
{
    Sandbox sandbox;
    const auto state = MakeState(
        PartyQuestReplicaRestoreJournalPhase::Prepared,
        sandbox.Root);
    std::error_code ec;
    std::filesystem::create_directories(state.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    const auto path = PartyQuestReplicaRestoreJournal::GetJournalPath(state);

    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(path, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    const auto process = PartyQuestReplicaRestoreJournalPersistence::Load(path);
    REQUIRE(process.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(process.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);

    const auto wrongStrong =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(path);
    REQUIRE(wrongStrong.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityMismatch);
    REQUIRE(wrongStrong.State.has_value());
    REQUIRE(std::filesystem::exists(path));

    auto ambiguousBytes = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE(ambiguousBytes.size() > 10);
    ambiguousBytes[8] = 2;
    ambiguousBytes[9] = 0;
    WriteBytes(path, ambiguousBytes);

    const auto ambiguousLegacy = PartyQuestReplicaRestoreJournalPersistence::Load(path);
    REQUIRE(ambiguousLegacy.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityAmbiguous);
    REQUIRE(ambiguousLegacy.State.has_value());
    REQUIRE(std::filesystem::exists(path));

    const auto ambiguousStrong =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(path);
    REQUIRE(ambiguousStrong.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityAmbiguous);
    REQUIRE(ambiguousStrong.State.has_value());
    REQUIRE(std::filesystem::exists(path));
}
