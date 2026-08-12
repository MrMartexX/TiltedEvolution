#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <mutex>

uint64_t PartyQuestRuntimeGenerationFence::GetGeneration() const noexcept
{
    const std::shared_lock lock(m_mutex);
    return m_generation;
}

uint64_t PartyQuestRuntimeGenerationFence::Invalidate() noexcept
{
    const std::unique_lock lock(m_mutex);
    ++m_generation;
    if (m_generation == 0)
        ++m_generation;
    return m_generation;
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
