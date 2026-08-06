#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

#include <cstdint>
#include <optional>

/**
 * Result of one trusted Papyrus/VM runtime observation.
 *
 * Unsupported means the local runtime/version cannot provide the required
 * observation contract at all. Unknown is a temporary inability to sample.
 * Busy/Idle are valid samples and must carry a self-consistent pending-work
 * count. QuestEventGeneration is a monotonically increasing generation owned
 * by the concrete runtime observer.
 */
enum class PartyQuestPapyrusRuntimeObservationStatus : uint8_t
{
    Unsupported,
    Unknown,
    Busy,
    Idle
};

struct PartyQuestPapyrusRuntimeObservation
{
    PartyQuestPapyrusRuntimeObservationStatus Status{
        PartyQuestPapyrusRuntimeObservationStatus::Unknown};
    uint32_t PendingWorkCount{};
    uint64_t QuestEventGeneration{};

    [[nodiscard]] bool IsSelfConsistent() const noexcept
    {
        switch (Status)
        {
        case PartyQuestPapyrusRuntimeObservationStatus::Unsupported:
        case PartyQuestPapyrusRuntimeObservationStatus::Unknown:
            return PendingWorkCount == 0;
        case PartyQuestPapyrusRuntimeObservationStatus::Busy:
            return PendingWorkCount != 0;
        case PartyQuestPapyrusRuntimeObservationStatus::Idle:
            return PendingWorkCount == 0;
        }
        return false;
    }
};

/**
 * Game-specific read-only observation boundary.
 *
 * Implementations may inspect only version-validated runtime state. This
 * interface does not authorize arbitrary VM memory walks or script execution.
 */
class PartyQuestPapyrusRuntimeObserver
{
public:
    virtual ~PartyQuestPapyrusRuntimeObserver() = default;

    [[nodiscard]] virtual PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t aTransactionId) noexcept = 0;
};

enum class PartyQuestPapyrusRuntimeMonitorStatus : uint8_t
{
    Inactive,
    Waiting,
    Quiescent,
    TimedOut,
    Unsupported,
    InvalidTransaction,
    InvalidClock,
    InvalidObservation
};

/**
 * Deterministic timeout/fail-closed orchestration over a trusted runtime
 * observer and PartyQuestPapyrusQuiescenceTracker.
 *
 * The caller supplies monotonic milliseconds. A clock regression, unsupported
 * runtime, malformed sample or timeout is terminal for the current monitor
 * session and can never produce an authorization. Unknown observations reset
 * stability and continue waiting until the deadline. Busy/Idle samples are fed
 * into the existing stable-generation tracker.
 *
 * This class deliberately does not restore checkpoints itself. A future
 * guarded-session integration must map terminal post-mutation outcomes to exact
 * PreRepair recovery before new work is admitted.
 */
class PartyQuestPapyrusRuntimeMonitor final
{
public:
    explicit PartyQuestPapyrusRuntimeMonitor(
        PartyQuestPapyrusRuntimeObserver& aObserver) noexcept
        : m_observer(aObserver)
    {
    }

    [[nodiscard]] bool Begin(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs) noexcept;

    [[nodiscard]] PartyQuestPapyrusRuntimeMonitorStatus Poll(
        uint64_t aTransactionId,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] std::optional<PartyQuestPapyrusQuiescenceAuthorization>
    Authorize() const noexcept;

    [[nodiscard]] bool Consume(
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept;

    [[nodiscard]] bool Reset(uint64_t aTransactionId) noexcept;

    [[nodiscard]] uint64_t GetTransactionId() const noexcept
    {
        return m_transactionId;
    }

    [[nodiscard]] PartyQuestPapyrusRuntimeMonitorStatus GetStatus() const noexcept
    {
        return m_status;
    }

private:
    [[nodiscard]] bool RestartTracker() noexcept;
    PartyQuestPapyrusRuntimeMonitorStatus EnterTerminal(
        PartyQuestPapyrusRuntimeMonitorStatus aStatus) noexcept;
    void Clear() noexcept;

    PartyQuestPapyrusRuntimeObserver& m_observer;
    PartyQuestPapyrusQuiescenceTracker m_tracker;
    uint64_t m_transactionId{};
    uint64_t m_startedAtMs{};
    uint64_t m_lastNowMs{};
    uint64_t m_timeoutMs{};
    PartyQuestPapyrusRuntimeMonitorStatus m_status{
        PartyQuestPapyrusRuntimeMonitorStatus::Inactive};
};
