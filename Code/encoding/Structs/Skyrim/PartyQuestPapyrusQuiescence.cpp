#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

#include <atomic>
#include <utility>

namespace
{
uint64_t NextTrackerSessionNonce() noexcept
{
    static std::atomic<uint64_t> sequence{1};
    for (;;)
    {
        const uint64_t value = sequence.fetch_add(1, std::memory_order_relaxed);
        if (value != 0)
            return value;
    }
}
} // namespace

void PartyQuestPapyrusQuiescenceTracker::Clear() noexcept
{
    m_transactionId = 0;
    m_sessionNonce = 0;
    m_lastGeneration = 0;
    m_observationRevision = 0;
    m_stableSamples = 0;
    m_hasGeneration = false;
    m_quiescent = false;
}

bool PartyQuestPapyrusQuiescenceTracker::Begin(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || m_transactionId != 0)
        return false;

    m_transactionId = aTransactionId;
    m_sessionNonce = NextTrackerSessionNonce();
    m_lastGeneration = 0;
    m_observationRevision = 0;
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

    ++m_observationRevision;
    if (m_observationRevision == 0)
    {
        Clear();
        return PartyQuestPapyrusQuiescenceStatus::InvalidTransaction;
    }

    if (m_quiescent)
    {
        // A later observation always invalidates any previously issued
        // authorization by changing m_observationRevision. The tracker may
        // remain quiescent only when the observed state itself is unchanged.
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

std::optional<PartyQuestPapyrusQuiescenceAuthorization>
PartyQuestPapyrusQuiescenceTracker::Authorize() const noexcept
{
    if (m_transactionId == 0 ||
        m_sessionNonce == 0 ||
        m_observationRevision == 0 ||
        !m_hasGeneration ||
        !m_quiescent ||
        m_stableSamples < kRequiredStableSamples)
    {
        return std::nullopt;
    }

    return PartyQuestPapyrusQuiescenceAuthorization(
        m_transactionId,
        m_sessionNonce,
        m_lastGeneration,
        m_observationRevision);
}

bool PartyQuestPapyrusQuiescenceTracker::Consume(
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept
{
    const bool matches =
        aAuthorization.IsVerified() &&
        m_transactionId != 0 &&
        m_sessionNonce != 0 &&
        m_observationRevision != 0 &&
        m_hasGeneration &&
        m_quiescent &&
        m_stableSamples >= kRequiredStableSamples &&
        aAuthorization.m_transactionId == m_transactionId &&
        aAuthorization.m_sessionNonce == m_sessionNonce &&
        aAuthorization.m_eventGeneration == m_lastGeneration &&
        aAuthorization.m_observationRevision == m_observationRevision;

    aAuthorization.Invalidate();
    if (!matches)
        return false;

    Clear();
    return true;
}

bool PartyQuestPapyrusQuiescenceTracker::Reset(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return false;

    Clear();
    return true;
}