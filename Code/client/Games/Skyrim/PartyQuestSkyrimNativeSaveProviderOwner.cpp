#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

PartyQuestSkyrimNativeSaveProviderOwner::
    ~PartyQuestSkyrimNativeSaveProviderOwner() noexcept
{
    (void)Shutdown();
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
    if (m_operationActive)
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::AdmissionClosed;
        return result;
    }
    if (HasDrainLocked())
    {
        result.Status =
            PartyQuestSkyrimNativeSaveProviderOwnerStatus::AdmissionClosed;
        return result;
    }
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t currentGeneration = fence.GetGeneration();
    if (m_capability && m_runtimeGeneration == aExpectedGeneration &&
        currentGeneration == aExpectedGeneration && m_accepting &&
        !m_revoking.load(std::memory_order_acquire))
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderOwnerStatus::
            AlreadyBound;
        return result;
    }
    if (m_capability)
    {
        m_revoking.store(true, std::memory_order_release);
        m_accepting = false;
        (void)m_capability->Invalidate(m_registration);
        m_capability.reset();
        m_runtimeGeneration = 0u;
    }

    if (currentGeneration != aExpectedGeneration)
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
        m_revoking.load(std::memory_order_acquire) || m_operationActive ||
        !m_accepting || m_tracked ||
        !m_capability)
        return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
    return m_capability->Begin(m_registration, acIdentity);
}
catch (...)
{
    return PartyQuestSkyrimNativeSaveProviderCommandStatus::ProviderRejected;
}

PartyQuestSkyrimNativeSaveProviderBeginInvokeResult
PartyQuestSkyrimNativeSaveProviderOwner::BeginAndInvoke(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    PartyQuestSkyrimNativeSaveInvoker apInvoker,
    void* apContext) noexcept
{
    PartyQuestSkyrimNativeSaveProviderBeginInvokeResult result;
    PartyQuestSkyrimNativeSaveProviderPollCapability* pCapability = nullptr;
    try
    {
        if (!apInvoker || m_shutdown.load(std::memory_order_acquire) ||
            m_revoking.load(std::memory_order_acquire))
        {
            return result;
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_shutdown.load(std::memory_order_acquire) ||
                m_revoking.load(std::memory_order_acquire) || !m_accepting ||
                m_operationActive || m_tracked || !m_capability)
            {
                return result;
            }
            m_operationActive = true;
            m_operationThread = std::this_thread::get_id();
            pCapability = &*m_capability;
        }

        result = pCapability->BeginAndInvoke(
            m_registration, acIdentity, apInvoker, apContext);
        FinishActiveOperation();
        return result;
    }
    catch (...)
    {
        if (pCapability)
            FinishActiveOperation();
        result.Status =
            PartyQuestSkyrimNativeSaveProviderCommandStatus::NativeCallFailed;
        return result;
    }
}

PartyQuestSkyrimNativeSaveTrackedBeginResult
PartyQuestSkyrimNativeSaveProviderOwner::BeginTrackedAndInvoke(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity,
    uint64_t aNowMs,
    bool aMainPathExists,
    bool aCosavePathExists,
    PartyQuestSkyrimNativeSaveInvoker apInvoker,
    void* apContext) noexcept
{
    PartyQuestSkyrimNativeSaveTrackedBeginResult result;
    if (!apInvoker || !acIdentity.IsValid())
        return result;

    std::unique_ptr<TrackedRequest> tracked;
    try
    {
        tracked = std::make_unique<TrackedRequest>(acIdentity);
    }
    catch (...)
    {
        return result;
    }
    const auto coordinated = tracked->Gate.BeginCoordinated(
        tracked->Contract, tracked->Identity, aNowMs,
        aMainPathExists, aCosavePathExists);
    if (coordinated.Status !=
        PartyQuestAsyncSaveFinalizationStatus::Pending)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderCommandStatus::
            InvalidRequest;
        return result;
    }
    const auto reserved = tracked->Lifecycle.ObserveReservation(
        tracked->Identity,
        PartyQuestAsyncSaveReservationOutcome::Accepted);
    if (reserved.Status != PartyQuestAsyncSaveLifecycleStatus::Active ||
        !reserved.DrainRequired)
    {
        result.Status = PartyQuestSkyrimNativeSaveProviderCommandStatus::
            InvalidRequest;
        return result;
    }

    PartyQuestSkyrimNativeSaveProviderPollCapability* pCapability = nullptr;
    TrackedRequest* pTracked = nullptr;
    try
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_shutdown.load(std::memory_order_acquire) ||
                m_revoking.load(std::memory_order_acquire) || !m_accepting ||
                m_operationActive || !m_capability || HasDrainLocked())
            {
                return result;
            }
            m_operationActive = true;
            m_operationThread = std::this_thread::get_id();
            pCapability = &*m_capability;
            m_tracked = std::move(tracked);
            pTracked = m_tracked.get();
        }

        const auto native = pCapability->BeginAndInvoke(
            m_registration, pTracked->Identity, apInvoker, apContext);
        result.Status = native.Status;
        result.EngineInvocationAttempted = native.EngineInvocationAttempted;
        result.EngineInvocationSucceeded = native.EngineInvocationSucceeded;
        result.NativeReservationAccepted = native.NativeReservationAccepted;

        {
            std::lock_guard lock(m_mutex);
            if (native.NativeReservationAccepted)
            {
                if (!m_accepting || m_deferredInvalidation ||
                    m_revoking.load(std::memory_order_acquire) ||
                    m_shutdown.load(std::memory_order_acquire))
                {
                    (void)m_tracked->Lifecycle.CloseAdmission();
                    m_accepting = false;
                }
                result.DrainRequired = true;
            }
            else
            {
                m_tracked.reset();
            }
        }
        FinishActiveOperation();
        return result;
    }
    catch (...)
    {
        if (pCapability)
            FinishActiveOperation();
        result.Status =
            PartyQuestSkyrimNativeSaveProviderCommandStatus::NativeCallFailed;
        return result;
    }
}

