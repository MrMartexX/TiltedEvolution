#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>

#include <utility>

namespace
{
bool ClosesAdmission(PartyQuestRuntimeOwner::ClientBoundary aBoundary) noexcept
{
    switch (aBoundary)
    {
    case PartyQuestRuntimeOwner::ClientBoundary::Disconnected:
    case PartyQuestRuntimeOwner::ClientBoundary::PartyLeft:
    case PartyQuestRuntimeOwner::ClientBoundary::CampaignChanged:
    case PartyQuestRuntimeOwner::ClientBoundary::RuntimeIdentityChanged:
    case PartyQuestRuntimeOwner::ClientBoundary::Shutdown:
        return true;
    case PartyQuestRuntimeOwner::ClientBoundary::Connected:
    case PartyQuestRuntimeOwner::ClientBoundary::PartyJoined:
        return false;
    }
    return true;
}
} // namespace

PartyQuestRuntimeOwner::PartyQuestRuntimeOwner() noexcept = default;

PartyQuestRuntimeOwner::~PartyQuestRuntimeOwner() noexcept
{
    m_compatibilityEnvironment.Stop();

    std::lock_guard lock(m_mutex);
    m_shutdown = true;
    m_connected = false;
    m_inParty = false;
    m_runtimeSessionBound = false;
    m_boundGeneration = 0;
    m_deferredWorld.Clear();
    m_boundCampaignId.reset();
    m_boundPlayerProfileId.reset();
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
    const bool closeBeforeFence = ClosesAdmission(aBoundary);
    uint64_t closedEpoch{};
    if (aBoundary != ClientBoundary::Shutdown)
    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown)
            return BoundaryStatus::AlreadyShutdown;
    }

    // Revocation is local and non-blocking. Perform it before attempting the
    // exclusive fence so reentrant lifecycle notification cannot leave the old
    // admission epoch open when this thread already holds an execution lease.
    if (closeBeforeFence)
    {
        std::lock_guard lock(m_mutex);
        ApplyBoundaryStateLocked(aBoundary);
        m_lifecycleInvalidationPending = true;
        closedEpoch = m_ownerEpoch;
    }

    auto invalidation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().TryBeginInvalidation();
    if (!invalidation || !invalidation->IsValid())
        return BoundaryStatus::SynchronizationFailed;

    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown && aBoundary != ClientBoundary::Shutdown)
            return BoundaryStatus::AlreadyShutdown;
        if (!closeBeforeFence)
            ApplyBoundaryStateLocked(aBoundary);
        else if (m_ownerEpoch == closedEpoch)
            m_lifecycleInvalidationPending = false;
    }

    if (aBoundary == ClientBoundary::Shutdown)
        m_compatibilityEnvironment.Stop();

    return BoundaryStatus::Applied;
}

uint64_t PartyQuestRuntimeOwner::CloseSessionLifecycleAdmission(
    PartyQuestRuntimeLifecycleEvent aEvent) noexcept
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

    uint64_t ownerEpoch{};
    {
        std::lock_guard lock(m_mutex);
        ApplyBoundaryStateLocked(boundary);
        m_lifecycleInvalidationPending = true;
        ownerEpoch = m_ownerEpoch;
    }

    return ownerEpoch;
}

void PartyQuestRuntimeOwner::CompleteSessionLifecycleInvalidation(
    uint64_t aOwnerEpoch,
    uint64_t aGeneration) noexcept
{
    if (aOwnerEpoch == 0 || aGeneration == 0)
        return;

    bool stopCompatibility{};
    {
        std::lock_guard lock(m_mutex);
        if (m_ownerEpoch == aOwnerEpoch)
        {
            m_lifecycleInvalidationPending = false;
            stopCompatibility = m_shutdown;
        }
    }

    // The caller owns the exclusive generation invalidation lease. Re-locking
    // that shared_mutex here would self-deadlock; nonzero is the local contract.
    (void)aGeneration;
    if (stopCompatibility)
        m_compatibilityEnvironment.Stop();
}

bool PartyQuestRuntimeOwner::PublishRuntimeSessionBound(
    uint64_t aGeneration,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    const PartyQuestRuntimeSessionOwnerBindResult& acBindResult) noexcept
{
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (aGeneration == 0 || !acCampaignId.IsValid() ||
        !acPlayerProfileId.IsValid() || !acBindResult.IsReadyForAdmission() ||
        !fence.IsExecutionLeaseHeldByCurrentThread() ||
        fence.GetGeneration() != aGeneration || !m_sessionOwner.IsBound() ||
        m_sessionOwner.IsRecoveryBlocked())
    {
        return false;
    }

    const auto* pSession = m_sessionOwner.GetRuntimeSession();
    if (!pSession || pSession->GetCampaignId() != acCampaignId ||
        pSession->GetPlayerProfileId() != acPlayerProfileId)
    {
        return false;
    }

    std::lock_guard lock(m_mutex);
    if (m_shutdown || m_lifecycleInvalidationPending ||
        fence.GetGeneration() != aGeneration ||
        !m_sessionOwner.IsBound() || m_sessionOwner.IsRecoveryBlocked())
    {
        return false;
    }

    m_runtimeSessionBound = true;
    m_boundGeneration = aGeneration;
    m_boundCampaignId = acCampaignId;
    m_boundPlayerProfileId = acPlayerProfileId;
    return true;
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

bool PartyQuestRuntimeOwner::IsAcceptingOperations() const noexcept
{
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease> lease;
    if (!fence.IsExecutionLeaseHeldByCurrentThread())
    {
        lease = fence.TryAcquire(generation);
        if (!lease || !lease->IsValid())
            return false;
    }

    std::lock_guard lock(m_mutex);
    return generation != 0 && m_boundGeneration == generation &&
        CanAcceptLocked();
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
    return m_deferredWorld.GetPendingCount();
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
    ClientBoundary aBoundary) noexcept
{
    ++m_ownerEpoch;
    if (m_ownerEpoch == 0)
        m_ownerEpoch = 1;

    m_deferredWorld.Clear();
    m_runtimeSessionBound = false;
    m_lifecycleInvalidationPending = false;
    m_boundGeneration = 0;
    m_boundCampaignId.reset();
    m_boundPlayerProfileId.reset();

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
}

bool PartyQuestRuntimeOwner::CanAcceptLocked() const noexcept
{
    if (m_shutdown || m_lifecycleInvalidationPending ||
        !m_connected || !m_inParty ||
        !m_runtimeSessionBound || m_boundGeneration == 0 ||
        !m_boundCampaignId || !m_boundPlayerProfileId ||
        !m_sessionOwner.IsBound() || m_sessionOwner.IsRecoveryBlocked())
    {
        return false;
    }

    const auto* pSession = m_sessionOwner.GetRuntimeSession();
    return pSession && pSession->GetCampaignId() == *m_boundCampaignId &&
        pSession->GetPlayerProfileId() == *m_boundPlayerProfileId;
}
