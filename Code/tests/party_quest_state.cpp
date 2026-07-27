#include <Structs/Skyrim/PartyQuestState.h>

#include <catch2/catch.hpp>

namespace
{
QuestSnapshot BuildQuestSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {aStage, 10};
    snapshot.Objectives = {
        {20, QuestObjectiveState::Displayed},
        {10, QuestObjectiveState::Completed}
    };
    return snapshot;
}

PartyQuestTransaction BuildTransaction(uint64_t aTransactionId, GameId aQuestId, uint64_t aExpectedRevision, uint16_t aStage)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = 7;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildQuestSnapshot(aQuestId, aStage);
    return transaction;
}
} // namespace

TEST_CASE("Party quest state assigns canonical world and quest revisions", "[quest.party-state]")
{
    PartyQuestState state;
    const GameId questId(1, 0x12345);

    const auto first = state.Apply(BuildTransaction(1001, questId, 0, 20));
    REQUIRE(first.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(first.WorldRevision == 1);
    REQUIRE(first.QuestRevision == 1);

    const QuestSnapshot* pFirstSnapshot = state.FindQuest(questId);
    REQUIRE(pFirstSnapshot != nullptr);
    REQUIRE(pFirstSnapshot->Revision == 1);
    REQUIRE(pFirstSnapshot->CurrentStage == 20);

    const auto second = state.Apply(BuildTransaction(1002, questId, 1, 30));
    REQUIRE(second.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(second.WorldRevision == 2);
    REQUIRE(second.QuestRevision == 2);
    REQUIRE(state.FindQuest(questId)->Revision == 2);
    REQUIRE(state.FindQuest(questId)->CurrentStage == 30);
    REQUIRE(state.GetJournal().size() == 2);
}

TEST_CASE("Party quest state makes repeated delivery idempotent", "[quest.party-state]")
{
    PartyQuestState state;
    const auto transaction = BuildTransaction(2001, GameId(1, 0x200), 0, 10);

    REQUIRE(state.Apply(transaction).Status == PartyQuestApplyStatus::Accepted);
    const auto duplicate = state.Apply(transaction);

    REQUIRE(duplicate.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE(duplicate.WorldRevision == 1);
    REQUIRE(duplicate.QuestRevision == 1);
    REQUIRE(state.GetJournal().size() == 1);
}

TEST_CASE("Party quest state rejects transaction id reuse with another payload", "[quest.party-state]")
{
    PartyQuestState state;
    const GameId questId(1, 0x300);

    REQUIRE(state.Apply(BuildTransaction(3001, questId, 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    const auto conflict = state.Apply(BuildTransaction(3001, questId, 1, 20));

    REQUIRE(conflict.Status == PartyQuestApplyStatus::TransactionConflict);
    REQUIRE(state.GetWorldRevision() == 1);
    REQUIRE(state.FindQuest(questId)->CurrentStage == 10);
    REQUIRE(state.GetJournal().size() == 1);
}

TEST_CASE("Party quest state rejects stale expected revisions", "[quest.party-state]")
{
    PartyQuestState state;
    const GameId questId(2, 0x400);

    REQUIRE(state.Apply(BuildTransaction(4001, questId, 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    const auto stale = state.Apply(BuildTransaction(4002, questId, 0, 20));

    REQUIRE(stale.Status == PartyQuestApplyStatus::RevisionMismatch);
    REQUIRE(stale.QuestRevision == 1);
    REQUIRE(state.GetWorldRevision() == 1);
    REQUIRE(state.FindQuest(questId)->CurrentStage == 10);
}

TEST_CASE("Party quest state keeps independent per-quest revisions", "[quest.party-state]")
{
    PartyQuestState state;
    const GameId firstQuest(1, 0x500);
    const GameId secondQuest(1, 0x501);

    REQUIRE(state.Apply(BuildTransaction(5001, firstQuest, 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildTransaction(5002, secondQuest, 0, 40)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildTransaction(5003, firstQuest, 1, 20)).Status == PartyQuestApplyStatus::Accepted);

    REQUIRE(state.GetWorldRevision() == 3);
    REQUIRE(state.FindQuest(firstQuest)->Revision == 2);
    REQUIRE(state.FindQuest(secondQuest)->Revision == 1);
}

TEST_CASE("Party quest journal deterministically replays accepted transactions", "[quest.party-state]")
{
    PartyQuestState original;
    const GameId firstQuest(3, 0x600);
    const GameId secondQuest(3, 0x601);

    REQUIRE(original.Apply(BuildTransaction(6001, firstQuest, 0, 10)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(original.Apply(BuildTransaction(6002, secondQuest, 0, 50)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(original.Apply(BuildTransaction(6003, firstQuest, 1, 30)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestState replayed;
    for (const auto& entry : original.GetJournal())
        REQUIRE(replayed.Apply(entry.Transaction).Status == PartyQuestApplyStatus::Accepted);

    REQUIRE(replayed.GetWorldRevision() == original.GetWorldRevision());
    REQUIRE(replayed.FindQuest(firstQuest) != nullptr);
    REQUIRE(replayed.FindQuest(secondQuest) != nullptr);
    REQUIRE(*replayed.FindQuest(firstQuest) == *original.FindQuest(firstQuest));
    REQUIRE(*replayed.FindQuest(secondQuest) == *original.FindQuest(secondQuest));
}

TEST_CASE("Party quest state rejects mismatched quest identifiers", "[quest.party-state]")
{
    PartyQuestState state;
    auto transaction = BuildTransaction(7001, GameId(1, 0x700), 0, 10);
    transaction.ProposedSnapshot.QuestId = GameId(1, 0x701);

    const auto result = state.Apply(transaction);
    REQUIRE(result.Status == PartyQuestApplyStatus::QuestIdMismatch);
    REQUIRE(state.GetWorldRevision() == 0);
    REQUIRE(state.GetQuestCount() == 0);
}

TEST_CASE("Party quest state rejects zero transaction identifiers", "[quest.party-state]")
{
    PartyQuestState state;
    const auto result = state.Apply(BuildTransaction(0, GameId(1, 0x800), 0, 10));

    REQUIRE(result.Status == PartyQuestApplyStatus::InvalidTransactionId);
    REQUIRE(state.GetWorldRevision() == 0);
    REQUIRE(state.GetJournal().empty());
}
