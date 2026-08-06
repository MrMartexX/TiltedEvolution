#include <Structs/Skyrim/PartyQuestRuntimeVerificationMonitor.h>

bool PartyQuestRuntimeVerificationMonitor::Begin(
    uint64_t aTransactionId,
    uint64_t aNowMs) noexcept
{
    if (aTransactionId == 0 || m_status != PartyQuestRuntimeVerificationMonitorStatus::Inactive)
        return false;

    m_transactionId = aTransactionId;
    m_startedAtMs = aNowMs;
    m_lastNowMs = aNowMs;
    m_divergentSamples = 0;
    m_status = PartyQuestRuntimeVerificationMonitorStatus::Waiting;
    return true;
}

PartyQuestRuntimeVerificationMonitorStatus
PartyQuestRuntimeVerificationMonitor::EnterTerminal(
    PartyQuestRuntimeVerificationMonitorStatus aStatus) noexcept
{
    m_status = aStatus;
    return m_status;
}

PartyQuestRuntimeVerificationMonitorStatus
PartyQuestRuntimeVerificationMonitor::CheckTime(uint64_t aNowMs) noexcept
{
    if (m_status == PartyQuestRuntimeVerificationMonitorStatus::Inactive)
        return m_status;

    if (aNowMs < m_lastNowMs)
        return EnterTerminal(PartyQuestRuntimeVerificationMonitorStatus::InvalidClock);

    m_lastNowMs = aNowMs;
    if (aNowMs - m_startedAtMs >= kTimeoutMs)
        return EnterTerminal(PartyQuestRuntimeVerificationMonitorStatus::TimedOut);

    return m_status;
}

PartyQuestRuntimeVerificationMonitorStatus PartyQuestRuntimeVerificationMonitor::Poll(
    uint64_t aTransactionId,
    uint64_t aNowMs) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return PartyQuestRuntimeVerificationMonitorStatus::InvalidTransaction;

    switch (m_status)
    {
    case PartyQuestRuntimeVerificationMonitorStatus::Waiting:
    case PartyQuestRuntimeVerificationMonitorStatus::Stable:
        return CheckTime(aNowMs);
    case PartyQuestRuntimeVerificationMonitorStatus::Inactive:
    case PartyQuestRuntimeVerificationMonitorStatus::TimedOut:
    case PartyQuestRuntimeVerificationMonitorStatus::DivergenceBudgetExceeded:
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidClock:
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification:
        return m_status;
    case PartyQuestRuntimeVerificationMonitorStatus::InvalidTransaction:
        return PartyQuestRuntimeVerificationMonitorStatus::InvalidTransaction;
    }

    return EnterTerminal(PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification);
}

PartyQuestRuntimeVerificationMonitorStatus PartyQuestRuntimeVerificationMonitor::Observe(
    uint64_t aTransactionId,
    uint64_t aNowMs,
    PartyQuestRuntimeVerificationStatus aVerification) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return PartyQuestRuntimeVerificationMonitorStatus::InvalidTransaction;

    if (m_status != PartyQuestRuntimeVerificationMonitorStatus::Waiting)
        return m_status;

    const auto timeStatus = CheckTime(aNowMs);
    if (timeStatus != PartyQuestRuntimeVerificationMonitorStatus::Waiting)
        return timeStatus;

    switch (aVerification)
    {
    case PartyQuestRuntimeVerificationStatus::Diverged:
        ++m_divergentSamples;
        if (m_divergentSamples >= kMaxDivergentSamples)
        {
            return EnterTerminal(
                PartyQuestRuntimeVerificationMonitorStatus::DivergenceBudgetExceeded);
        }
        return m_status;

    case PartyQuestRuntimeVerificationStatus::NeedsStableSample:
        return m_status;

    case PartyQuestRuntimeVerificationStatus::Stable:
        m_status = PartyQuestRuntimeVerificationMonitorStatus::Stable;
        return m_status;

    case PartyQuestRuntimeVerificationStatus::InvalidState:
        return EnterTerminal(
            PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification);
    }

    return EnterTerminal(PartyQuestRuntimeVerificationMonitorStatus::InvalidVerification);
}

void PartyQuestRuntimeVerificationMonitor::Cancel(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return;

    m_transactionId = 0;
    m_startedAtMs = 0;
    m_lastNowMs = 0;
    m_divergentSamples = 0;
    m_status = PartyQuestRuntimeVerificationMonitorStatus::Inactive;
}
