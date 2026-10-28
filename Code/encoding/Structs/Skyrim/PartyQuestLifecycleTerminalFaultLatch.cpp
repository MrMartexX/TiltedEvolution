#include <Structs/Skyrim/PartyQuestLifecycleTerminalFaultLatch.h>

PartyQuestLifecycleTerminalFaultLatchResult
PartyQuestLifecycleTerminalFaultLatch::LatchFatal(
    PartyQuestOrderedLifecycleReason aRejectedReason,
    PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept
{
    if (!IsValidReason(aRejectedReason))
        return PartyQuestLifecycleTerminalFaultLatchResult::InvalidReason;

    if (!IsKnownStatus(aStatus))
        return PartyQuestLifecycleTerminalFaultLatchResult::InvalidStatus;

    if (!IsFatalStatus(aStatus))
        return PartyQuestLifecycleTerminalFaultLatchResult::NonFatalStatus;

    if (m_latched != 0u)
        return ObserveKnownReason(aRejectedReason);

    m_firstRejectedReason = aRejectedReason;
    m_cause = aStatus;
    m_observedEvidence =
        PartyQuestOrderedLifecycleEvidenceFor(aRejectedReason);
    m_latched = 1u;
    m_shutdownObserved =
        aRejectedReason == PartyQuestOrderedLifecycleReason::Shutdown ? 1u : 0u;
    return PartyQuestLifecycleTerminalFaultLatchResult::Latched;
}

PartyQuestLifecycleTerminalFaultLatchResult
PartyQuestLifecycleTerminalFaultLatch::Observe(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    if (!IsValidReason(aReason))
        return PartyQuestLifecycleTerminalFaultLatchResult::InvalidReason;

    if (m_latched == 0u)
        return PartyQuestLifecycleTerminalFaultLatchResult::NotLatched;

    return ObserveKnownReason(aReason);
}

PartyQuestLifecycleTerminalFaultSnapshot
PartyQuestLifecycleTerminalFaultLatch::Snapshot() const noexcept
{
    return {
        m_observedEvidence,
        m_firstRejectedReason,
        m_cause,
        m_latched,
        m_shutdownObserved};
}

bool PartyQuestLifecycleTerminalFaultLatch::IsKnownStatus(
    PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestOrderedLifecycleEnqueueStatus::Queued:
    case PartyQuestOrderedLifecycleEnqueueStatus::Coalesced:
    case PartyQuestOrderedLifecycleEnqueueStatus::Duplicate:
    case PartyQuestOrderedLifecycleEnqueueStatus::TerminalQueued:
    case PartyQuestOrderedLifecycleEnqueueStatus::TerminalClosed:
    case PartyQuestOrderedLifecycleEnqueueStatus::CounterExhausted:
    case PartyQuestOrderedLifecycleEnqueueStatus::QueueCapacityExceeded:
    case PartyQuestOrderedLifecycleEnqueueStatus::AllocationFailed:
    case PartyQuestOrderedLifecycleEnqueueStatus::InvalidReason:
        return true;
    }

    return false;
}

bool PartyQuestLifecycleTerminalFaultLatch::IsFatalStatus(
    PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept
{
    return aStatus ==
            PartyQuestOrderedLifecycleEnqueueStatus::QueueCapacityExceeded ||
        aStatus ==
            PartyQuestOrderedLifecycleEnqueueStatus::CounterExhausted ||
        aStatus ==
            PartyQuestOrderedLifecycleEnqueueStatus::AllocationFailed;
}

bool PartyQuestLifecycleTerminalFaultLatch::IsValidReason(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    return PartyQuestOrderedLifecycleEvidenceFor(aReason) != 0u;
}

PartyQuestLifecycleTerminalFaultLatchResult
PartyQuestLifecycleTerminalFaultLatch::ObserveKnownReason(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    const auto evidence = PartyQuestOrderedLifecycleEvidenceFor(aReason);
    const auto merged = static_cast<PartyQuestOrderedLifecycleEvidenceMask>(
        m_observedEvidence | evidence);
    const bool shutdown =
        aReason == PartyQuestOrderedLifecycleReason::Shutdown;

    if (merged == m_observedEvidence &&
        (!shutdown || m_shutdownObserved != 0u))
    {
        return PartyQuestLifecycleTerminalFaultLatchResult::DuplicateEvidence;
    }

    m_observedEvidence = merged;
    if (shutdown)
        m_shutdownObserved = 1u;
    return PartyQuestLifecycleTerminalFaultLatchResult::Observed;
}
