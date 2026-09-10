#pragma once

#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

#include <cstdint>
#include <optional>

/**
 * Logical Papyrus work domains that must all be observed before an
 * authoritative runtime observer may claim Idle.
 *
 * These are semantic domains, not Skyrim object offsets. A concrete adapter
 * may map multiple runtime queues/containers into one domain only after that
 * mapping, locking and supported-version contract have been verified.
 */
enum class PartyQuestPapyrusRuntimeWorkDomain : uint32_t
{
    None = 0,
    FunctionMessageQueues = 1u << 0,
    VmTaskQueue = 1u << 1,
    UiWaitingQueue = 1u << 2,
    SuspendResumeQueues = 1u << 3,
    RunningStacks = 1u << 4,
    LatentReturnQueue = 1u << 5
};

inline constexpr uint32_t kPartyQuestPapyrusRuntimeRequiredWorkDomains =
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::FunctionMessageQueues) |
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::VmTaskQueue) |
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::UiWaitingQueue) |
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::SuspendResumeQueues) |
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks) |
    static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::LatentReturnQueue);

[[nodiscard]] constexpr bool IsPartyQuestPapyrusRuntimeWorkDomainMaskValid(
    uint32_t aMask) noexcept
{
    return (aMask & ~kPartyQuestPapyrusRuntimeRequiredWorkDomains) == 0;
}

[[nodiscard]] constexpr bool HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(
    uint32_t aMask) noexcept
{
    return aMask == kPartyQuestPapyrusRuntimeRequiredWorkDomains;
}

class PartyQuestSkyrimPapyrusRuntimeProfileResolver;
class PartyQuestPapyrusRuntimeObserverAuthorization;
class PartyQuestPapyrusRuntimeObserverTestAccess;

/**
 * Process-local proof that the concrete Skyrim VM observation profile is safe
 * to use as an authoritative Papyrus source.
 *
 * A production resolver may issue this capability only after an exact runtime
 * identity match (using the already validated executable/version identity),
 * and only for a profile whose complete work-domain mapping, coherent snapshot
 * locking and independently authorized monotonic work-generation source are all
 * established. Unknown or partially mapped runtimes must not receive this
 * capability.
 *
 * RuntimeProfileFingerprint and GenerationSourceFingerprint are deterministic
 * contract identities, not secrets or authentication primitives. The
 * capability itself is constructor-confined so arbitrary callers cannot turn
 * guessed fingerprints into runtime authority.
 */
class PartyQuestPapyrusRuntimeProfileAuthorization final
{
public:
    PartyQuestPapyrusRuntimeProfileAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_runtimeProfileFingerprint != 0 &&
            m_generationSourceFingerprint != 0 &&
            m_exactRuntimeMatch &&
            HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(m_observedWorkDomains) &&
            m_coherentSnapshot;
    }

    [[nodiscard]] uint64_t GetRuntimeProfileFingerprint() const noexcept
    {
        return m_runtimeProfileFingerprint;
    }

    [[nodiscard]] uint64_t GetGenerationSourceFingerprint() const noexcept
    {
        return m_generationSourceFingerprint;
    }

private:
    friend class PartyQuestSkyrimPapyrusRuntimeProfileResolver;
    friend class PartyQuestPapyrusRuntimeObserverAuthorization;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestPapyrusRuntimeProfileAuthorization(
        uint64_t aRuntimeProfileFingerprint,
        bool aExactRuntimeMatch,
        uint32_t aObservedWorkDomains,
        bool aCoherentSnapshot,
        uint64_t aGenerationSourceFingerprint) noexcept
        : m_runtimeProfileFingerprint(aRuntimeProfileFingerprint)
        , m_generationSourceFingerprint(aGenerationSourceFingerprint)
        , m_observedWorkDomains(aObservedWorkDomains)
        , m_exactRuntimeMatch(aExactRuntimeMatch)
        , m_coherentSnapshot(aCoherentSnapshot)
    {
    }

    uint64_t m_runtimeProfileFingerprint{};
    uint64_t m_generationSourceFingerprint{};
    uint32_t m_observedWorkDomains{};
    bool m_exactRuntimeMatch{};
    bool m_coherentSnapshot{};
};

