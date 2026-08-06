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
 * Implementations may inspect only version-validated runtime state. Merely
 * implementing this public interface is not sufficient to authorize a live
 * repair transition; production observers must also possess the encapsulated
 * observer authorization below.
 */
class PartyQuestPapyrusRuntimeObserver
{
public:
    virtual ~PartyQuestPapyrusRuntimeObserver() = default;

    [[nodiscard]] virtual PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t aTransactionId) noexcept = 0;
};

class PartyQuestSkyrimPapyrusRuntimeObserver;
class PartyQuestPapyrusRuntimeObserverTestAccess;

/**
 * Process-local capability binding an approved observer implementation to its
 * exact object instance. A fabricated implementation of the public observer
 * interface cannot construct this capability and therefore cannot authorize a
 * live runtime transition.
 *
 * The concrete Skyrim observer will be the production issuer once its VM
 * observation envelope is version-validated. Tests use a named friend-only
 * factory under Code/tests.
 */
class PartyQuestPapyrusRuntimeObserverAuthorization final
{
public:
    PartyQuestPapyrusRuntimeObserverAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_observer != nullptr;
    }

    [[nodiscard]] bool Matches(
        const PartyQuestPapyrusRuntimeObserver& acObserver) const noexcept
    {
        return IsVerified() && m_observer == &acObserver;
    }

private:
    friend class PartyQuestSkyrimPapyrusRuntimeObserver;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestPapyrusRuntimeObserverAuthorization(
        const PartyQuestPapyrusRuntimeObserver& acObserver) noexcept
        : m_observer(&acObserver)
    {
    }

    const PartyQuestPapyrusRuntimeObserver* m_observer{};
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
 * Deterministic timeout/fail-closed orchestration over a runtime observer and
 * PartyQuestPapyrusQuiescenceTracker.
 *
 * The caller supplies monotonic milliseconds. A clock regression, event-
 * generation regression, unsupported runtime, malformed sample or timeout is
 * terminal for the current monitor session and can never produce authoritative
 * quiescence. Unknown observations reset stability and continue waiting until
 * the deadline. Busy/Idle samples are fed into the stable-generation tracker.
 *
 * Begin() without observer authorization remains a diagnostic algorithm surface
 * for isolated tests only. ConsumeAuthoritative() succeeds exclusively for a
 * session begun with an exact observer-bound authorization.
 */
class PartyQuestPapyrusRuntimeMonitor final
{
public:
    explicit PartyQuestPapyrusRuntimeMonitor(
        PartyQuestPapyrusRuntimeObserver& aObserver) noexcept
        : m_observer(aObserver)
    {
    }

    /** Diagnostic-only session; cannot pass ConsumeAuthoritative(). */
    [[nodiscard]] bool Begin(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs) noexcept;

    /** Authoritative session for an exact approved observer instance. */
    [[nodiscard]] bool Begin(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs,
        const PartyQuestPapyrusRuntimeObserverAuthorization& acAuthorization) noexcept;

    [[nodiscard]] PartyQuestPapyrusRuntimeMonitorStatus Poll(
        uint64_t aTransactionId,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] std::optional<PartyQuestPapyrusQuiescenceAuthorization>
    Authorize() const noexcept;

    /** Diagnostic consume; does not assert observer trust. */
    [[nodiscard]] bool Consume(
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept;

    /** Live-control-plane consume; requires an authorized observer session. */
    [[nodiscard]] bool ConsumeAuthoritative(
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

    [[nodiscard]] bool IsAuthoritativeSession() const noexcept
    {
        return m_authoritativeObserver && m_transactionId != 0;
    }

private:
    [[nodiscard]] bool BeginInternal(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs,
        bool aAuthoritativeObserver) noexcept;
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
    uint64_t m_lastObservedGeneration{};
    bool m_hasObservedGeneration{};
    bool m_authoritativeObserver{};
    PartyQuestPapyrusRuntimeMonitorStatus m_status{
        PartyQuestPapyrusRuntimeMonitorStatus::Inactive};
};
