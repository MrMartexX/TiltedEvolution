#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <functional>
#include <optional>

enum class PartyQuestRuntimeMutationDispatchStatus : uint8_t
{
    Dispatched,
    InvalidRequest,
    InvalidRuntimeState,
    ProcessOwnerMismatch,
    GuardMismatch,
    ObservationUnavailable,
    CompatibilityRejected,
    CompatibilityAuthorityMismatch,
    ArmFailed,
    DispatchContextLost,
    ExecutorRejected,
    RuntimeGenerationChanged
};

struct PartyQuestRuntimeMutationDispatchResult
{
    PartyQuestRuntimeMutationDispatchStatus Status{
        PartyQuestRuntimeMutationDispatchStatus::InvalidRuntimeState};
    PartyQuestRuntimeCompatibilityStatus CompatibilityStatus{
        PartyQuestRuntimeCompatibilityStatus::UnknownQuest};
    PartyQuestRuntimeGuardResult ArmResult;
    bool MutationBarrierArmed{};
    bool MutationInvoked{};

    [[nodiscard]] bool WasDispatched() const noexcept
    {
        return Status == PartyQuestRuntimeMutationDispatchStatus::Dispatched &&
            MutationInvoked;
    }
};

/**
 * Point-of-use runtime mutation gate.
 *
 * ArmRuntimeMutation() is only the durable "mutation may have occurred" barrier;
 * it is not sufficient authority to call Skyrim. A future canonical mutation
 * executor must be invoked synchronously through this gate.
 *
 * The compatibility observer is sampled twice: once before arming, then again
 * after the durable arm immediately before the executor callback. Both samples
 * must be contained in the same process-local runtime generation. The final
 * structural/physical guard check and executor callback run while an execution
 * lease pins that generation, so a concurrent lifecycle/resolver invalidation
 * cannot silently cross the observation-to-dispatch boundary.
 *
 * The production entrypoint additionally requires the exact guarded session
 * owned by PartyQuestRuntimeSessionOwner::GetProcessOwner(). Any call through
 * the explicit-fence seam that touches either the shared process generation
 * fence or the shared process SaveGuard is subject to the same rule and must use
 * both process resources together. This prevents a private/test session from
 * mixing one process-global authority with an unrelated lifecycle domain.
 *
 * The generation is deliberately process-local and is never durable authority.
 * No reusable dispatch token escapes this call.
 */
class PartyQuestRuntimeMutationDispatchGate final
{
public:
    using CompatibilityObserver = std::function<std::optional<PartyQuestRuntimeCompatibilityFacts>(
        const GameId&)>;
    using MutationExecutor = std::function<bool(const PartyQuestRuntimeApplyRequest&)>;

    /**
     * Production entrypoint. It always uses the shared process generation fence
     * and requires the exact shared process-owned guarded session that lifecycle
     * integration fences. If production bootstrap has not bound that owner, the
     * call fails closed before observation or durable mutation arming.
     */
    [[nodiscard]] static PartyQuestRuntimeMutationDispatchResult Dispatch(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestRuntimeApplyRequest& acCurrentRequest,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const CompatibilityObserver& acObserver,
        const MutationExecutor& acExecutor);

    /**
     * Explicit-fence seam for deterministic unit tests. A fully local SaveGuard
     * plus local generation fence may use this overload without a process owner.
     * If either supplied component is the shared process resource, both process
     * resources and the exact process-owned guarded session are mandatory.
     */
    [[nodiscard]] static PartyQuestRuntimeMutationDispatchResult Dispatch(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestRuntimeApplyRequest& acCurrentRequest,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        PartyQuestRuntimeGenerationFence& aGenerationFence,
        const CompatibilityObserver& acObserver,
        const MutationExecutor& acExecutor);
};
