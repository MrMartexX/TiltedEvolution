#include <Structs/Skyrim/PartyQuestProtocol.h>
#include <Structs/Skyrim/PartyQuestResourcePolicy.h>

#include <algorithm>

namespace
{
uint64_t ComputeSemanticDigest(QuestSnapshot aSnapshot)
{
    aSnapshot.Revision = 0;
    aSnapshot.InitiatorPlayerId = 0;
    aSnapshot.Canonicalize();
    return aSnapshot.ComputeDigest();
}

bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;

    return acLeft.BaseId < acRight.BaseId;
}
} // namespace

PartyQuestClientSubmissionDecision PartyQuestClientSubmissionQueue::Observe(
    const QuestSnapshot& acSnapshot,
    const PartyQuestReplica& acReplica)
{
    PartyQuestClientSubmissionDecision decision;
    if (!acSnapshot.QuestId ||
        !PartyQuestResourcePolicy::IsSnapshotWithinBounds(acSnapshot))
        return decision;

    SubmissionSnapshot observed;
    observed.Snapshot = acSnapshot;
    observed.Snapshot.Canonicalize();
    observed.SemanticDigest = ComputeSemanticDigest(observed.Snapshot);

    auto entryIt = m_quests.find(observed.Snapshot.QuestId);
    if (entryIt != m_quests.end() && entryIt->second.InFlight)
    {
        QuestEntry& entry = entryIt->second;
        if (entry.InFlight->SemanticDigest == observed.SemanticDigest ||
            (entry.Queued && entry.Queued->SemanticDigest == observed.SemanticDigest))
        {
            decision.Status = PartyQuestClientSubmissionStatus::Duplicate;
            return decision;
        }

        decision.Status = entry.Queued
            ? PartyQuestClientSubmissionStatus::ReplacedQueued
            : PartyQuestClientSubmissionStatus::Queued;
        entry.Queued = std::move(observed);
        return decision;
    }

    if (entryIt != m_quests.end() && entryIt->second.Queued)
    {
        QuestEntry& entry = entryIt->second;
        if (entry.Queued->SemanticDigest == observed.SemanticDigest)
        {
            decision.Status = PartyQuestClientSubmissionStatus::Duplicate;
            return decision;
        }

        entry.Queued = std::move(observed);
        decision.Status = PartyQuestClientSubmissionStatus::ReplacedQueued;
        return decision;
    }

    if (const QuestSnapshot* pCanonical = acReplica.FindQuest(observed.Snapshot.QuestId))
    {
        if (ComputeSemanticDigest(*pCanonical) == observed.SemanticDigest)
        {
            decision.Status = PartyQuestClientSubmissionStatus::Duplicate;
            return decision;
        }
    }

    if (m_quests.size() >=
        PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests)
    {
        decision.Status = PartyQuestClientSubmissionStatus::ResourceLimitExceeded;
        return decision;
    }

    decision.Status = PartyQuestClientSubmissionStatus::Ready;
    decision.ReadySnapshot = std::move(observed.Snapshot);
    return decision;
}

PartyQuestClientSubmissionStatus PartyQuestClientSubmissionQueue::QueueLatest(const QuestSnapshot& acSnapshot)
{
    if (!acSnapshot.QuestId ||
        !PartyQuestResourcePolicy::IsSnapshotWithinBounds(acSnapshot))
        return PartyQuestClientSubmissionStatus::InvalidSnapshot;

    SubmissionSnapshot observed;
    observed.Snapshot = acSnapshot;
    observed.Snapshot.Canonicalize();
    observed.SemanticDigest = ComputeSemanticDigest(observed.Snapshot);

    auto entryIt = m_quests.find(observed.Snapshot.QuestId);
    if (entryIt == m_quests.end())
    {
        if (m_quests.size() >=
            PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests)
        {
            return PartyQuestClientSubmissionStatus::ResourceLimitExceeded;
        }
        entryIt = m_quests.emplace(observed.Snapshot.QuestId, QuestEntry{}).first;
    }
    QuestEntry& entry = entryIt->second;
    if (entry.InFlight && entry.InFlight->SemanticDigest == observed.SemanticDigest)
        return PartyQuestClientSubmissionStatus::Duplicate;

    if (entry.Queued)
    {
        if (entry.Queued->SemanticDigest == observed.SemanticDigest)
            return PartyQuestClientSubmissionStatus::Duplicate;

        entry.Queued = std::move(observed);
        return PartyQuestClientSubmissionStatus::ReplacedQueued;
    }

    entry.Queued = std::move(observed);
    return PartyQuestClientSubmissionStatus::Queued;
}

