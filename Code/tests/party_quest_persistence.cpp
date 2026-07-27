#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
QuestSnapshot BuildPersistentSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.SceneParticipantPlayerId = 12;
    snapshot.CompletedStages = {aStage, 10, 20};
    snapshot.Objectives = {
        {30, QuestObjectiveState::Displayed},
        {10, QuestObjectiveState::Completed},
        {20, QuestObjectiveState::Hidden}
    };
    snapshot.ReferenceAliases = {
        {7, GameId(1, 0x220), true},
        {2, std::nullopt, false}
    };
    snapshot.LocationAliases = {
        {5, GameId(1, 0x330)},
        {1, std::nullopt}
    };
    snapshot.CreatedReferences = {
        GameId(2, 0x440),
        GameId(2, 0x441)
    };
    return snapshot;
}

PartyQuestTransaction BuildPersistentTransaction(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage,
    uint32_t aInitiator = 7)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = aInitiator;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildPersistentSnapshot(aQuestId, aStage);
    return transaction;
}

PartyQuestState BuildPersistentState()
{
    PartyQuestState state;
    const GameId firstQuest(1, 0x1000);
    const GameId secondQuest(2, 0x2000);

    REQUIRE(state.Apply(BuildPersistentTransaction(10001, firstQuest, 0, 10, 3)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildPersistentTransaction(10002, secondQuest, 0, 40, 4)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildPersistentTransaction(10003, firstQuest, 1, 30, 5)).Status == PartyQuestApplyStatus::Accepted);
    return state;
}

void RemoveArchiveFiles(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);

    auto backup = acPath;
    backup += ".bak";
    std::filesystem::remove(backup, ec);

    auto temporary = acPath;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
}
} // namespace

TEST_CASE("Party quest persistence round-trips checkpoint and journal", "[quest.party-state.persistence]")
{
    PartyQuestState original = BuildPersistentState();
    const auto firstEncoding = PartyQuestStatePersistence::Encode(original);
    const auto secondEncoding = PartyQuestStatePersistence::Encode(original);

    REQUIRE(firstEncoding == secondEncoding);

    auto loaded = PartyQuestStatePersistence::Decode(firstEncoding);
    REQUIRE(loaded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE_FALSE(loaded.UsedBackup);
    REQUIRE(loaded.State->GetWorldRevision() == original.GetWorldRevision());
    REQUIRE(loaded.State->GetQuestCount() == original.GetQuestCount());
    REQUIRE(loaded.State->GetJournal() == original.GetJournal());

    for (const auto& [questId, snapshot] : original.GetQuests())
    {
        const QuestSnapshot* pLoadedSnapshot = loaded.State->FindQuest(questId);
        REQUIRE(pLoadedSnapshot != nullptr);
        REQUIRE(*pLoadedSnapshot == snapshot);
    }

    const QuestSnapshot* pFirstQuest = loaded.State->FindQuest(GameId(1, 0x1000));
    REQUIRE(pFirstQuest != nullptr);
    REQUIRE(pFirstQuest->InitiatorPlayerId == 5);

    const auto duplicate = loaded.State->Apply(original.GetJournal().back().Transaction);
    REQUIRE(duplicate.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE(duplicate.WorldRevision == original.GetWorldRevision());
}

TEST_CASE("Party quest persistence rejects corrupted and truncated archives", "[quest.party-state.persistence]")
{
    const auto encoded = PartyQuestStatePersistence::Encode(BuildPersistentState());

    auto corrupted = encoded;
    REQUIRE(corrupted.size() > 20);
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestStatePersistence::Decode(corrupted).Status == PartyQuestPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestStatePersistence::Decode(truncated).Status == PartyQuestPersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestStatePersistence::Decode(unsupported).Status == PartyQuestPersistenceStatus::UnsupportedVersion);
}

TEST_CASE("Party quest persistence atomically saves and recovers the previous archive", "[quest.party-state.persistence]")
{
    const auto uniqueSuffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_state_" + std::to_string(uniqueSuffix) + ".bin");
    RemoveArchiveFiles(path);

    PartyQuestState state = BuildPersistentState();
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(path, state) == PartyQuestPersistenceStatus::Success);

    auto firstLoad = PartyQuestStatePersistence::Load(path);
    REQUIRE(firstLoad.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(firstLoad.State.has_value());
    REQUIRE(firstLoad.State->GetWorldRevision() == 3);
    REQUIRE_FALSE(firstLoad.UsedBackup);

    REQUIRE(state.Apply(BuildPersistentTransaction(10004, GameId(2, 0x2000), 1, 50, 9)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(path, state) == PartyQuestPersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    auto recovered = PartyQuestStatePersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.UsedBackup);
    REQUIRE(recovered.State->GetWorldRevision() == 3);

    RemoveArchiveFiles(path);
}
