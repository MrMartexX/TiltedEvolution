#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

PartyQuestRuntimeGenerationFence&
PartyQuestRuntimeGenerationFence::GetProcessFence() noexcept
{
    static PartyQuestRuntimeGenerationFence s_processFence;
    return s_processFence;
}

uint64_t PartyQuestRuntimeGenerationFence::GetGeneration() const noexcept
{
    if (IsPoisoned())
        return 0;

    try
    {
        const std::shared_lock lock(m_mutex);
        if (IsPoisoned())
            return 0;
        return m_generation;
    }
    catch (...)
    {
        Poison();
        return 0;
    }
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
    auto lease = TryBeginInvalidation();
    return lease ? lease->GetGeneration() : 0;
}

std::optional<PartyQuestRuntimeGenerationFence::InvalidationLease>
PartyQuestRuntimeGenerationFence::TryBeginInvalidation() noexcept
{
    if (IsPoisoned())
        return std::nullopt;

    try
    {
        std::unique_lock lock(m_mutex);
        if (IsPoisoned())
            return std::nullopt;

        const uint64_t generation = AdvanceGenerationLocked();
        return InvalidationLease(std::move(lock), generation);
    }
    catch (...)
    {
        Poison();
        return std::nullopt;
    }
}

PartyQuestRuntimeGenerationFence::InvalidationLease
PartyQuestRuntimeGenerationFence::BeginInvalidation() noexcept
{
    auto lease = TryBeginInvalidation();
    return lease ? std::move(*lease) : InvalidationLease{};
}

PartyQuestRuntimeGenerationFence::LifecycleTransitionTicket
PartyQuestRuntimeGenerationFence::BeginLifecycleTransition() noexcept
{
    if (IsPoisoned())
        return {};

    try
    {
        std::unique_lock lock(m_mutex);
        if (IsPoisoned() || m_lifecycleTicket != 0)
            return {};

        const uint64_t generation = AdvanceGenerationLocked();
        m_lifecycleTicket = AllocateLifecycleTicketLocked();
        return {m_lifecycleTicket, generation};
    }
    catch (...)
    {
        Poison();
        return {};
    }
}

bool PartyQuestRuntimeGenerationFence::CompleteLifecycleTransition(
    LifecycleTransitionTicket aTicket) noexcept
{
    if (!aTicket.IsValid() || IsPoisoned())
        return false;

    try
    {
        std::unique_lock lock(m_mutex);
        if (IsPoisoned() || m_lifecycleTicket != aTicket.Ticket)
            return false;

        m_lifecycleTicket = 0;
        return true;
    }
    catch (...)
    {
        Poison();
        return false;
    }
}

bool PartyQuestRuntimeGenerationFence::IsLifecycleTransitionPending() const noexcept
{
    if (IsPoisoned())
        return true;

    try
    {
        const std::shared_lock lock(m_mutex);
        return IsPoisoned() || m_lifecycleTicket != 0;
    }
    catch (...)
    {
        Poison();
        return true;
    }
}

std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease>
PartyQuestRuntimeGenerationFence::TryAcquire(
    uint64_t aExpectedGeneration) const noexcept
{
    if (aExpectedGeneration == 0 || IsPoisoned())
        return std::nullopt;

    try
    {
        std::shared_lock lock(m_mutex);
        if (IsPoisoned() || m_lifecycleTicket != 0 ||
            m_generation != aExpectedGeneration)
        {
            return std::nullopt;
        }

        return ExecutionLease(std::move(lock), aExpectedGeneration);
    }
    catch (...)
    {
        Poison();
        return std::nullopt;
    }
}
