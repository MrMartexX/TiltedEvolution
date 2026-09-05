#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>
#include <Structs/Skyrim/PartyQuestDeferredWorld.h>
#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

/**
 * Process/client-session aggregate for equal-party quest runtime work.
 *
 * This is the single production owner above PartyQuestRuntimeSessionOwner. The
 * nested session owner owns the durable apply journal, checkpoint/recovery state,
 * SaveGuard integration and workspace lease. This aggregate additionally owns
 * lifecycle admission, the typed deferred-world queue, the expensive compatibility
 * cache and the concrete runtime observer/executor callbacks installed by the
 * Skyrim client.
 *
 * Arbitrary deferred callbacks are intentionally not accepted. Runtime deferred
 * work must use PartyQuestDeferredWorldQueue's immutable campaign/profile/
 * generation/transaction/revision identity envelope and its point-of-use durable
 * revalidation path. Lifecycle boundaries retire that complete typed state.
 */
class PartyQuestRuntimeOwner final
{
public:
    enum class ClientBoundary : uint8_t
    {
        Connected,
        PartyJoined,
        Disconnected,
        PartyLeft,
        CampaignChanged,
        RuntimeIdentityChanged,
        Shutdown
    };

    enum class BoundaryStatus : uint8_t
    {
        Applied,
        AlreadyShutdown,
        SynchronizationFailed
    };

    using SkyrimObserver = std::function<bool()>;
    using PapyrusObserver = std::function<PartyQuestPapyrusRuntimeObservation(uint64_t)>;
    using RuntimeExecutor = std::function<bool(const PartyQuestRuntimeApplyRequest&)>;

    PartyQuestRuntimeOwner() noexcept;
    ~PartyQuestRuntimeOwner() noexcept;

    PartyQuestRuntimeOwner(const PartyQuestRuntimeOwner&) = delete;
    PartyQuestRuntimeOwner& operator=(const PartyQuestRuntimeOwner&) = delete;
    PartyQuestRuntimeOwner(PartyQuestRuntimeOwner&&) = delete;
    PartyQuestRuntimeOwner& operator=(PartyQuestRuntimeOwner&&) = delete;

    [[nodiscard]] static PartyQuestRuntimeOwner& GetProcessOwner() noexcept;

    [[nodiscard]] PartyQuestRuntimeSessionOwner& GetSessionOwner() noexcept
    {
        return m_sessionOwner;
    }

    [[nodiscard]] const PartyQuestRuntimeSessionOwner& GetSessionOwner() const noexcept
    {
        return m_sessionOwner;
    }

    [[nodiscard]] PartyQuestCompatibilityEnvironmentCache&
    GetCompatibilityEnvironmentCache() noexcept
    {
        return m_compatibilityEnvironment;
    }

    [[nodiscard]] BoundaryStatus ApplyClientBoundary(ClientBoundary aBoundary) noexcept;

    /**
     * Closes local admission before PartyQuestRuntimeSessionOwner attempts the
     * exclusive generation transition. This must remain non-blocking so a
     * same-thread/reentrant lifecycle callback cannot leave stale admission open
     * merely because the current thread already owns an execution lease.
     */
    [[nodiscard]] uint64_t CloseSessionLifecycleAdmission(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept;

    /**
     * Installs concrete client adapters into this owner. The mutation executor is
     * deliberately stored without a public direct-call API: production mutation
     * must still enter through PartyQuestRuntimeMutationDispatchGate and its
     * durable barrier before any future integration consumes this executor.
     */
    void ConfigureRuntimeAdapters(
        SkyrimObserver aSkyrimObserver,
        PapyrusObserver aPapyrusObserver,
        RuntimeExecutor aExecutor) noexcept;
    void ClearRuntimeAdapters() noexcept;

    [[nodiscard]] bool IsAcceptingOperations() const noexcept;
    [[nodiscard]] bool IsShutdown() const noexcept;
    [[nodiscard]] uint64_t GetOwnerEpoch() const noexcept;
    [[nodiscard]] size_t GetPendingOperationCount() const noexcept;

    /** Read-only owned adapters; both are point-of-use generation leased. */
    [[nodiscard]] bool ObserveSkyrimRuntime() noexcept;
    [[nodiscard]] std::optional<PartyQuestPapyrusRuntimeObservation>
    ObservePapyrusRuntime(uint64_t aTransactionId) noexcept;

private:
    friend class PartyQuestRuntimeSessionBootstrap;
    friend class PartyQuestRuntimeSessionOwner;
    friend class PartyQuestRuntimeOwnerTestAccess;
    friend class PartyQuestRuntimeSessionOwnerTestAccess;

    [[nodiscard]] bool PublishRuntimeSessionBound(
        uint64_t aGeneration,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const PartyQuestRuntimeSessionOwnerBindResult& acBindResult) noexcept;
    /** Completes only the exact revocation epoch holding the exclusive fence. */
    void CompleteSessionLifecycleInvalidation(
        uint64_t aOwnerEpoch,
        uint64_t aGeneration) noexcept;
    void ApplyBoundaryStateLocked(ClientBoundary aBoundary) noexcept;
    [[nodiscard]] bool CanAcceptLocked() const noexcept;

    mutable std::mutex m_mutex;
    PartyQuestRuntimeSessionOwner m_sessionOwner;
    PartyQuestCompatibilityEnvironmentCache m_compatibilityEnvironment;
    PartyQuestDeferredWorldQueue m_deferredWorld;
    SkyrimObserver m_skyrimObserver;
    PapyrusObserver m_papyrusObserver;
    RuntimeExecutor m_executor;
    uint64_t m_ownerEpoch{1};
    uint64_t m_boundGeneration{};
    std::optional<PartyQuestCampaignId> m_boundCampaignId;
    std::optional<PartyQuestPlayerProfileId> m_boundPlayerProfileId;
    bool m_connected{};
    bool m_inParty{};
    bool m_runtimeSessionBound{};
    bool m_lifecycleInvalidationPending{};
    bool m_shutdown{};
};

/**
 * Stateless QuestService-facing handle. The actual worker/cache lifetime is
 * owned exclusively by PartyQuestRuntimeOwner.
 */
class PartyQuestRuntimeCompatibilityEnvironmentHandle final
{
public:
    [[nodiscard]] bool Start(
        PartyQuestCompatibilityEnvironmentSnapshot aSnapshot) noexcept
    {
        return PartyQuestRuntimeOwner::GetProcessOwner()
            .GetCompatibilityEnvironmentCache()
            .Start(std::move(aSnapshot));
    }

    void Stop() noexcept
    {
        PartyQuestRuntimeOwner::GetProcessOwner()
            .GetCompatibilityEnvironmentCache()
            .Stop();
    }

    [[nodiscard]] PartyQuestCompatibilityEnvironmentCacheStatus GetStatus() const noexcept
    {
        return PartyQuestRuntimeOwner::GetProcessOwner()
            .GetCompatibilityEnvironmentCache()
            .GetStatus();
    }

    [[nodiscard]] std::optional<PartyQuestCompatibilityEnvironmentFingerprints>
    GetReady() const noexcept
    {
        return PartyQuestRuntimeOwner::GetProcessOwner()
            .GetCompatibilityEnvironmentCache()
            .GetReady();
    }

    [[nodiscard]] uint64_t GetComputationCount() const noexcept
    {
        return PartyQuestRuntimeOwner::GetProcessOwner()
            .GetCompatibilityEnvironmentCache()
            .GetComputationCount();
    }
};
