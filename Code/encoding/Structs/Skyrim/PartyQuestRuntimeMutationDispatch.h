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
     * that lifecycle/load-order integration invalidates.
     */
    [[nodiscard]] static PartyQuestRuntimeMutationDispatchResult Dispatch(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestRuntimeApplyRequest& acCurrentRequest,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const CompatibilityObserver& acObserver,
        const MutationExecutor& acExecutor);

    /**
     * Explicit-fence seam for deterministic unit tests. Production integration
     * should use the overload above so it cannot accidentally create an
     * unrelated generation domain.
     */
    [[nodiscard]] static PartyQuestRuntimeMutationDispatchResult Dispatch(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestRuntimeApplyRequest& acCurrentRequest,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        PartyQuestRuntimeGenerationFence& aGenerationFence,
        const CompatibilityObserver& acObserver,
        const MutationExecutor& acExecutor);
};