/**
 * Result of one trusted Papyrus/VM runtime observation.
 *
 * Unsupported means the local runtime/version cannot provide the required
 * observation contract at all. Unknown is a temporary inability to sample.
 * Busy/Idle are valid samples and must carry a self-consistent pending-work
 * count. QuestEventGeneration is a monotonically increasing generation owned
 * by the concrete runtime observer. ObservedWorkDomains identifies exactly
 * which logical VM work domains contributed to this sample.
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
    uint32_t ObservedWorkDomains{};

    [[nodiscard]] bool IsSelfConsistent() const noexcept
    {
        if (!IsPartyQuestPapyrusRuntimeWorkDomainMaskValid(ObservedWorkDomains))
            return false;

        switch (Status)
        {
        case PartyQuestPapyrusRuntimeObservationStatus::Unsupported:
            return PendingWorkCount == 0 && ObservedWorkDomains == 0;
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
 * Each observer lifetime receives a process-local nonzero InstanceNonce. The
 * nonce is identity, not a secret or authentication primitive: it prevents a
 * capability or monitor session issued to one object lifetime from becoming
 * valid again if another observer is later constructed at the same address.
 * Observers are deliberately non-copyable/non-movable so this identity cannot
 * silently migrate between objects.
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

    PartyQuestPapyrusRuntimeObserver(
        const PartyQuestPapyrusRuntimeObserver&) = delete;
    PartyQuestPapyrusRuntimeObserver& operator=(
        const PartyQuestPapyrusRuntimeObserver&) = delete;
    PartyQuestPapyrusRuntimeObserver(
        PartyQuestPapyrusRuntimeObserver&&) = delete;
    PartyQuestPapyrusRuntimeObserver& operator=(
        PartyQuestPapyrusRuntimeObserver&&) = delete;

    [[nodiscard]] uint64_t GetInstanceNonce() const noexcept
    {
        return m_instanceNonce;
    }

    [[nodiscard]] virtual PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t aTransactionId) noexcept = 0;

protected:
    PartyQuestPapyrusRuntimeObserver() noexcept;

private:
    uint64_t m_instanceNonce{};
};

class PartyQuestSkyrimPapyrusRuntimeObserver;

/**
 * Process-local capability binding an approved observer implementation to its
 * exact object lifetime and an independently verified Skyrim runtime profile.
 * A fabricated implementation of the public observer interface cannot
 * construct this capability, and even the production Skyrim observer cannot
 * authorize live control without a valid runtime-profile capability.
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
        return m_observer != nullptr &&
            m_observerInstanceNonce != 0 &&
            m_runtimeProfileFingerprint != 0;
    }

    [[nodiscard]] bool Matches(
        const PartyQuestPapyrusRuntimeObserver& acObserver) const noexcept
    {
        return IsVerified() &&
            m_observer == &acObserver &&
            m_observerInstanceNonce == acObserver.GetInstanceNonce();
    }

    [[nodiscard]] uint64_t GetRuntimeProfileFingerprint() const noexcept
    {
        return m_runtimeProfileFingerprint;
    }

private:
    friend class PartyQuestSkyrimPapyrusRuntimeObserver;
    friend class PartyQuestPapyrusRuntimeObserverTestAccess;

    explicit PartyQuestPapyrusRuntimeObserverAuthorization(
        const PartyQuestPapyrusRuntimeObserver& acObserver,
        const PartyQuestPapyrusRuntimeProfileAuthorization& acRuntimeProfile) noexcept
    {
        if (!acRuntimeProfile.IsVerified())
            return;

        m_observer = &acObserver;
        m_observerInstanceNonce = acObserver.GetInstanceNonce();
        m_runtimeProfileFingerprint = acRuntimeProfile.GetRuntimeProfileFingerprint();
    }

    const PartyQuestPapyrusRuntimeObserver* m_observer{};
    uint64_t m_observerInstanceNonce{};
    uint64_t m_runtimeProfileFingerprint{};
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
 * The caller supplies monotonic milliseconds. A clock regression, observer-
 * lifetime change, event-generation regression, unsupported runtime, malformed
 * sample or timeout is terminal for the current monitor session and can never
 * produce authoritative quiescence. Unknown observations reset stability and
 * continue waiting until the deadline. Busy/Idle samples are fed into the
 * stable-generation tracker.
 *
 * Begin() without observer authorization remains a diagnostic algorithm surface
 * for isolated tests only. ConsumeAuthoritative() succeeds exclusively for a
 * session begun with an exact observer-lifetime-bound authorization whose
 * runtime profile also proved exact identity, complete logical work coverage,
 * coherent snapshot capability and an independently authorized monotonic
 * generation source. Such an authoritative session additionally requires every
 * Idle sample to carry the complete logical Papyrus work envelope; partial
 * coverage can never authorize live quiescence.
 *
 * Monitor state is deliberately non-copyable/non-movable because it owns the
 * tracker session and authoritative observer-lifetime binding. Duplicating that
 * state would create a second route to old quiescence evidence.
 */
class PartyQuestPapyrusRuntimeMonitor final
{
public:
    explicit PartyQuestPapyrusRuntimeMonitor(
        PartyQuestPapyrusRuntimeObserver& aObserver) noexcept
        : m_observer(aObserver)
    {
    }

    PartyQuestPapyrusRuntimeMonitor(
        const PartyQuestPapyrusRuntimeMonitor&) = delete;
    PartyQuestPapyrusRuntimeMonitor& operator=(
        const PartyQuestPapyrusRuntimeMonitor&) = delete;
    PartyQuestPapyrusRuntimeMonitor(
        PartyQuestPapyrusRuntimeMonitor&&) = delete;
    PartyQuestPapyrusRuntimeMonitor& operator=(
        PartyQuestPapyrusRuntimeMonitor&&) = delete;

    /** Diagnostic-only session; cannot pass ConsumeAuthoritative(). */
    [[nodiscard]] bool Begin(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs) noexcept;

    /** Authoritative session for an exact approved observer lifetime/profile. */
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

    /** Live-control-plane consume; requires a current authorized observer lifetime. */
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
        return m_authoritativeObserver &&
            m_transactionId != 0 &&
            m_authorizedObserverInstanceNonce != 0 &&
            m_authorizedObserverInstanceNonce == m_observer.GetInstanceNonce();
    }

private:
    [[nodiscard]] bool BeginInternal(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        uint64_t aTimeoutMs,
        uint64_t aAuthorizedObserverInstanceNonce) noexcept;
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
    uint64_t m_authorizedObserverInstanceNonce{};
    bool m_hasObservedGeneration{};
    bool m_authoritativeObserver{};
    PartyQuestPapyrusRuntimeMonitorStatus m_status{
        PartyQuestPapyrusRuntimeMonitorStatus::Inactive};
};
