#pragma once

#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeModuleLease.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

class PartyQuestSkyrimNativeLoadBridgeResolver;
class PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess;

enum class PartyQuestSkyrimNativeLoadBridgeOwnerStatus : uint8_t
{
    Applied = 1u,
    AdmissionClosed = 2u,
    GenerationUnavailable = 3u,
    OperationInFlight = 4u,
    LifecycleDeferred = 5u,
    InvalidAuthenticatedLease = 6u,
    ContractViolation = 7u,
    SynchronizationFailed = 8u
};

struct PartyQuestSkyrimNativeLoadBridgeOwnerResult final
{
    PartyQuestSkyrimNativeLoadBridgeOwnerStatus Status{
        PartyQuestSkyrimNativeLoadBridgeOwnerStatus::SynchronizationFailed};
    uint8_t ForeignCallAttempted{};
    uint8_t Reserved[6]{};
    PartyQuestNativeLoadBridgeOwnerResult State;

    [[nodiscard]] bool IsApplied() const noexcept
    {
        return Status ==
            PartyQuestSkyrimNativeLoadBridgeOwnerStatus::Applied;
    }
};

enum class PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus : uint8_t
{
    Completed = 1u,
    KnownPendingBudgetExhausted = 2u,
    LifecycleDeferred = 3u,
    PoisonedUnsafeToUnload = 4u,
    ContractViolation = 5u,
    SynchronizationFailed = 6u,
    UnexpectedOwnerState = 7u
};

struct PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult final
{
    PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus Status{
        PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus::
            UnexpectedOwnerState};
    PartyQuestNativeLoadBridgeOwnerPhase FinalPhase{
        PartyQuestNativeLoadBridgeOwnerPhase::Unbound};
    PartyQuestNativeLoadBridgeOwnerRequestPhase FinalRequestPhase{
        PartyQuestNativeLoadBridgeOwnerRequestPhase::None};
    uint8_t CapabilityRetained{};
    uint8_t Reserved[4]{};

    uint32_t PollCalls{};
    uint32_t KnownPendingPolls{};
    PartyQuestSkyrimNativeLoadBridgeOwnerResult Last;

    [[nodiscard]] bool IsSafeToTeardown() const noexcept
    {
        return Status ==
                PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus::
                    Completed &&
            FinalPhase ==
                PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete &&
            FinalRequestPhase ==
                PartyQuestNativeLoadBridgeOwnerRequestPhase::None &&
            CapabilityRetained == 0u;
    }

    [[nodiscard]] bool MustRetainCapability() const noexcept
    {
        return CapabilityRetained != 0u;
    }
};

/**
 * Windows-side serialization owner for one authenticated native LoadGame bridge
 * binding.
 *
 * The reducer remains the lifecycle/ownership authority. This class only owns
 * synchronization, the physical pinned module lease, and the exact call
 * choreography:
 *
 *   current-generation operation:
 *     read expected generation under owner mutex
 *     -> acquire PartyQuestRuntimeGenerationFence execution lease
 *     -> revalidate/Plan/authorize under owner mutex
 *     -> release owner mutex
 *     -> one foreign call
 *     -> reacquire owner mutex
 *     -> ApplyForeignOutcome
 *
 *   DrainOnly/ShutdownDrain operation:
 *     Plan/authorize under owner mutex
 *     -> release owner mutex
 *     -> exact request-bound Cancel/Poll/Retire on retained old module lease
 *     -> reacquire owner mutex
 *     -> ApplyForeignOutcome
 *
 * No foreign code executes while m_mutex is held. A RequestDrain capability is
 * never converted into current runtime authorization and therefore acquires no
 * stale generation lease.
 *
 * BindAuthenticated is private: the future trusted resolver must authenticate
 * descriptor/module/runtime evidence before handing this owner a module lease.
 * This slice performs no DLL discovery, path trust, runtime fingerprint
 * research, SKSE hook installation, or production service wiring.
 *
 * The owner is non-copyable/non-movable because mutex identity, reducer
 * monotonic counters and the exact physical lease form one process-lifetime
 * ownership domain.
 */
class PartyQuestSkyrimNativeLoadBridgeOwner final
{
public:
    PartyQuestSkyrimNativeLoadBridgeOwner() noexcept = default;
    ~PartyQuestSkyrimNativeLoadBridgeOwner() noexcept = default;

    /**
     * Production process-lifetime ownership domain.
     *
     * Runtime generations may advance and World may be recreated, but native
     * attempt/completion/effect monotonic history must not reset. Production
     * lifecycle/service code must therefore use this owner rather than create a
     * World-scoped instance. Public construction remains available for isolated
     * deterministic tests, matching the existing RuntimeGenerationFence test
     * pattern.
     */
    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeOwner&
    GetProcessOwner() noexcept;

