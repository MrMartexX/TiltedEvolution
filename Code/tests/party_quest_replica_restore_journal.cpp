#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kRestoreCampaign{0x1234567890ABCDEFull, 0x1111222233334444ull};
const PartyQuestPlayerProfileId kRestorePlayer{0xAAAABBBBCCCCDDDDull, 0x5555666677778888ull};

struct RestoreSandbox
{
    std::filesystem::path Root;

    RestoreSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_journal_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RestoreSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteRestoreBytes(const std::filesystem::path& acPath, const std::string& acBytes)
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

PartyQuestCoopSavePaths BuildRestorePaths(const RestoreSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns", kRestoreCampaign, kRestorePlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestReplicaRestorePlan BuildRestorePlan(
    const PartyQuestCoopSavePaths& acPaths,
    const std::filesystem::path& acCheckpointSource,
    const std::filesystem::path& acDestination,
    uint64_t aWorldRevision)
{
    const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(acCheckpointSource);
    REQUIRE(observation.has_value());

    PartyQuestReplicaRestorePlan plan;
    plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
    plan.CampaignId = kRestoreCampaign;
    plan.PlayerProfileId = kRestorePlayer;
    plan.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    plan.CampaignWorldRevision = aWorldRevision;
    plan.Operations.push_back({
        PartyQuestReplicaFileKind::SkyrimSave,
        acCheckpointSource,
        acDestination,
        observation->Size,
        observation->Digest});
    return plan;
}
} // namespace

TEST_CASE("Restore journal crosses the mutation barrier only after verified rollback bytes", "[quest.party-state.restore-journal]")
{
    RestoreSandbox sandbox;
    const auto paths = BuildRestorePaths(sandbox);
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, 800) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRestoreBytes(checkpoint, "CANONICAL_CHECKPOINT_BYTES");
    WriteRestoreBytes(destination, "CURRENT_REPLICA_BYTES");

    const auto plan = BuildRestorePlan(paths, checkpoint, destination, 800);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 42);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    REQUIRE(state.Phase == PartyQuestReplicaRestoreJournalPhase::Prepared);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(state) ==
        PartyQuestReplicaRestoreRecoveryDisposition::ResumeBeforeMutation);
    REQUIRE_FALSE(PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(state));
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::BackupVerificationFailed);

    REQUIRE(state.Operations.size() == 1);
    WriteRestoreBytes(state.Operations[0].RollbackPath, "CURRENT_REPLICA_BYTES");
    REQUIRE(PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(state));
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(state) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired);

    WriteRestoreBytes(destination, "CANONICAL_CHECKPOINT_BYTES");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkRestored(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(state) ==
        PartyQuestReplicaRestoreRecoveryDisposition::VerifyThenCommit);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkCommitted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(state) ==
        PartyQuestReplicaRestoreRecoveryDisposition::Clean);
}

TEST_CASE("Restore journal records an originally absent destination without inventing rollback bytes", "[quest.party-state.restore-journal]")
{
    RestoreSandbox sandbox;
    const auto paths = BuildRestorePaths(sandbox);
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, 801) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRestoreBytes(checkpoint, "NEW_REPLICA_BYTES");

    const auto plan = BuildRestorePlan(paths, checkpoint, destination, 801);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 43);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    REQUIRE_FALSE(state.Operations[0].DestinationExisted);
    REQUIRE(PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(state));
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
}

TEST_CASE("Restore journal persistence round-trips identity phase and recovery disposition", "[quest.party-state.restore-journal]")
{
    RestoreSandbox sandbox;
    const auto paths = BuildRestorePaths(sandbox);
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, 802) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRestoreBytes(checkpoint, "CHECKPOINT_802");
    WriteRestoreBytes(destination, "BEFORE_802");

    const auto plan = BuildRestorePlan(paths, checkpoint, destination, 802);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 44);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    WriteRestoreBytes(state.Operations[0].RollbackPath, "BEFORE_802");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);

    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.State == state);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*loaded.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired);
}

TEST_CASE("Restore journal rejects oversized archives before decode", "[quest.party-state.replica-restore-journal]")
{
    std::vector<uint8_t> oversized(
        PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes + 1,
        0);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::Decode(oversized).Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded);
}

TEST_CASE("Restore journal never silently falls back to a stale backup across the mutation barrier", "[quest.party-state.restore-journal]")
{
    RestoreSandbox sandbox;
    const auto paths = BuildRestorePaths(sandbox);
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, 803) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRestoreBytes(checkpoint, "CHECKPOINT_803");
    WriteRestoreBytes(destination, "BEFORE_803");

    const auto plan = BuildRestorePlan(paths, checkpoint, destination, 803);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 45);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    WriteRestoreBytes(state.Operations[0].RollbackPath, "BEFORE_803");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);

    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);

    WriteRestoreBytes(journalPath, "CORRUPTED_CURRENT_JOURNAL");
    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady);
}

TEST_CASE("Restore journal can recover a fully written temporary state instead of using stale backup", "[quest.party-state.restore-journal]")
{
    RestoreSandbox sandbox;
    const auto paths = BuildRestorePaths(sandbox);
    const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        paths, PartyQuestCheckpointKind::PreRepair, 804) / "saves" / "Hero.ess";
    const auto destination = paths.SavesDirectory / "Hero.ess";
    WriteRestoreBytes(checkpoint, "CHECKPOINT_804");
    WriteRestoreBytes(destination, "BEFORE_804");

    const auto plan = BuildRestorePlan(paths, checkpoint, destination, 804);
    auto prepared = PartyQuestReplicaRestoreJournal::Prepare(paths, plan, 46);
    REQUIRE(prepared.IsReady());
    auto state = *prepared.State;
    WriteRestoreBytes(state.Operations[0].RollbackPath, "BEFORE_804");
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkBackupsReady(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);

    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(PartyQuestReplicaRestoreJournal::MarkMutationStarted(state) ==
        PartyQuestReplicaRestoreJournalStatus::Ready);

    std::filesystem::path temporary = journalPath;
    temporary += ".tmp";
    const auto encoded = PartyQuestReplicaRestoreJournalPersistence::Encode(state);
    REQUIRE_FALSE(encoded.empty());
    std::error_code ec;
    std::filesystem::create_directories(temporary.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    file.flush();
    REQUIRE(file.good());
    file.close();

    WriteRestoreBytes(journalPath, "CORRUPTED_PRIMARY");
    const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(journalPath);
    REQUIRE(loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(loaded.UsedTemporary);
    REQUIRE(loaded.State == state);
    REQUIRE(PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(*loaded.State) ==
        PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired);
}