PartyQuestSkyrimNativeSaveTrackedPollResult
PartyQuestSkyrimNativeSaveProviderOwner::PollTracked(uint64_t aNowMs) noexcept
{
    PartyQuestSkyrimNativeSaveTrackedPollResult result;
    PartyQuestSkyrimNativeSaveProviderPollCapability* pCapability = nullptr;
    TrackedRequest* pTracked = nullptr;
    try
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_operationActive || !m_capability || !m_tracked)
                return result;
            m_operationActive = true;
            m_operationThread = std::this_thread::get_id();
            pCapability = &*m_capability;
            pTracked = m_tracked.get();
        }

        auto polled = pCapability->PollAndRoute(
            m_registration, pTracked->Identity, pTracked->Contract,
            pTracked->Gate, aNowMs);
        result.Status = polled.Status;

        {
            std::lock_guard lock(m_mutex);
            if (polled.Status ==
                    PartyQuestSkyrimNativeSaveProviderPollStatus::Applied &&
                polled.Transport.Route.Finalization.RetirementApplied)
            {
                if (!m_accepting || m_deferredInvalidation ||
                    m_revoking.load(std::memory_order_acquire) ||
                    m_shutdown.load(std::memory_order_acquire))
                {
                    (void)pTracked->Lifecycle.CloseAdmission();
                }
                const bool succeeded = polled.Transport.Route.Finalization.
                    Completion.has_value();
                (void)pTracked->Lifecycle.ObserveCompletion(
                    pTracked->Identity,
                    succeeded ? PartyQuestAsyncSavePhysicalOutcome::Succeeded :
                                PartyQuestAsyncSavePhysicalOutcome::Failed);
                const auto retired = pTracked->Lifecycle.ObserveRetirement(
                    pTracked->Identity);
                result.Retired = retired.SafeToInvalidate;
                if (retired.ConsumptionAuthorized &&
                    polled.Transport.Route.Finalization.Completion)
                {
                    result.Completion.emplace(std::move(
                        *polled.Transport.Route.Finalization.Completion));
                }
                m_tracked.reset();
            }
        }
        FinishActiveOperation();
        return result;
    }
    catch (...)
    {
        if (pCapability)
            FinishActiveOperation();
        result.Status =
            PartyQuestSkyrimNativeSaveProviderPollStatus::NativeCallFailed;
        return result;
    }
}

