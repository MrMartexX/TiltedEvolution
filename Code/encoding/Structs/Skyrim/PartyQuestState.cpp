#include <Structs/Skyrim/PartyQuestState.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

namespace
{
uint64_t GetQuestRevision(const PartyQuestState& acState, const GameId& acQuestId) noexcept
{
    const QuestSnapshot* pQuest = acState.FindQuest(acQuestId);
    return pQuest ? pQuest->Revision : 0;
}

PartyQuestTransaction CanonicalizeTransaction(const PartyQuestTransaction& acTransaction)
{
    PartyQuestTransaction transaction = acTransaction;
    transaction.ProposedSnapshot.Canonicalize();
    return transaction;
}
} // namespace

bool PartyQuestTransaction::operator==(const PartyQuestTransaction& acRhs) const
{
    if (TransactionId != acRhs.TransactionId ||
        InitiatorPlayerId != acRhs.InitiatorPlayerId ||
        QuestId != acRhs.QuestId ||
        ExpectedQuestRevision != acRhs.ExpectedQuestRevision)
    {
        return false;
    }

    QuestSnapshot left = ProposedSnapshot;
    QuestSnapshot right = acRhs.ProposedSnapshot;
    left.Canonicalize();
    right.Canonicalize();
    return left == right;
}

PartyQuestApplyResult PartyQuestState::Apply(const PartyQuestTransaction& acTransaction)
{
    if (acTransaction.TransactionId == 0)
        return {PartyQuestApplyStatus::InvalidTransactionId, m_worldRevision, GetQuestRevision(*this, acTransaction.QuestId)};

    const PartyQuestTransaction transaction = CanonicalizeTransaction(acTransaction);

    const auto transactionIt = m_transactionJournalIndices.find(transaction.TransactionId);
    if (transactionIt != m_transactionJournalIndices.end())
    {
        const PartyQuestJournalEntry& entry = m_journal[transactionIt->second];
        const auto status = entry.Transaction == transaction
            ? PartyQuestApplyStatus::Duplicate
            : PartyQuestApplyStatus::TransactionConflict;
        return {status, m_worldRevision, entry.QuestRevision};
    }

    if (transaction.QuestId != transaction.ProposedSnapshot.QuestId)
        return {PartyQuestApplyStatus::QuestIdMismatch, m_worldRevision, GetQuestRevision(*this, transaction.QuestId)};

    const uint64_t currentQuestRevision = GetQuestRevision(*this, transaction.QuestId);
    if (transaction.ExpectedQuestRevision != currentQuestRevision)
        return {PartyQuestApplyStatus::RevisionMismatch, m_worldRevision, currentQuestRevision};

    if (m_journal.size() >=
        PartyQuestDurableResourcePolicy::MaxCanonicalJournalRecords)
    {
        return {
            PartyQuestApplyStatus::ResourceLimitExceeded,
            m_worldRevision,
            currentQuestRevision};
    }

    QuestSnapshot canonicalSnapshot = transaction.ProposedSnapshot;
    canonicalSnapshot.Revision = currentQuestRevision + 1;
    canonicalSnapshot.InitiatorPlayerId = transaction.InitiatorPlayerId;
    canonicalSnapshot.Canonicalize();

    ++m_worldRevision;
    m_quests[transaction.QuestId] = canonicalSnapshot;

    PartyQuestJournalEntry entry;
    entry.WorldRevision = m_worldRevision;
    entry.QuestRevision = currentQuestRevision + 1;
    entry.Transaction = transaction;

    m_transactionJournalIndices.emplace(transaction.TransactionId, m_journal.size());
    m_journal.push_back(std::move(entry));

    return {PartyQuestApplyStatus::Accepted, m_worldRevision, currentQuestRevision + 1};
}

const QuestSnapshot* PartyQuestState::FindQuest(const GameId& acQuestId) const noexcept
{
    const auto it = m_quests.find(acQuestId);
    return it != m_quests.end() ? &it->second : nullptr;
}
