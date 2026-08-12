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

std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease>
PartyQuestRuntimeGenerationFence::TryAcquire(
    uint64_t aExpectedGeneration) const noexcept
{
    if (aExpectedGeneration == 0)
        return std::nullopt;

    std::shared_lock lock(m_mutex);
    if (m_generation != aExpectedGeneration)
        return std::nullopt;

    return ExecutionLease(std::move(lock), aExpectedGeneration);
}
