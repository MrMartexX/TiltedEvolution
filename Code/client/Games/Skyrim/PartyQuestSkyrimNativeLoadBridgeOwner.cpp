#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeOwner.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

namespace
{
using OwnerStatus = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
using StateResult = PartyQuestNativeLoadBridgeOwnerResult;
using StateResultCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using Authority = PartyQuestNativeLoadBridgeCallAuthority;

[[nodiscard]] PartyQuestNativeLoadBridgeOwnerForeignOutcome
UnknownOutcome(
    const PartyQuestNativeLoadBridgeOwnerEffect& acEffect) noexcept
{
    PartyQuestNativeLoadBridgeOwnerForeignOutcome outcome{};
    outcome.EffectKind = acEffect.Kind;
    outcome.Disposition =
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Unknown;
    outcome.EffectSequence = acEffect.Sequence;
    return outcome;
}

[[nodiscard]] bool IsForeignCommand(
    PartyQuestNativeLoadBridgeOwnerCommandKind aKind) noexcept
{
    switch (aKind)
    {
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Cancel:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Poll:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Retire:
        return true;
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Invalid:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Bind:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::GenerationTransition:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Shutdown:
        return false;
    }

    return false;
}
} // namespace

PartyQuestSkyrimNativeLoadBridgeOwner&
PartyQuestSkyrimNativeLoadBridgeOwner::GetProcessOwner() noexcept
{
    // Establish fence lifetime before the process owner, mirroring
    // PartyQuestRuntimeOwner. Explicit application shutdown is still required;
    // the static destructor deliberately performs no foreign/native calls.
    (void)PartyQuestRuntimeGenerationFence::GetProcessFence();
    static PartyQuestSkyrimNativeLoadBridgeOwner s_owner;
    return s_owner;
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::BindAuthenticated(
    PartyQuestSkyrimNativeLoadBridgeModuleLease&& aLease) noexcept
try
{
    if (!aLease.IsCallable() ||
        aLease.GetBoundGeneration() == 0u ||
        aLease.GetRuntimeFingerprint() == 0u)
    {
        return MakeOwnerResult(
            OwnerStatus::InvalidAuthenticatedLease);
    }

    const uint64_t expectedGeneration = aLease.GetBoundGeneration();
    auto generationLease =
        PartyQuestRuntimeGenerationFence::GetProcessFence().TryAcquire(
            expectedGeneration);
    if (!generationLease || !generationLease->IsValid())
    {
        return MakeOwnerResult(OwnerStatus::GenerationUnavailable);
    }

    std::lock_guard lock(m_mutex);
    if (m_shutdownRequested)
        return MakeOwnerResult(OwnerStatus::AdmissionClosed);

    if (m_operationActive || m_lease)
        return MakeOwnerResult(OwnerStatus::OperationInFlight);

    PartyQuestNativeLoadBridgeOwnerCommand command{};
    command.Kind = PartyQuestNativeLoadBridgeOwnerCommandKind::Bind;
    command.BindDisposition =
        PartyQuestNativeLoadBridgeOwnerBindDisposition::Authenticated;
    command.RuntimeGeneration = expectedGeneration;
    command.RuntimeFingerprint = aLease.GetRuntimeFingerprint();

    const auto plan = m_state.Plan(command);
    if (plan.Code != StateResultCode::Bound)
        return MakeOwnerResult(OwnerStatus::Applied, plan);

    m_lease.emplace(std::move(aLease));
    return MakeOwnerResult(OwnerStatus::Applied, plan);
}
catch (...)
{
    return MakeOwnerResult(OwnerStatus::SynchronizationFailed);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::Reserve(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    PartyQuestNativeLoadBridgeOwnerCommand command{};
    command.Kind = PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve;
    command.Identity = acIdentity;
    return ExecuteRequestCommand(command);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::Cancel(
    uint64_t aAttemptNonce) noexcept
{
    return ExecuteRequestCommand(
        MakeNonceCommand(
            PartyQuestNativeLoadBridgeOwnerCommandKind::Cancel,
            aAttemptNonce));
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::Poll(
    uint64_t aAttemptNonce) noexcept
{
    return ExecuteRequestCommand(
        MakeNonceCommand(
            PartyQuestNativeLoadBridgeOwnerCommandKind::Poll,
            aAttemptNonce));
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::Retire(
    uint64_t aAttemptNonce) noexcept
{
    return ExecuteRequestCommand(
        MakeNonceCommand(
            PartyQuestNativeLoadBridgeOwnerCommandKind::Retire,
            aAttemptNonce));
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::ExecuteRequestCommand(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
try
{
    if (!IsForeignCommand(acCommand.Kind))
    {
        PartyQuestNativeLoadBridgeOwnerResult invalid{};
        invalid.Code =
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidCommand;
        return MakeOwnerResult(OwnerStatus::Applied, invalid);
    }

    uint64_t expectedGeneration = 0u;
    bool requiresGenerationLease = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_operationActive)
            return MakeOwnerResult(OwnerStatus::OperationInFlight);

        if (m_shutdownRequested &&
            acCommand.Kind ==
                PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve)
        {
            return MakeOwnerResult(OwnerStatus::AdmissionClosed);
        }

        const auto snapshot = m_state.Snapshot();
        if (snapshot.Phase == Phase::Bound)
        {
            expectedGeneration = snapshot.BoundGeneration;
            requiresGenerationLease = true;
        }
    }

    std::optional<PartyQuestRuntimeGenerationFence::ExecutionLease>
        generationLease;
    if (requiresGenerationLease)
    {
        generationLease =
            PartyQuestRuntimeGenerationFence::GetProcessFence().TryAcquire(
                expectedGeneration);
        if (!generationLease || !generationLease->IsValid())
        {
            return MakeOwnerResult(
                OwnerStatus::GenerationUnavailable);
        }
    }

    std::unique_lock lock(m_mutex);
    if (m_operationActive)
        return MakeOwnerResult(OwnerStatus::OperationInFlight);

    if (m_shutdownRequested &&
        acCommand.Kind ==
            PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve)
    {
        return MakeOwnerResult(OwnerStatus::AdmissionClosed);
    }

    const auto before = m_state.Snapshot();
    const bool nowRequiresGenerationLease =
        before.Phase == Phase::Bound;
    if (nowRequiresGenerationLease != requiresGenerationLease ||
        (nowRequiresGenerationLease &&
            (before.BoundGeneration != expectedGeneration ||
             before.CurrentGeneration != expectedGeneration)))
    {
        return MakeOwnerResult(
            OwnerStatus::GenerationUnavailable);
    }

    if (nowRequiresGenerationLease &&
        (!generationLease ||
         !generationLease->IsValid() ||
         generationLease->GetGeneration() != expectedGeneration))
    {
        return MakeOwnerResult(
            OwnerStatus::GenerationUnavailable);
    }

    auto plan = m_state.Plan(acCommand);
    if (plan.HasEffect == 0u)
        return MakeOwnerResult(OwnerStatus::Applied, plan);

    const auto authorized =
        PartyQuestNativeLoadBridgeCallCapabilityPolicy::Authorize(
            m_state.Snapshot(), plan);
    if (!authorized.IsAuthorized() ||
        !m_lease ||
        !m_lease->IsCallable() ||
        m_lease->GetBoundGeneration() !=
            authorized.Capability.BoundGeneration ||
        m_lease->GetRuntimeFingerprint() !=
            authorized.Capability.RuntimeFingerprint)
    {
        const auto poisoned = PoisonPendingEffectLocked(plan);
        return MakeOwnerResult(
            OwnerStatus::ContractViolation,
            poisoned);
    }

    if (nowRequiresGenerationLease)
    {
        if (authorized.Capability.Authority !=
                Authority::CurrentGeneration ||
            !authorized.Capability.RequiresCurrentGenerationLease())
        {
            const auto poisoned = PoisonPendingEffectLocked(plan);
            return MakeOwnerResult(
                OwnerStatus::ContractViolation,
                poisoned);
        }
    }
    else
    {
        if (authorized.Capability.Authority !=
                Authority::RequestDrain ||
            !authorized.Capability.IsRequestDrain())
        {
            const auto poisoned = PoisonPendingEffectLocked(plan);
            return MakeOwnerResult(
                OwnerStatus::ContractViolation,
                poisoned);
        }
    }

    m_operationActive = true;
    m_operationThread = std::this_thread::get_id();
    m_activeEffectSequence = plan.Effect.Sequence;

    const auto effect = plan.Effect;
    const auto capability = authorized.Capability;
    lock.unlock();

    // Deliberately outside m_mutex. m_operationActive prevents m_lease from
    // being replaced or released until this exact effect is applied.
    const auto outcome = m_lease->Execute(capability, effect);

    lock.lock();
    if (!m_operationActive ||
        m_operationThread != std::this_thread::get_id() ||
        m_activeEffectSequence != effect.Sequence)
    {
        const auto poisoned = PoisonPendingEffectLocked(plan);
        FinishOperationLocked();
        return MakeOwnerResult(
            OwnerStatus::ContractViolation,
            poisoned,
            true);
    }

    const auto applied = m_state.ApplyForeignOutcome(outcome);
    ApplyReleaseLocked(applied);
    FinishOperationLocked();
    return MakeOwnerResult(
        OwnerStatus::Applied,
        applied,
        true);
}
catch (...)
{
    try
    {
        std::lock_guard lock(m_mutex);
        if (m_operationActive &&
            m_operationThread == std::this_thread::get_id())
        {
            FinishOperationLocked();
        }
    }
    catch (...)
    {
    }
    return MakeOwnerResult(OwnerStatus::SynchronizationFailed);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::ObserveGeneration(
    uint64_t aGeneration) noexcept
try
{
    if (aGeneration == 0u)
    {
        return MakeOwnerResult(
            OwnerStatus::GenerationUnavailable);
    }

    auto& generationFence =
        PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (generationFence.IsExecutionLeaseHeldByCurrentThread())
    {
        return MakeOwnerResult(
            OwnerStatus::LifecycleDeferred);
    }

    // Do not call GetGeneration()/TryAcquire here. The existing synchronous
    // lifecycle path may still own the fence's exclusive InvalidationLease.
    std::unique_lock lock(m_mutex);
    if (m_operationActive)
    {
        if (m_operationThread == std::this_thread::get_id())
        {
            return MakeOwnerResult(
                OwnerStatus::LifecycleDeferred);
        }

        m_operationDrained.wait(lock, [this]() noexcept
        {
            return !m_operationActive;
        });
    }

    PartyQuestNativeLoadBridgeOwnerCommand command{};
    command.Kind =
        PartyQuestNativeLoadBridgeOwnerCommandKind::GenerationTransition;
    command.RuntimeGeneration = aGeneration;

    const auto plan = m_state.Plan(command);
    ApplyReleaseLocked(plan);
    return MakeOwnerResult(OwnerStatus::Applied, plan);
}
catch (...)
{
    return MakeOwnerResult(OwnerStatus::SynchronizationFailed);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::Shutdown() noexcept
try
{
    std::unique_lock lock(m_mutex);
    m_shutdownRequested = true;

    if (m_operationActive)
    {
        if (m_operationThread == std::this_thread::get_id())
        {
            return MakeOwnerResult(
                OwnerStatus::LifecycleDeferred);
        }

        m_operationDrained.wait(lock, [this]() noexcept
        {
            return !m_operationActive;
        });
    }

    PartyQuestNativeLoadBridgeOwnerCommand command{};
    command.Kind = PartyQuestNativeLoadBridgeOwnerCommandKind::Shutdown;
    auto plan = m_state.Plan(command);

    if (plan.HasEffect == 0u)
    {
        ApplyReleaseLocked(plan);
        return MakeOwnerResult(OwnerStatus::Applied, plan);
    }

    return ExecuteShutdownEffectLocked(lock, plan);
}
catch (...)
{
    try
    {
        std::lock_guard lock(m_mutex);
        if (m_operationActive &&
            m_operationThread == std::this_thread::get_id())
        {
            FinishOperationLocked();
        }
    }
    catch (...)
    {
    }
    return MakeOwnerResult(OwnerStatus::SynchronizationFailed);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::ExecuteShutdownEffectLocked(
    std::unique_lock<std::mutex>& aLock,
    PartyQuestNativeLoadBridgeOwnerResult aPlan)
{
    const auto authorized =
        PartyQuestNativeLoadBridgeCallCapabilityPolicy::Authorize(
            m_state.Snapshot(), aPlan);
    if (!authorized.IsAuthorized() ||
        authorized.Capability.Authority != Authority::RequestDrain ||
        !authorized.Capability.IsRequestDrain() ||
        !m_lease ||
        !m_lease->IsCallable() ||
        m_lease->GetBoundGeneration() !=
            authorized.Capability.BoundGeneration ||
        m_lease->GetRuntimeFingerprint() !=
            authorized.Capability.RuntimeFingerprint)
    {
        const auto poisoned = PoisonPendingEffectLocked(aPlan);
        return MakeOwnerResult(
            OwnerStatus::ContractViolation,
            poisoned);
    }

    return ExecutePlannedEffectLocked(
        aLock,
        aPlan,
        authorized.Capability,
        true);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::ExecutePlannedEffectLocked(
    std::unique_lock<std::mutex>& aLock,
    PartyQuestNativeLoadBridgeOwnerResult aPlan,
    const PartyQuestNativeLoadBridgeCallCapability& acCapability,
    bool aForeignCallAttempted)
{
    m_operationActive = true;
    m_operationThread = std::this_thread::get_id();
    m_activeEffectSequence = aPlan.Effect.Sequence;

    const auto effect = aPlan.Effect;
    const auto capability = acCapability;
    aLock.unlock();

    const auto outcome = m_lease->Execute(capability, effect);

    aLock.lock();
    if (!m_operationActive ||
        m_operationThread != std::this_thread::get_id() ||
        m_activeEffectSequence != effect.Sequence)
    {
        const auto poisoned = PoisonPendingEffectLocked(aPlan);
        FinishOperationLocked();
        return MakeOwnerResult(
            OwnerStatus::ContractViolation,
            poisoned,
            aForeignCallAttempted);
    }

    const auto applied = m_state.ApplyForeignOutcome(outcome);
    ApplyReleaseLocked(applied);
    FinishOperationLocked();
    return MakeOwnerResult(
        OwnerStatus::Applied,
        applied,
        aForeignCallAttempted);
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::PoisonPendingEffectLocked(
    PartyQuestNativeLoadBridgeOwnerResult aPlan) noexcept
{
    if (aPlan.HasEffect == 0u ||
        aPlan.Effect.Kind ==
            PartyQuestNativeLoadBridgeOwnerEffectKind::None ||
        aPlan.Effect.Sequence == 0u)
    {
        return aPlan;
    }

    return m_state.ApplyForeignOutcome(
        UnknownOutcome(aPlan.Effect));
}

void PartyQuestSkyrimNativeLoadBridgeOwner::ApplyReleaseLocked(
    const PartyQuestNativeLoadBridgeOwnerResult& acResult) noexcept
{
    if (acResult.ReleaseCapability == 1u)
        m_lease.reset();
}

void PartyQuestSkyrimNativeLoadBridgeOwner::FinishOperationLocked() noexcept
{
    m_operationActive = false;
    m_operationThread = {};
    m_activeEffectSequence = 0u;
    m_operationDrained.notify_all();
}

PartyQuestNativeLoadBridgeOwnerCommand
PartyQuestSkyrimNativeLoadBridgeOwner::MakeNonceCommand(
    PartyQuestNativeLoadBridgeOwnerCommandKind aKind,
    uint64_t aAttemptNonce) noexcept
{
    PartyQuestNativeLoadBridgeOwnerCommand command{};
    command.Kind = aKind;
    command.AttemptNonce = aAttemptNonce;
    return command;
}

PartyQuestSkyrimNativeLoadBridgeOwnerResult
PartyQuestSkyrimNativeLoadBridgeOwner::MakeOwnerResult(
    PartyQuestSkyrimNativeLoadBridgeOwnerStatus aStatus,
    const PartyQuestNativeLoadBridgeOwnerResult& acState,
    bool aForeignCallAttempted) noexcept
{
    PartyQuestSkyrimNativeLoadBridgeOwnerResult result{};
    result.Status = aStatus;
    result.ForeignCallAttempted = aForeignCallAttempted ? 1u : 0u;
    result.State = acState;
    return result;
}

PartyQuestNativeLoadBridgeOwnerSnapshot
PartyQuestSkyrimNativeLoadBridgeOwner::Snapshot() const noexcept
try
{
    std::lock_guard lock(m_mutex);
    return m_state.Snapshot();
}
catch (...)
{
    PartyQuestNativeLoadBridgeOwnerSnapshot snapshot{};
    snapshot.Phase =
        PartyQuestNativeLoadBridgeOwnerPhase::PoisonedUnsafeToUnload;
    return snapshot;
}

bool PartyQuestSkyrimNativeLoadBridgeOwner::IsShutdownRequested() const noexcept
try
{
    std::lock_guard lock(m_mutex);
    return m_shutdownRequested;
}
catch (...)
{
    return true;
}

bool PartyQuestSkyrimNativeLoadBridgeOwner::IsOperationActive() const noexcept
try
{
    std::lock_guard lock(m_mutex);
    return m_operationActive;
}
catch (...)
{
    return true;
}
