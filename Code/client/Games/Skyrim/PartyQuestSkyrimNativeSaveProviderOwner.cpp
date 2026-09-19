#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

PartyQuestSkyrimNativeSaveProviderOwner::
    ~PartyQuestSkyrimNativeSaveProviderOwner() noexcept
{
    Shutdown();
}

PartyQuestSkyrimNativeSaveProviderOwnerBindResult
PartyQuestSkyrimNativeSaveProviderOwner::Bind(
    const std::filesystem::path& acTrustedGameDirectory,
    uint64_t aExpectedGeneration) noexcept try
{
    PartyQuestSkyrimNativeSaveProviderOwnerBindResult result;
    result.RuntimeGeneration = aExpectedGeneration;
    if (aExpectedGeneration == 0u)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderOwnerStatus::
            InvalidExpectedGeneration;
        return result;
    }

    if (m_shutdown.load(std::memory_order_acquire))
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::AdmissionClosed;
        return result;
    }
    std::lock_guard lock(m_mutex);
    if (m_shutdown.load(std::memory_order_acquire))
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::AdmissionClosed;
        return result;
    }
    if (m_capability)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderOwnerStatus::
            AlreadyBound;
        return result;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (fence.GetGeneration() != aExpectedGeneration)
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::StaleGeneration;
        return result;
    }

    auto resolved = PartyQuestSkyrimNativeSaveProviderResolver::
        ResolveAndRegister(acTrustedGameDirectory, m_registration);
    result.ResolveStatus = resolved.Status;
    if (!resolved.IsRegistered() || !resolved.PollCapability)
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::ResolveRejected;
        return result;
    }

    m_capability.emplace(std::move(*resolved.PollCapability));
    if (fence.GetGeneration() != aExpectedGeneration)
    {
        (void)m_capability->Invalidate(m_registration);
        m_capability.reset();
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::StaleGeneration;
        return result;
    }

    m_runtimeGeneration = aExpectedGeneration;
    m_accepting = true;
    m_revoking.store(false, std::memory_order_release);
    result.Status = PartyQuestSkyrimNativeSaveProviderOwnerStatus::Bound;
    return result;
}
catch (...)
{
    PartyQuestSkyrimNativeSaveProviderOwnerBindResult result;
    result.Status =
        PartyQuestSkyrimNativeSaveProviderOwnerStatus::SynchronizationFailed;
    result.RuntimeGeneration = aExpectedGeneration;
    return result;
}

PartyQuestSkyrimNativeSaveProviderCommandStatus
PartyQuestSkyrimNativeSaveProviderOwner::Begin(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept try
{
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire))
        return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
    std::lock_guard lock(m_mutex);
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire) || !m_accepting ||
        !m_capability)
        return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
    return m_capability->Begin(m_registration, acIdentity);
}
catch (...)
{
    return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
}

PartyQuestSkyrimNativeSaveProviderCommandStatus
PartyQuestSkyrimNativeSaveProviderOwner::Cancel(
    uint64_t aAttemptNonce) noexcept try
{
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire))
        return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
    std::lock_guard lock(m_mutex);
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire) || !m_accepting ||
        !m_capability)
        return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
    return m_capability->Cancel(m_registration, aAttemptNonce);
}
catch (...)
{
    return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
}

PartyQuestSkyrimNativeSaveProviderPollResult
PartyQuestSkyrimNativeSaveProviderOwner::PollAndRoute(
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    uint64_t aNowMs) noexcept try
{
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire))
        return {};
    std::lock_guard lock(m_mutex);
    if (m_shutdown.load(std::memory_order_acquire) ||
        m_revoking.load(std::memory_order_acquire) || !m_accepting ||
        !m_capability)
        return {};
    return m_capability->PollAndRoute(
        m_registration, acReservedIdentity, aContract, aGate, aNowMs);
}
catch (...)
{
    return {};
}

PartyQuestSkyrimNativeSaveProviderOwnerStatus
PartyQuestSkyrimNativeSaveProviderOwner::Invalidate() noexcept try
{
    m_revoking.store(true, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    m_revoking.store(true, std::memory_order_release);
    m_accepting = false;
    if (m_capability)
    {
        (void)m_capability->Invalidate(m_registration);
        m_capability.reset();
    }
    m_runtimeGeneration = 0u;
    return PartyQuestSkyrimNativeSaveProviderOwnerStatus::Invalidated;
}
catch (...)
{
    return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
        SynchronizationFailed;
}

void PartyQuestSkyrimNativeSaveProviderOwner::Shutdown() noexcept
{
    m_shutdown.store(true, std::memory_order_release);
    m_revoking.store(true, std::memory_order_release);
    try
    {
        std::lock_guard lock(m_mutex);
        m_accepting = false;
        if (m_capability)
        {
            (void)m_capability->Invalidate(m_registration);
            m_capability.reset();
        }
        m_runtimeGeneration = 0u;
    }
    catch (...)
    {
    }
}

bool PartyQuestSkyrimNativeSaveProviderOwner::IsBound() const noexcept try
{
    if (m_shutdown.load(std::memory_order_acquire))
        return false;
    std::lock_guard lock(m_mutex);
    return !m_shutdown.load(std::memory_order_acquire) &&
        !m_revoking.load(std::memory_order_acquire) && m_accepting &&
        m_capability && m_capability->IsValid() &&
        m_runtimeGeneration != 0u;
}
catch (...)
{
    return false;
}

bool PartyQuestSkyrimNativeSaveProviderOwner::IsShutdown() const noexcept
{
    return m_shutdown.load(std::memory_order_acquire);
}

uint64_t
PartyQuestSkyrimNativeSaveProviderOwner::GetRuntimeGeneration() const noexcept
try
{
    if (m_shutdown.load(std::memory_order_acquire))
        return 0u;
    std::lock_guard lock(m_mutex);
    return m_runtimeGeneration;
}
catch (...)
{
    return 0u;
}