PartyQuestSkyrimNativeSaveProviderOwnerStatus
PartyQuestSkyrimNativeSaveProviderOwner::CloseAdmissionAndCancelForDrain()
    noexcept
{
    PartyQuestSkyrimNativeSaveProviderPollCapability* pCapability = nullptr;
    uint64_t nonce = 0u;
    try
    {
        {
            std::lock_guard lock(m_mutex);
            m_accepting = false;
            if (m_operationActive)
                return PartyQuestSkyrimNativeSaveProviderOwnerStatus::DrainPending;
            if (!m_tracked)
                return PartyQuestSkyrimNativeSaveProviderOwnerStatus::Invalidated;
            const auto closed = m_tracked->Lifecycle.CloseAdmission();
            if (!closed.DrainRequired)
                return PartyQuestSkyrimNativeSaveProviderOwnerStatus::Invalidated;
            if (!m_capability)
                return PartyQuestSkyrimNativeSaveProviderOwnerStatus::DrainPending;
            m_operationActive = true;
            m_operationThread = std::this_thread::get_id();
            pCapability = &*m_capability;
            nonce = m_tracked->Identity.AttemptNonce;
        }
        const auto cancelStatus =
            pCapability->Cancel(m_registration, nonce);
        {
            std::lock_guard lock(m_mutex);
            if (cancelStatus ==
                    PartyQuestSkyrimNativeSaveProviderCommandStatus::Accepted &&
                m_tracked && m_tracked->Identity.AttemptNonce == nonce)
                (void)m_tracked->Lifecycle.ObserveCancelRequested(
                    m_tracked->Identity);
        }
        FinishActiveOperation();
        return PartyQuestSkyrimNativeSaveProviderOwnerStatus::DrainPending;
    }
    catch (...)
    {
        if (pCapability)
            FinishActiveOperation();
        return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
            SynchronizationFailed;
    }
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
        m_revoking.load(std::memory_order_acquire) || m_operationActive ||
        !m_accepting || m_tracked ||
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
        m_revoking.load(std::memory_order_acquire) || m_operationActive ||
        !m_accepting || m_tracked ||
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
    std::unique_lock lock(m_mutex);
    m_revoking.store(true, std::memory_order_release);
    m_accepting = false;
    if (m_operationActive)
    {
        m_deferredInvalidation = true;
        if (m_operationThread == std::this_thread::get_id())
        {
            return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
                InvalidationDeferred;
        }
        m_operationDrained.wait(lock, [this]() noexcept
        {
            return !m_operationActive;
        });
    }
    if (HasDrainLocked())
    {
        (void)m_tracked->Lifecycle.CloseAdmission();
        return PartyQuestSkyrimNativeSaveProviderOwnerStatus::DrainPending;
    }
    InvalidateLocked();
    return PartyQuestSkyrimNativeSaveProviderOwnerStatus::Invalidated;
}
catch (...)
{
    return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
        SynchronizationFailed;
}

PartyQuestSkyrimNativeSaveProviderOwnerStatus
PartyQuestSkyrimNativeSaveProviderOwner::Shutdown() noexcept
{
    m_shutdown.store(true, std::memory_order_release);
    m_revoking.store(true, std::memory_order_release);
    try
    {
        std::unique_lock lock(m_mutex);
        m_accepting = false;
        if (m_operationActive)
        {
            m_deferredInvalidation = true;
            if (m_operationThread == std::this_thread::get_id())
                return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
                    InvalidationDeferred;
            m_operationDrained.wait(lock, [this]() noexcept
            {
                return !m_operationActive;
            });
        }
        if (HasDrainLocked())
        {
            (void)m_tracked->Lifecycle.CloseAdmission();
            return PartyQuestSkyrimNativeSaveProviderOwnerStatus::DrainPending;
        }
        InvalidateLocked();
        return PartyQuestSkyrimNativeSaveProviderOwnerStatus::Invalidated;
    }
    catch (...)
    {
        return PartyQuestSkyrimNativeSaveProviderOwnerStatus::
            SynchronizationFailed;
    }
}

void PartyQuestSkyrimNativeSaveProviderOwner::FinishActiveOperation() noexcept
{
    try
    {
        PartyQuestSkyrimNativeSaveProviderPollCapability* pCapability = nullptr;
        uint64_t cancelNonce = 0u;
        std::unique_lock lock(m_mutex);
        if (m_tracked && !m_accepting)
        {
            const auto closed = m_tracked->Lifecycle.CloseAdmission();
            if (closed.CancelRequired && m_capability)
            {
                pCapability = &*m_capability;
                cancelNonce = m_tracked->Identity.AttemptNonce;
            }
        }
        if (pCapability)
        {
            lock.unlock();
            const auto cancelStatus =
                pCapability->Cancel(m_registration, cancelNonce);
            lock.lock();
            if (cancelStatus ==
                    PartyQuestSkyrimNativeSaveProviderCommandStatus::Accepted &&
                m_tracked &&
                m_tracked->Identity.AttemptNonce == cancelNonce)
            {
                (void)m_tracked->Lifecycle.ObserveCancelRequested(
                    m_tracked->Identity);
            }
        }
        m_operationActive = false;
        m_operationThread = {};
        if ((m_deferredInvalidation ||
            m_revoking.load(std::memory_order_acquire) ||
            m_shutdown.load(std::memory_order_acquire)) && !HasDrainLocked())
        {
            InvalidateLocked();
        }
        m_operationDrained.notify_all();
    }
    catch (...)
    {
        m_revoking.store(true, std::memory_order_release);
        m_operationDrained.notify_all();
    }
}

void PartyQuestSkyrimNativeSaveProviderOwner::InvalidateLocked() noexcept
{
    m_accepting = false;
    m_deferredInvalidation = false;
    if (m_capability)
    {
        (void)m_capability->Invalidate(m_registration);
        m_capability.reset();
    }
    m_runtimeGeneration = 0u;
}

bool PartyQuestSkyrimNativeSaveProviderOwner::HasDrainLocked() const noexcept
{
    return m_tracked && m_tracked->Lifecycle.Current().DrainRequired;
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
