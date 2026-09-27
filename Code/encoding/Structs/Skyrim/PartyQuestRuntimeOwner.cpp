#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

#include <utility>

PartyQuestRuntimeOwner::PartyQuestRuntimeOwner() noexcept
{
    try
    {
        m_callbackLifetime = std::make_shared<CallbackLifetime>();
        m_callbackLifetime->Owner = this;
        m_callbackLifetime->Alive = true;
    }
    catch (...)
    {
        m_callbackLifetime.reset();
        m_shutdown = true;
    }
}

PartyQuestRuntimeOwner::~PartyQuestRuntimeOwner() noexcept
{
    CloseCallbackLifetime();
    m_compatibilityEnvironment.Stop();

    std::lock_guard lock(m_mutex);
    m_shutdown = true;
    m_connected = false;
    m_inParty = false;
    m_runtimeSessionBound = false;
    m_boundGeneration = 0;
    m_deferredOperations.clear();
    m_skyrimObserver = {};
    m_papyrusObserver = {};
    m_executor = {};
}

PartyQuestRuntimeOwner& PartyQuestRuntimeOwner::GetProcessOwner() noexcept
{
    (void)PartyQuestRuntimeGenerationFence::GetProcessFence();
    static PartyQuestRuntimeOwner s_owner;
    return s_owner;
}

PartyQuestRuntimeOwner::BoundaryStatus
PartyQuestRuntimeOwner::ApplyClientBoundary(ClientBoundary aBoundary) noexcept
{
    if (aBoundary != ClientBoundary::Shutdown)
    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown)
            return BoundaryStatus::AlreadyShutdown;
    }

    auto invalidation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().TryBeginInvalidation();
    if (!invalidation || !invalidation->IsValid())
        return BoundaryStatus::SynchronizationFailed;

    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown && aBoundary != ClientBoundary::Shutdown)
            return BoundaryStatus::AlreadyShutdown;
        ApplyBoundaryStateLocked(aBoundary, invalidation->GetGeneration());
    }

    if (aBoundary == ClientBoundary::Shutdown)
        m_compatibilityEnvironment.Stop();

    return BoundaryStatus::Applied;
}

void PartyQuestRuntimeOwner::ObserveSessionLifecycleBoundary(
    PartyQuestRuntimeLifecycleEvent aEvent,
    uint64_t aGeneration) noexcept
{
    ClientBoundary boundary = ClientBoundary::RuntimeIdentityChanged;
    switch (aEvent)
    {
    case PartyQuestRuntimeLifecycleEvent::PartyLeave:
        boundary = ClientBoundary::PartyLeft;
        break;
    case PartyQuestRuntimeLifecycleEvent::Disconnect:
        boundary = ClientBoundary::Disconnected;
        break;
    case PartyQuestRuntimeLifecycleEvent::CampaignSwitch:
        boundary = ClientBoundary::CampaignChanged;
        break;
    case PartyQuestRuntimeLifecycleEvent::Shutdown:
        boundary = ClientBoundary::Shutdown;
        break;
    case PartyQuestRuntimeLifecycleEvent::LoadGame:
    case PartyQuestRuntimeLifecycleEvent::NewGame:
    case PartyQuestRuntimeLifecycleEvent::MainMenu:
    case PartyQuestRuntimeLifecycleEvent::ProfileSwitch:
        boundary = ClientBoundary::RuntimeIdentityChanged;
        break;
    }

    {
        std::lock_guard lock(m_mutex);
        ApplyBoundaryStateLocked(boundary, aGeneration);
    }

    if (boundary == ClientBoundary::Shutdown)
        m_compatibilityEnvironment.Stop();
}

void PartyQuestRuntimeOwner::MarkRuntimeSessionBound(uint64_t aGeneration) noexcept
{
    if (aGeneration == 0 ||
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration() != aGeneration)
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    if (m_shutdown || !m_sessionOwner.IsBound())
        return;

    m_runtimeSessionBound = true;
    m_boundGeneration = aGeneration;
}

