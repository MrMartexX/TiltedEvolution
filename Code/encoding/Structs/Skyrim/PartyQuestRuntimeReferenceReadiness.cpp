#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

PartyQuestRuntimeReferenceReadiness::PartyQuestRuntimeReferenceReadiness() noexcept
    : PartyQuestRuntimeReferenceReadiness(
        PartyQuestRuntimeGenerationFence::GetProcessFence())
{
}

PartyQuestRuntimeReferenceReadiness::PartyQuestRuntimeReferenceReadiness(
    PartyQuestRuntimeGenerationFence& aGenerationFence) noexcept
    : m_generationFence(aGenerationFence)
{
}

bool PartyQuestRuntimeReferenceReadiness::UsesGenerationFence(
    const PartyQuestRuntimeGenerationFence& acGenerationFence) const noexcept
{
    return &m_generationFence == &acGenerationFence;
}

void PartyQuestRuntimeReferenceReadiness::ResetLocked(
    uint64_t aGeneration) noexcept
{
    m_observationGeneration = aGeneration;
    m_loadedReferences.clear();
    m_overflowed = false;
}

bool PartyQuestRuntimeReferenceReadiness::Observe(
    uint32_t aFormId,
    bool aLoaded) noexcept
{
    if (aFormId == 0)
        return false;

    const uint64_t generation = m_generationFence.GetGeneration();
    auto lease = m_generationFence.TryAcquire(generation);
    if (!lease || !lease->IsValid())
        return false;

    std::lock_guard lock(m_mutex);
    if (m_observationGeneration != generation)
        ResetLocked(generation);

    if (m_overflowed)
        return false;

    if (!aLoaded)
    {
        m_loadedReferences.erase(aFormId);
        return true;
    }

    if (m_loadedReferences.contains(aFormId))
        return true;

    if (m_loadedReferences.size() >= MaxTrackedReferences)
    {
        m_loadedReferences.clear();
        m_overflowed = true;
        return false;
    }

    m_loadedReferences.emplace(aFormId);
    return true;
}

bool PartyQuestRuntimeReferenceReadiness::IsLoaded(
    uint32_t aFormId,
    uint64_t aExpectedGeneration) const noexcept
{
    if (aFormId == 0 || aExpectedGeneration == 0)
        return false;

    auto lease = m_generationFence.TryAcquire(aExpectedGeneration);
    if (!lease || !lease->IsValid())
        return false;

    std::lock_guard lock(m_mutex);
    return !m_overflowed &&
        m_observationGeneration == aExpectedGeneration &&
        m_loadedReferences.contains(aFormId);
}

bool PartyQuestRuntimeReferenceReadiness::AreLoaded(
    const std::vector<uint32_t>& acFormIds,
    uint64_t aExpectedGeneration) const noexcept
{
    if (aExpectedGeneration == 0)
        return false;

    auto lease = m_generationFence.TryAcquire(aExpectedGeneration);
    if (!lease || !lease->IsValid())
        return false;

    std::lock_guard lock(m_mutex);
    if (m_overflowed || m_observationGeneration != aExpectedGeneration)
        return false;

    for (const uint32_t formId : acFormIds)
    {
        if (formId == 0 || !m_loadedReferences.contains(formId))
            return false;
    }

    return true;
}

uint64_t PartyQuestRuntimeReferenceReadiness::GetObservationGeneration() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_observationGeneration;
}

bool PartyQuestRuntimeReferenceReadiness::IsOverflowed() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_overflowed;
}
