#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <utility>

void PartyQuestPapyrusRuntimeMonitor::Clear() noexcept
{
    m_transactionId = 0;
    m_startedAtMs = 0;
    m_lastNowMs = 0;
    m_timeoutMs = 0;
    m_lastObservedGeneration = 0;
    m_hasObservedGeneration = false;
    m_authoritativeObserver = false;
    m_status = PartyQuestPapyrusRuntimeMonitorStatus::Inactive;
}

bool PartyQuestPapyrusRuntimeMonitor::BeginInternal(
    uint64_t aTransactionId,
    uint64_t aNowMs,
    uint64_t aTimeoutMs,
    bool aAuthoritativeObserver) noexcept
{
    if (aTransactionId == 0 || aTimeoutMs == 0 || m_transactionId != 0)
        return false;

    if (!m_tracker.Begin(aTransactionId))
        return false;

    m_transactionId = aTransactionId;
    m_startedAtMs = aNowMs;
    m_lastNowMs = aNowMs;
    m_timeoutMs = aTimeoutMs;
    m_lastObservedGeneration = 0;
    m_hasObservedGeneration = false;
    m_authoritativeObserver = aAuthoritativeObserver;
    m_status = PartyQuestPapyrusRuntimeMonitorStatus::Waiting;
    return true;
}

bool PartyQuestPapyrusRuntimeMonitor::Begin(
    uint64_t aTransactionId,
    uint64_t aNowMs,
    uint64_t aTimeoutMs) noexcept
{
    return BeginInternal(
        aTransactionId,
        aNowMs,
        aTimeoutMs,
        false);
}

bool PartyQuestPapyrusRuntimeMonitor::Begin(
    uint64_t aTransactionId,
    uint64_t aNowMs,
    uint64_t aTimeoutMs,
    const PartyQuestPapyrusRuntimeObserverAuthorization& acAuthorization) noexcept
{
    if (!acAuthorization.Matches(m_observer))
        return false;

    return BeginInternal(
        aTransactionId,
        aNowMs,
        aTimeoutMs,
        true);
}

bool PartyQuestPapyrusRuntimeMonitor::RestartTracker() noexcept
{
    if (m_transactionId == 0)
        return false;

    const uint64_t transactionId = m_transactionId;
    if (m_tracker.GetTransactionId() != 0 &&
        !m_tracker.Reset(transactionId))
    {
        return false;
    }

    return m_tracker.Begin(transactionId);
}

PartyQuestPapyrusRuntimeMonitorStatus
PartyQuestPapyrusRuntimeMonitor::EnterTerminal(
    PartyQuestPapyrusRuntimeMonitorStatus aStatus) noexcept
{
    if (m_transactionId != 0 && m_tracker.GetTransactionId() == m_transactionId)
        m_tracker.Reset(m_transactionId);

    m_status = aStatus;
    return m_status;
}

PartyQuestPapyrusRuntimeMonitorStatus PartyQuestPapyrusRuntimeMonitor::Poll(
    uint64_t aTransactionId,
    uint64_t aNowMs) noexcept
{
    if (aTransactionId == 0 ||
        aTransactionId != m_transactionId ||
        m_transactionId == 0)
    {
        return PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction;
    }

    switch (m_status)
    {
    case PartyQuestPapyrusRuntimeMonitorStatus::TimedOut:
    case PartyQuestPapyrusRuntimeMonitorStatus::Unsupported:
    case PartyQuestPapyrusRuntimeMonitorStatus::InvalidClock:
    case PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation:
        return m_status;
    case PartyQuestPapyrusRuntimeMonitorStatus::Inactive:
    case PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction:
        return PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction;
    case PartyQuestPapyrusRuntimeMonitorStatus::Waiting:
    case PartyQuestPapyrusRuntimeMonitorStatus::Quiescent:
        break;
    }

    if (aNowMs < m_lastNowMs || aNowMs < m_startedAtMs)
        return EnterTerminal(PartyQuestPapyrusRuntimeMonitorStatus::InvalidClock);

    m_lastNowMs = aNowMs;
    if (aNowMs - m_startedAtMs >= m_timeoutMs)
        return EnterTerminal(PartyQuestPapyrusRuntimeMonitorStatus::TimedOut);

    const PartyQuestPapyrusRuntimeObservation observation =
        m_observer.Observe(aTransactionId);
    if (!observation.IsSelfConsistent())
        return EnterTerminal(
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);

    switch (observation.Status)
    {
    case PartyQuestPapyrusRuntimeObservationStatus::Unsupported:
        return EnterTerminal(PartyQuestPapyrusRuntimeMonitorStatus::Unsupported);

    case PartyQuestPapyrusRuntimeObservationStatus::Unknown:
        if (!RestartTracker())
        {
            return EnterTerminal(
                PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction);
        }
        m_status = PartyQuestPapyrusRuntimeMonitorStatus::Waiting;
        return m_status;

    case PartyQuestPapyrusRuntimeObservationStatus::Busy:
    case PartyQuestPapyrusRuntimeObservationStatus::Idle:
        break;
    }

    if (m_hasObservedGeneration &&
        observation.QuestEventGeneration < m_lastObservedGeneration)
    {
        return EnterTerminal(
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
    }

    m_lastObservedGeneration = observation.QuestEventGeneration;
    m_hasObservedGeneration = true;

    const auto trackerStatus = m_tracker.Observe(
        aTransactionId,
        observation.PendingWorkCount,
        observation.QuestEventGeneration);
    if (trackerStatus == PartyQuestPapyrusQuiescenceStatus::InvalidTransaction)
    {
        return EnterTerminal(
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction);
    }

    m_status = trackerStatus == PartyQuestPapyrusQuiescenceStatus::Quiescent
        ? PartyQuestPapyrusRuntimeMonitorStatus::Quiescent
        : PartyQuestPapyrusRuntimeMonitorStatus::Waiting;
    return m_status;
}

std::optional<PartyQuestPapyrusQuiescenceAuthorization>
PartyQuestPapyrusRuntimeMonitor::Authorize() const noexcept
{
    if (m_status != PartyQuestPapyrusRuntimeMonitorStatus::Quiescent)
        return std::nullopt;

    return m_tracker.Authorize();
}

bool PartyQuestPapyrusRuntimeMonitor::Consume(
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept
{
    if (m_status != PartyQuestPapyrusRuntimeMonitorStatus::Quiescent ||
        aAuthorization.GetTransactionId() != m_transactionId)
    {
        return false;
    }

    if (!m_tracker.Consume(std::move(aAuthorization)))
        return false;

    Clear();
    return true;
}

bool PartyQuestPapyrusRuntimeMonitor::ConsumeAuthoritative(
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept
{
    if (!m_authoritativeObserver)
        return false;

    return Consume(std::move(aAuthorization));
}

bool PartyQuestPapyrusRuntimeMonitor::Reset(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0 || aTransactionId != m_transactionId)
        return false;

    if (m_tracker.GetTransactionId() == m_transactionId &&
        !m_tracker.Reset(m_transactionId))
    {
        return false;
    }

    Clear();
    return true;
}