PartyQuestRuntimeOwner::BootstrapResult PartyQuestRuntimeOwner::RunBootstrap(
    uint64_t aExpectedGeneration,
    const BootstrapAction& acAction) noexcept
{
    BootstrapResult result;
    result.RuntimeGeneration = aExpectedGeneration;
    if (aExpectedGeneration == 0 || !acAction)
        return result;

    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown)
            return result;
    }

    try
    {
        if (!acAction())
            return result;
    }
    catch (...)
    {
        result.Status = BootstrapStatus::Exception;
        return result;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (fence.GetGeneration() != aExpectedGeneration ||
        !m_sessionOwner.IsBound())
    {
        result.Status = BootstrapStatus::StaleGeneration;
        return result;
    }

    MarkRuntimeSessionBound(aExpectedGeneration);
    {
        std::lock_guard lock(m_mutex);
        if (!m_runtimeSessionBound || m_boundGeneration != aExpectedGeneration)
        {
            result.Status = BootstrapStatus::StaleGeneration;
            return result;
        }
    }

    result.Status = BootstrapStatus::Bound;
    return result;
}

void PartyQuestRuntimeOwner::ConfigureRuntimeAdapters(
    SkyrimObserver aSkyrimObserver,
    PapyrusObserver aPapyrusObserver,
    RuntimeExecutor aExecutor) noexcept
{
    try
    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown)
            return;
        m_skyrimObserver = std::move(aSkyrimObserver);
        m_papyrusObserver = std::move(aPapyrusObserver);
        m_executor = std::move(aExecutor);
    }
    catch (...)
    {
        ClearRuntimeAdapters();
    }
}

void PartyQuestRuntimeOwner::ClearRuntimeAdapters() noexcept
{
    std::lock_guard lock(m_mutex);
    m_skyrimObserver = {};
    m_papyrusObserver = {};
    m_executor = {};
}

PartyQuestRuntimeOwner::EnqueueResult PartyQuestRuntimeOwner::Enqueue(
    Validation aValidation,
    Operation aOperation) noexcept
{
    EnqueueResult result;
    if (!aValidation || !aOperation)
        return result;

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto lease = fence.TryAcquire(generation);
    if (generation == 0 || !lease || !lease->IsValid())
    {
        result.Status = EnqueueStatus::SynchronizationFailed;
        return result;
    }

    try
    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || !m_sessionOwner.IsBound() ||
            m_boundGeneration != generation)
        {
            result.Status = EnqueueStatus::AdmissionClosed;
            return result;
        }
        if (m_deferredOperations.size() >= MaxDeferredOperations)
        {
            result.Status = EnqueueStatus::ResourceLimitExceeded;
            return result;
        }

        uint64_t operationId = m_nextOperationId++;
        if (operationId == 0)
            operationId = m_nextOperationId++;

        DeferredOperation deferred;
        deferred.OperationId = operationId;
        deferred.RuntimeGeneration = generation;
        deferred.OwnerEpoch = m_ownerEpoch;
        deferred.Validate = std::move(aValidation);
        deferred.Execute = std::move(aOperation);
        m_deferredOperations.emplace_back(std::move(deferred));

        result.Status = EnqueueStatus::Queued;
        result.OperationId = operationId;
        result.RuntimeGeneration = generation;
        result.OwnerEpoch = m_ownerEpoch;
        return result;
    }
    catch (...)
    {
        result.Status = EnqueueStatus::InvalidOperation;
        return result;
    }
}

PartyQuestRuntimeOwner::ExecuteStatus PartyQuestRuntimeOwner::ExecuteNext() noexcept
{
    DeferredOperation deferred;
    try
    {
        std::lock_guard lock(m_mutex);
        if (m_deferredOperations.empty())
            return ExecuteStatus::Empty;
        deferred = std::move(m_deferredOperations.front());
        m_deferredOperations.pop_front();
    }
    catch (...)
    {
        return ExecuteStatus::CallbackFailed;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto lease = fence.TryAcquire(deferred.RuntimeGeneration);
    if (!lease || !lease->IsValid())
        return ExecuteStatus::StaleGeneration;

    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || !m_sessionOwner.IsBound())
            return ExecuteStatus::AdmissionClosed;
        if (deferred.OwnerEpoch != m_ownerEpoch ||
            deferred.RuntimeGeneration != m_boundGeneration ||
            lease->GetGeneration() != deferred.RuntimeGeneration)
        {
            return ExecuteStatus::StaleGeneration;
        }
    }

    bool validated = false;
    try
    {
        validated = deferred.Validate();
    }
    catch (...)
    {
        return ExecuteStatus::CallbackFailed;
    }
    if (!validated)
        return ExecuteStatus::ValidationRejected;

    try
    {
        deferred.Execute();
        return ExecuteStatus::Executed;
    }
    catch (...)
    {
        return ExecuteStatus::CallbackFailed;
    }
}

