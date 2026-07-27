#include <Structs/Skyrim/PartyQuestState.h>

namespace
{
uint64_t GetQuestRevision(const PartyQuestState& acState, const GameId& acQuestId) noexcept
{
    const QuestSnapshot* pQuest = acState.FindQuest(acQuestId);
    return pQuest ? pQuest->Revision : 0;
}
} // namespace

PartyQuestApplyResult PartyQuestState::Apply(const PartyQuestTransaction& acTransaction)
{
    if (acTransaction.TransactionId == 0)
        return {PartyQuestApplyStatus::InvalidTransactionId, m_worldRevision, GetQuestRevision(*this, acTransaction.QuestId)};

    const auto transactionIt = m_transactionJournalIndices.find(acTransaction.TransactionId);
    if (transactionIt != m_transactionJournalIndices.end())
    {
        const PartyQuestJournalEntry& entry = m_journal[transactionIt->second];
        const auto status = entry.Transaction == acTransaction
            ? PartyQuestApplyStatus::Duplicate
            : PartyQuestApplyStatus::TransactionConflict;
        return {status, m_worldRevision, entry.QuestRevision};
    }

    if (acTransaction.QuestId != acTransaction.ProposedSnapshot.QuestId)
        return {PartyQuestApplyStatus::QuestIdMismatch, m_worldRevision, GetQuestRevision(*this, acTransaction.QuestId)};

    const uint64_t currentQuestRevision = GetQuestRevision(*this, acTransaction.QuestId);
    if (acTransaction.ExpectedQuestRevision != currentQuestRevision)
        return {PartyQuestApplyStatus::RevisionMismatch, m_worldRevision, currentQuestRevision};

    QuestSnapshot canonicalSnapshot = acTransaction.ProposedSnapshot;
    canonicalSnapshot.Revision = currentQuestRevision + 1;
    canonicalSnapshot.Canonicalize();

    ++m_worldRevision;
    m_quests[acTransaction.QuestId] = canonicalSnapshot;

    PartyQuestJournalEntry entry;
    entry.WorldRevision = m_worldRevision;
    entry.QuestRevision = currentQuestRevision + 1;
    entry.Transaction = acTransaction;

    m_transactionJournalIndices.emplace(acTransaction.TransactionId, m_journal.size());
    m_journal.push_back(entry);

    return {PartyQuestApplyStatus::Accepted, m_worldRevision, currentQuestRevision + 1};
}

const QuestSnapshot* PartyQuestState::FindQuest(const GameId& acQuestId) const noexcept
{
    const auto it = m_quests.find(acQuestId);
    return it != m_quests.end() ? &it->second : nullptr;
}