bool PartyQuestClientSubmissionQueue::MarkInFlight(
    uint64_t aTransactionId,
    const QuestSnapshot& acSnapshot)
{
    if (aTransactionId == 0 || !acSnapshot.QuestId ||
        !PartyQuestResourcePolicy::IsSnapshotWithinBounds(acSnapshot) ||
        m_transactionQuests.contains(aTransactionId) ||
        m_transactionQuests.size() >=
            PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests)
        return false;

    auto entryIt = m_quests.find(acSnapshot.QuestId);
    if (entryIt == m_quests.end())
    {
        if (m_quests.size() >=
            PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests)
        {
            return false;
        }
        entryIt = m_quests.emplace(acSnapshot.QuestId, QuestEntry{}).first;
    }
    QuestEntry& entry = entryIt->second;
    if (entry.InFlight)
        return false;

    SubmissionSnapshot submission;
    submission.Snapshot = acSnapshot;
    submission.Snapshot.Canonicalize();
    submission.SemanticDigest = ComputeSemanticDigest(submission.Snapshot);

    entry.InFlight = std::move(submission);
    entry.Queued.reset();
    m_transactionQuests.emplace(aTransactionId, acSnapshot.QuestId);
    return true;
}

std::optional<QuestSnapshot> PartyQuestClientSubmissionQueue::Complete(
    uint64_t aTransactionId,
    const QuestSnapshot& acCanonicalSnapshot)
{
    const auto transactionIt = m_transactionQuests.find(aTransactionId);
    if (transactionIt == m_transactionQuests.end())
        return std::nullopt;

    const GameId questId = transactionIt->second;
    m_transactionQuests.erase(transactionIt);

    const auto questIt = m_quests.find(questId);
    if (questIt == m_quests.end())
        return std::nullopt;

    QuestEntry& entry = questIt->second;
    entry.InFlight.reset();

    std::optional<QuestSnapshot> ready;
    if (entry.Queued)
    {
        if (entry.Queued->SemanticDigest != ComputeSemanticDigest(acCanonicalSnapshot))
            ready = std::move(entry.Queued->Snapshot);
        entry.Queued.reset();
    }

    if (!entry.InFlight && !entry.Queued)
        m_quests.erase(questIt);

    return ready;
}

bool PartyQuestClientSubmissionQueue::Reject(uint64_t aTransactionId)
{
    const auto transactionIt = m_transactionQuests.find(aTransactionId);
    if (transactionIt == m_transactionQuests.end())
        return false;

    const GameId questId = transactionIt->second;
    m_transactionQuests.erase(transactionIt);

    const auto questIt = m_quests.find(questId);
    if (questIt == m_quests.end() || !questIt->second.InFlight)
        return false;

    QuestEntry& entry = questIt->second;
    if (!entry.Queued)
        entry.Queued = std::move(entry.InFlight);
    entry.InFlight.reset();
    return true;
}

bool PartyQuestClientSubmissionQueue::Discard(uint64_t aTransactionId)
{
    const auto transactionIt = m_transactionQuests.find(aTransactionId);
    if (transactionIt == m_transactionQuests.end())
        return false;

    const GameId questId = transactionIt->second;
    m_transactionQuests.erase(transactionIt);
    m_quests.erase(questId);
    return true;
}

std::vector<QuestSnapshot> PartyQuestClientSubmissionQueue::TakeReady(const PartyQuestReplica& acReplica)
{
    std::vector<std::pair<GameId, QuestSnapshot>> sorted;
    sorted.reserve(m_quests.size());

    std::vector<GameId> emptyEntries;
    for (auto& [questId, entry] : m_quests)
    {
        if (entry.InFlight || !entry.Queued)
            continue;

        const QuestSnapshot* pCanonical = acReplica.FindQuest(questId);
        if (!pCanonical || ComputeSemanticDigest(*pCanonical) != entry.Queued->SemanticDigest)
            sorted.emplace_back(questId, std::move(entry.Queued->Snapshot));

        entry.Queued.reset();
        emptyEntries.push_back(questId);
    }

    for (const GameId& questId : emptyEntries)
    {
        const auto it = m_quests.find(questId);
        if (it != m_quests.end() && !it->second.InFlight && !it->second.Queued)
            m_quests.erase(it);
    }

    std::sort(sorted.begin(), sorted.end(), [](const auto& acLeft, const auto& acRight)
    {
        return GameIdLess(acLeft.first, acRight.first);
    });

    std::vector<QuestSnapshot> ready;
    ready.reserve(sorted.size());
    for (auto& [questId, snapshot] : sorted)
    {
        (void)questId;
        ready.push_back(std::move(snapshot));
    }
    return ready;
}

void PartyQuestClientSubmissionQueue::RequeueInFlight()
{
    for (auto& [questId, entry] : m_quests)
    {
        (void)questId;
        if (!entry.InFlight)
            continue;

        if (!entry.Queued)
            entry.Queued = std::move(entry.InFlight);
        entry.InFlight.reset();
    }

    m_transactionQuests.clear();
}

void PartyQuestClientSubmissionQueue::Clear() noexcept
{
    m_quests.clear();
    m_transactionQuests.clear();
}

size_t PartyQuestClientSubmissionQueue::GetInFlightCount() const noexcept
{
    return m_transactionQuests.size();
}

size_t PartyQuestClientSubmissionQueue::GetQueuedCount() const noexcept
{
    size_t count = 0;
    for (const auto& [questId, entry] : m_quests)
    {
        (void)questId;
        count += entry.Queued.has_value() ? 1 : 0;
    }
    return count;
}
