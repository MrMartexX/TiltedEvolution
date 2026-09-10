#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>
#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

/**
 * Process/client-session aggregate for equal-party quest runtime work.
 *
 * This is the single production owner above PartyQuestRuntimeSessionOwner. The
 * nested session owner owns the durable apply journal, checkpoint/recovery state,
 * SaveGuard integration and workspace lease. This aggregate additionally owns
 * lifecycle admission, deferred work, the expensive compatibility cache and the
 * concrete runtime observer/executor callbacks installed by the Skyrim client.
 *
 * Every deferred operation is stamped with both the process generation and the
 * owner epoch. ExecuteNext() acquires PartyQuestRuntimeGenerationFence::ExecutionLease
 * before the final validation callback and keeps it through the executor callback.
 * Lifecycle invalidation uses the exclusive side of that exact fence, eliminating
 * the validate -> LoadGame/disconnect -> Skyrim access TOCTOU window.
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

    enum class EnqueueStatus : uint8_t
    {
        Queued,
        AdmissionClosed,
        InvalidOperation,
        SynchronizationFailed,
        ResourceLimitExceeded
    };

    enum class ExecuteStatus : uint8_t
    {
        Executed,
        Empty,
        AdmissionClosed,
        StaleGeneration,
        ValidationRejected,
        CallbackFailed,
        SynchronizationFailed
    };

    enum class BootstrapStatus : uint8_t
    {
        Bound,
        Rejected,
        StaleGeneration,
        Exception
    };

    struct EnqueueResult
    {
        EnqueueStatus Status{EnqueueStatus::InvalidOperation};
        uint64_t OperationId{};
        uint64_t RuntimeGeneration{};
        uint64_t OwnerEpoch{};
    };

    struct BootstrapResult
    {
        BootstrapStatus Status{BootstrapStatus::Rejected};
        uint64_t RuntimeGeneration{};
    };

    using Validation = std::function<bool()>;
    using Operation = std::function<void()>;
    using BootstrapAction = std::function<bool()>;
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
     * Called by PartyQuestRuntimeSessionOwner while it owns the exclusive process
     * generation invalidation lease. It must never try to acquire that lease again.
     */
    void ObserveSessionLifecycleBoundary(
        PartyQuestRuntimeLifecycleEvent aEvent,
        uint64_t aGeneration) noexcept;

    /** Mark a successful durable process-session bind for the exact generation. */
    void MarkRuntimeSessionBound(uint64_t aGeneration) noexcept;

    [[nodiscard]] BootstrapResult RunBootstrap(
        uint64_t aExpectedGeneration,
        const BootstrapAction& acAction) noexcept;

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

    [[nodiscard]] EnqueueResult Enqueue(
        Validation aValidation,
        Operation aOperation) noexcept;
    [[nodiscard]] ExecuteStatus ExecuteNext() noexcept;

    /**
     * Safe callback for external queues. Its lifetime gate is held through the
     * complete ExecuteNext() call, so invoking it after owner destruction is a no-op.
     */
    [[nodiscard]] std::function<void()> MakeExecuteNextCallback() noexcept;

    [[nodiscard]] bool IsAcceptingOperations() const noexcept;
    [[nodiscard]] bool IsShutdown() const noexcept;
    [[nodiscard]] uint64_t GetOwnerEpoch() const noexcept;
    [[nodiscard]] size_t GetPendingOperationCount() const noexcept;

    /** Read-only owned adapters; both are point-of-use generation leased. */
    [[nodiscard]] bool ObserveSkyrimRuntime() noexcept;
    [[nodiscard]] std::optional<PartyQuestPapyrusRuntimeObservation>
    ObservePapyrusRuntime(uint64_t aTransactionId) noexcept;

private:
    struct DeferredOperation
    {
        uint64_t OperationId{};
        uint64_t RuntimeGeneration{};
        uint64_t OwnerEpoch{};
        Validation Validate;
        Operation Execute;
    };

    struct CallbackLifetime
    {
        std::mutex Mutex;
        PartyQuestRuntimeOwner* Owner{};
        bool Alive{};
    };

    static constexpr size_t MaxDeferredOperations = 1024;

    void ApplyBoundaryStateLocked(ClientBoundary aBoundary, uint64_t aGeneration) noexcept;
    [[nodiscard]] bool CanAcceptLocked() const noexcept;
    void CloseCallbackLifetime() noexcept;

    mutable std::mutex m_mutex;
    PartyQuestRuntimeSessionOwner m_sessionOwner;
    PartyQuestCompatibilityEnvironmentCache m_compatibilityEnvironment;
    std::deque<DeferredOperation> m_deferredOperations;
    SkyrimObserver m_skyrimObserver;
    PapyrusObserver m_papyrusObserver;
    RuntimeExecutor m_executor;
    std::shared_ptr<CallbackLifetime> m_callbackLifetime;
    uint64_t m_ownerEpoch{1};
    uint64_t m_nextOperationId{1};
    uint64_t m_boundGeneration{};
    bool m_connected{};
    bool m_inParty{};
    bool m_runtimeSessionBound{};
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