    PartyQuestSkyrimNativeLoadBridgeOwner(
        const PartyQuestSkyrimNativeLoadBridgeOwner&) = delete;
    PartyQuestSkyrimNativeLoadBridgeOwner& operator=(
        const PartyQuestSkyrimNativeLoadBridgeOwner&) = delete;
    PartyQuestSkyrimNativeLoadBridgeOwner(
        PartyQuestSkyrimNativeLoadBridgeOwner&&) = delete;
    PartyQuestSkyrimNativeLoadBridgeOwner& operator=(
        PartyQuestSkyrimNativeLoadBridgeOwner&&) = delete;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult Reserve(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult Cancel(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult Poll(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult Retire(
        uint64_t aAttemptNonce) noexcept;

    /**
     * Reconcile the reducer to a generation published by the existing process
     * lifecycle.
     *
     * This method never advances or reacquires PartyQuestRuntimeGenerationFence.
     * That is intentional: existing synchronous lifecycle code may call while
     * it still owns the exclusive InvalidationLease. Observing a generation can
     * only close/advance old bridge state; it cannot authorize a new binding.
     * BindAuthenticated separately proves exact current-generation authority
     * with TryAcquire().
     *
     * If a foreign operation is active on another thread, lifecycle waits for
     * it; same-thread reentry returns LifecycleDeferred instead of
     * self-deadlocking.
     */
    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    ObserveGeneration(uint64_t aGeneration) noexcept;

    /**
     * Close admission permanently and execute one reducer shutdown step.
     *
     * If Cancel reports InvalidState for an already-claimed request, the result
     * is Pending and the caller must Poll until completion, then call Shutdown
     * again so the reducer publishes exact Retire. No timeout fabricates a
     * successful release.
     */
    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    Shutdown() noexcept;

    /**
     * Deterministically drive shutdown through Cancel/Poll/Retire.
     *
     * aMaxKnownPendingPolls bounds only Poll calls that return the valid
     * Pending status. It is deliberately not a wall-clock timeout and cannot
     * interrupt one hung foreign call; the current ABI has no safe cancellation
     * primitive for an in-flight call.
     *
     * Budget exhaustion is not reducer poison. It leaves ShutdownDrain intact,
     * admission closed and the exact capability retained for process-terminal
     * policy or a later drain continuation. Unknown/SEH/impossible foreign
     * outcomes remain reducer-terminal PoisonedUnsafeToUnload and also retain
     * capability ownership.
     */
    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult
    DrainShutdown(uint32_t aMaxKnownPendingPolls) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerSnapshot Snapshot()
        const noexcept;

    [[nodiscard]] bool IsShutdownRequested() const noexcept;
    [[nodiscard]] bool IsOperationActive() const noexcept;

private:
    friend class PartyQuestSkyrimNativeLoadBridgeResolver;
    friend class PartyQuestSkyrimNativeLoadBridgeOwnerTestAccess;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    BindAuthenticated(
        PartyQuestSkyrimNativeLoadBridgeModuleLease&& aLease) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    ExecuteRequestCommand(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    ExecuteShutdownEffectLocked(
        std::unique_lock<std::mutex>& aLock,
        PartyQuestNativeLoadBridgeOwnerResult aPlan);

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    ExecutePlannedEffectLocked(
        std::unique_lock<std::mutex>& aLock,
        PartyQuestNativeLoadBridgeOwnerResult aPlan,
        const PartyQuestNativeLoadBridgeCallCapability& acCapability,
        bool aForeignCallAttempted);

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeOwnerResult
    PoisonPendingEffectLocked(
        PartyQuestNativeLoadBridgeOwnerResult aPlan) noexcept;

    void ApplyReleaseLocked(
        const PartyQuestNativeLoadBridgeOwnerResult& acResult) noexcept;

    void FinishOperationLocked() noexcept;

    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult
    MakeShutdownDrainResult(
        PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus aStatus,
        uint32_t aPollCalls,
        uint32_t aKnownPendingPolls,
        const PartyQuestSkyrimNativeLoadBridgeOwnerResult& acLast,
        const PartyQuestNativeLoadBridgeOwnerSnapshot& acSnapshot) noexcept;

    [[nodiscard]] static PartyQuestNativeLoadBridgeOwnerCommand
    MakeNonceCommand(
        PartyQuestNativeLoadBridgeOwnerCommandKind aKind,
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeOwnerResult
    MakeOwnerResult(
        PartyQuestSkyrimNativeLoadBridgeOwnerStatus aStatus,
        const PartyQuestNativeLoadBridgeOwnerResult& acState = {},
        bool aForeignCallAttempted = false) noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_operationDrained;

    PartyQuestNativeLoadBridgeOwnerState m_state;
    std::optional<PartyQuestSkyrimNativeLoadBridgeModuleLease> m_lease;

    bool m_operationActive{};
    bool m_shutdownRequested{};
    bool m_shutdownDriverActive{};
    std::thread::id m_operationThread{};
    std::thread::id m_shutdownDriverThread{};
    uint64_t m_activeEffectSequence{};
};

static_assert(sizeof(PartyQuestSkyrimNativeLoadBridgeOwnerStatus) == 1u);
static_assert(sizeof(
    PartyQuestSkyrimNativeLoadBridgeShutdownDrainStatus) == 1u);
static_assert(std::is_standard_layout_v<
    PartyQuestSkyrimNativeLoadBridgeOwnerResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestSkyrimNativeLoadBridgeOwnerResult>);
static_assert(std::is_standard_layout_v<
    PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestSkyrimNativeLoadBridgeShutdownDrainResult>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestSkyrimNativeLoadBridgeOwner>);
static_assert(!std::is_copy_assignable_v<
    PartyQuestSkyrimNativeLoadBridgeOwner>);
static_assert(!std::is_move_constructible_v<
    PartyQuestSkyrimNativeLoadBridgeOwner>);
static_assert(!std::is_move_assignable_v<
    PartyQuestSkyrimNativeLoadBridgeOwner>);
