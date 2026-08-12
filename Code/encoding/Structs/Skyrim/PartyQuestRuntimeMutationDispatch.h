#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
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
    ExecutorRejected
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
 * after the durable arm immediately before the executor callback. If runtime
 * compatibility changes while the durable state is being persisted, the
 * callback is not invoked and the armed recovery barrier remains fail-closed.
 * No reusable dispatch token escapes this call.
 */
class PartyQuestRuntimeMutationDispatchGate final
{
public:
    using CompatibilityObserver = std::function<std::optional<PartyQuestRuntimeCompatibilityFacts>(
        const GameId&)>;
    using MutationExecutor = std::function<bool(const PartyQuestRuntimeApplyRequest&)>;

    [[nodiscard]] static PartyQuestRuntimeMutationDispatchResult Dispatch(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestRuntimeApplyRequest& acCurrentRequest,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const CompatibilityObserver& acObserver,
        const MutationExecutor& acExecutor);
};
