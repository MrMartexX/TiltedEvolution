#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

PartyQuestRuntimeGenerationFence&
PartyQuestRuntimeGenerationFence::GetProcessFence() noexcept
{
    static PartyQuestRuntimeGenerationFence s_processFence;
    return s_processFence;
}

uint64_t PartyQuestRuntimeGenerationFence::GetGeneration() const noexcept
{
    const std::shared_lock lock(m_mutex);
    return m_generation;
}

uint64_t PartyQuestRuntimeGenerationFence::AdvanceGenerationLocked() noexcept
{
    ++m_generation;
    if (m_generation == 0)
        ++m_generation;
    return m_generation;
}

uint64_t PartyQuestRuntimeGenerationFence::AllocateLifecycleTicketLocked() noexcept
{
    uint64_t ticket = m_nextLifecycleTicket++;
    if (ticket == 0)
        ticket = m_nextLifecycleTicket++;
    if (m_nextLifecycleTicket == 0)
        ++m_nextLifecycleTicket;
    return ticket;
}

uint64_t PartyQuestRuntimeGenerationFence::Invalidate() noexcept
{
    auto lease = BeginInvalidation();
    return lease.GetGeneration();
}

PartyQuestRuntimeGenerationFence::InvalidationLease
PartyQuestRuntimeGenerationFence::BeginInvalidation() noexcept
{
    std::unique_lock lock(m_mutex);
    const uint64_t generation = AdvanceGenerationLocked();
    return InvalidationLease(std::move(lock), generation);
}

PartyQuestRuntimeGenerationFence::LifecycleTransitionTicket
PartyQuestRuntimeGenerationFence::BeginLifecycleTransition() noexcept
{
    try
    {
        std::unique_lock lock(m_mutex);
        if (m_lifecycleTicket != 0)
            return {};

        const uint64_t generation = AdvanceGenerationLocked();
        m_lifecycleTicket = AllocateLifecycleTicketLocked();
        return {m_lifecycleTicket, generation};
    }
    catch (...)
    {
        return {};
    }
}

bool PartyQuestRuntimeGenerationFence::CompleteLifecycleTransition(
    LifecycleTransitionTicket aTicket) noexcept
{
    if (!aTicket.IsValid())
        return false;

    try
    {
        std::unique_lock lock(m_mutex);
        if (m_lifecycleTicket != aTicket.Ticket)
            return false;

        m_lifecycleTicket = 0;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool PartyQuestRuntimeGenerationFence::IsLifecycleTransitionPending() const noexcept
{
    try
    {
        const std::shared_lock lock(m_mutex);
        return m_lifecycleTicket != 0;
    }
    catch (...)
    {
        return true;
    }
}

std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease>
PartyQuestRuntimeGenerationFence::TryAcquire(
    uint64_t aExpectedGeneration) const noexcept
{
    if (aExpectedGeneration == 0)
        return std::nullopt;

    std::shared_lock lock(m_mutex);
    if (m_lifecycleTicket != 0 || m_generation != aExpectedGeneration)
        return std::nullopt;

    return ExecutionLease(std::move(lock), aExpectedGeneration);
}