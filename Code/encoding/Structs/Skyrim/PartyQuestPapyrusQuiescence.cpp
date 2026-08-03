#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

bool PartyQuestPapyrusQuiescenceTracker::Begin(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || m_transactionId != 0)
        return false;

    m_transactionId = aTransactionId;
    m_lastGeneration = 0;
    m_stableSamples = 0;
    m_hasGeneration = false;
    m_quiescent = false;
    return true;
}

PartyQuestPapyrusQuiescenceStatus PartyQuestPapyrusQuiescenceTracker::Observe(
    uint64_t aTransactionId,
    uint32_t aPendingEventCount,
    uint64_t aQuestEventGeneration) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return PartyQuestPapyrusQuiescenceStatus::InvalidTransaction;

    if (m_quiescent)
    {
        // A later observation can invalidate quiescence if new work appeared.
        if (aPendingEventCount == 0 &&
            m_hasGeneration &&
            aQuestEventGeneration == m_lastGeneration)
        {
            return PartyQuestPapyrusQuiescenceStatus::Quiescent;
        }

        m_quiescent = false;
        m_stableSamples = 0;
    }

    if (aPendingEventCount != 0)
    {
        m_lastGeneration = aQuestEventGeneration;
        m_hasGeneration = true;
        m_stableSamples = 0;
        return PartyQuestPapyrusQuiescenceStatus::Waiting;
    }

    if (!m_hasGeneration || aQuestEventGeneration != m_lastGeneration)
    {
        m_lastGeneration = aQuestEventGeneration;
        m_hasGeneration = true;
        m_stableSamples = 1;
        return PartyQuestPapyrusQuiescenceStatus::Waiting;
    }

    ++m_stableSamples;
    if (m_stableSamples < kRequiredStableSamples)
        return PartyQuestPapyrusQuiescenceStatus::Waiting;

    m_quiescent = true;
    return PartyQuestPapyrusQuiescenceStatus::Quiescent;
}

bool PartyQuestPapyrusQuiescenceTracker::Reset(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return false;

    m_transactionId = 0;
    m_lastGeneration = 0;
    m_stableSamples = 0;
    m_hasGeneration = false;
    m_quiescent = false;
    return true;
}