std::function<void()> PartyQuestRuntimeOwner::MakeExecuteNextCallback() noexcept
{
    try
    {
        std::weak_ptr<CallbackLifetime> lifetime = m_callbackLifetime;
        return [lifetime]() noexcept
        {
            const auto locked = lifetime.lock();
            if (!locked)
                return;

            std::lock_guard callbackLock(locked->Mutex);
            if (!locked->Alive || !locked->Owner)
                return;
            (void)locked->Owner->ExecuteNext();
        };
    }
    catch (...)
    {
        return {};
    }
}

bool PartyQuestRuntimeOwner::IsAcceptingOperations() const noexcept
{
    std::lock_guard lock(m_mutex);
    return CanAcceptLocked() && m_sessionOwner.IsBound();
}

bool PartyQuestRuntimeOwner::IsShutdown() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_shutdown;
}

uint64_t PartyQuestRuntimeOwner::GetOwnerEpoch() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_ownerEpoch;
}

size_t PartyQuestRuntimeOwner::GetPendingOperationCount() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_deferredOperations.size();
}

bool PartyQuestRuntimeOwner::ObserveSkyrimRuntime() noexcept
{
    SkyrimObserver observer;
    uint64_t generation{};
    try
    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || !m_skyrimObserver)
            return false;
        generation = m_boundGeneration;
        observer = m_skyrimObserver;
    }
    catch (...)
    {
        return false;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease> lease;
    if (!fence.IsExecutionLeaseHeldByCurrentThread())
    {
        lease = fence.TryAcquire(generation);
        if (!lease || !lease->IsValid())
            return false;
    }

    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || m_boundGeneration != generation)
            return false;
    }

    try
    {
        return observer();
    }
    catch (...)
    {
        return false;
    }
}

std::optional<PartyQuestPapyrusRuntimeObservation>
PartyQuestRuntimeOwner::ObservePapyrusRuntime(uint64_t aTransactionId) noexcept
{
    PapyrusObserver observer;
    uint64_t generation{};
    try
    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || !m_papyrusObserver)
            return std::nullopt;
        generation = m_boundGeneration;
        observer = m_papyrusObserver;
    }
    catch (...)
    {
        return std::nullopt;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease> lease;
    if (!fence.IsExecutionLeaseHeldByCurrentThread())
    {
        lease = fence.TryAcquire(generation);
        if (!lease || !lease->IsValid())
            return std::nullopt;
    }

    {
        std::lock_guard lock(m_mutex);
        if (!CanAcceptLocked() || m_boundGeneration != generation)
            return std::nullopt;
    }

    try
    {
        return observer(aTransactionId);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void PartyQuestRuntimeOwner::ApplyBoundaryStateLocked(
    ClientBoundary aBoundary,
    uint64_t aGeneration) noexcept
{
    ++m_ownerEpoch;
    if (m_ownerEpoch == 0)
        m_ownerEpoch = 1;

    m_deferredOperations.clear();
    m_runtimeSessionBound = false;
    m_boundGeneration = 0;

    switch (aBoundary)
    {
    case ClientBoundary::Connected:
        m_connected = true;
        m_inParty = false;
        break;
    case ClientBoundary::PartyJoined:
        m_inParty = m_connected;
        break;
    case ClientBoundary::Disconnected:
        m_connected = false;
        m_inParty = false;
        break;
    case ClientBoundary::PartyLeft:
        m_inParty = false;
        break;
    case ClientBoundary::CampaignChanged:
    case ClientBoundary::RuntimeIdentityChanged:
        break;
    case ClientBoundary::Shutdown:
        m_shutdown = true;
        m_connected = false;
        m_inParty = false;
        m_skyrimObserver = {};
        m_papyrusObserver = {};
        m_executor = {};
        break;
    }

    (void)aGeneration;
}

bool PartyQuestRuntimeOwner::CanAcceptLocked() const noexcept
{
    return !m_shutdown && m_connected && m_inParty &&
        m_runtimeSessionBound && m_boundGeneration != 0;
}

void PartyQuestRuntimeOwner::CloseCallbackLifetime() noexcept
{
    const auto lifetime = m_callbackLifetime;
    if (!lifetime)
        return;

    {
        std::lock_guard callbackLock(lifetime->Mutex);
        lifetime->Alive = false;
        lifetime->Owner = nullptr;
    }
    m_callbackLifetime.reset();
}
