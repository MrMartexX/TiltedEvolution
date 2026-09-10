#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>

class PartyQuestRuntimeGenerationFenceTestAccess;

class PartyQuestRuntimeGenerationFence final
{
public:
    struct LifecycleTransitionTicket
    {
        uint64_t Ticket{};
        uint64_t Generation{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Ticket != 0 && Generation != 0;
        }
    };

    class ExecutionLease final
    {
    public:
        ExecutionLease(ExecutionLease&& aOther) noexcept;
        ExecutionLease& operator=(ExecutionLease&& aOther) noexcept;
        ExecutionLease(const ExecutionLease&) = delete;
        ExecutionLease& operator=(const ExecutionLease&) = delete;
        ~ExecutionLease() noexcept;

        [[nodiscard]] uint64_t GetGeneration() const noexcept
        {
            return m_generation;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_lock.owns_lock() && m_generation != 0 && m_owner != nullptr;
        }

    private:
        friend class PartyQuestRuntimeGenerationFence;

        ExecutionLease(
            std::shared_lock<std::shared_mutex>&& aLock,
            uint64_t aGeneration,
            const PartyQuestRuntimeGenerationFence* apOwner) noexcept;

        void ReleaseThreadRegistration() noexcept;

        std::shared_lock<std::shared_mutex> m_lock;
        const PartyQuestRuntimeGenerationFence* m_owner{};
        uint64_t m_generation{};
    };

    /**
     * Exclusive lifecycle/resolver transition lease for synchronous rebuilds.
     *
     * Construction advances the process-local generation while holding the
     * exclusive side of the same mutex used by ExecutionLease. Keep this lease
     * alive for the complete synchronous state rebuild/teardown so a new
     * dispatch cannot observe the new generation until runtime state is fully
     * published.
     */
    class InvalidationLease final
    {
    public:
        InvalidationLease(InvalidationLease&&) noexcept = default;
        InvalidationLease& operator=(InvalidationLease&&) noexcept = default;
        InvalidationLease(const InvalidationLease&) = delete;
        InvalidationLease& operator=(const InvalidationLease&) = delete;

        [[nodiscard]] uint64_t GetGeneration() const noexcept
        {
            return m_generation;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_lock.owns_lock() && m_generation != 0;
        }

    private:
        friend class PartyQuestRuntimeGenerationFence;

        InvalidationLease() noexcept = default;

        InvalidationLease(
            std::unique_lock<std::shared_mutex>&& aLock,
            uint64_t aGeneration) noexcept
            : m_lock(std::move(aLock))
            , m_generation(aGeneration)
        {
        }

        std::unique_lock<std::shared_mutex> m_lock;
        uint64_t m_generation{};
    };

    /** Shared process fence used by production lifecycle and dispatch code. */
    [[nodiscard]] static PartyQuestRuntimeGenerationFence& GetProcessFence() noexcept;

    /** Zero means the synchronization domain has failed closed. */
    [[nodiscard]] uint64_t GetGeneration() const noexcept;
    [[nodiscard]] bool IsPoisoned() const noexcept
    {
        return m_poisoned.load(std::memory_order_acquire);
    }

    /**
     * True only while the calling thread owns a shared execution lease on the
     * process fence. Lifecycle code uses this to reject reentrant invalidation
     * instead of self-deadlocking on std::shared_mutex.
     */
    [[nodiscard]] bool IsExecutionLeaseHeldByCurrentThread() const noexcept;

    /** Advance the generation and release the invalidation barrier immediately. */
    [[nodiscard]] uint64_t Invalidate() noexcept;

    /**
     * Tries to acquire the exclusive lifecycle/resolver barrier. Any mutex-layer
     * exception irreversibly poisons this fence so future runtime dispatch is
     * denied rather than continuing without a proven synchronization domain.
     * Reentrant invalidation from a process execution callback fails closed
     * without waiting on its own shared lease.
     */
    [[nodiscard]] std::optional<InvalidationLease>
    TryBeginInvalidation() noexcept;

    /**
     * Compatibility wrapper. Production state mutation must prefer
     * TryBeginInvalidation() and explicitly reject an invalid lease.
     */
    [[nodiscard]] InvalidationLease BeginInvalidation() noexcept;

    /**
     * Publish an asynchronous engine lifecycle transition without transferring
     * mutex ownership across threads. The generation advances at admission and
     * TryAcquire() remains fail-closed until the exact ticket is completed.
     */
    [[nodiscard]] LifecycleTransitionTicket BeginLifecycleTransition() noexcept;
    [[nodiscard]] bool CompleteLifecycleTransition(
        LifecycleTransitionTicket aTicket) noexcept;
    [[nodiscard]] bool IsLifecycleTransitionPending() const noexcept;

    [[nodiscard]] std::optional<ExecutionLease> TryAcquire(
        uint64_t aExpectedGeneration) const noexcept;

private:
    friend class PartyQuestRuntimeGenerationFenceTestAccess;

    void Poison() const noexcept
    {
        m_poisoned.store(true, std::memory_order_release);
    }

    void RegisterExecutionLeaseOnCurrentThread() const noexcept;
    void UnregisterExecutionLeaseOnCurrentThread() const noexcept;

    [[nodiscard]] uint64_t AdvanceGenerationLocked() noexcept;
    [[nodiscard]] uint64_t AllocateLifecycleTicketLocked() noexcept;

    static thread_local uint32_t s_processExecutionLeaseDepth;

    mutable std::shared_mutex m_mutex;
    mutable std::atomic_bool m_poisoned{false};
    uint64_t m_generation{1};
    uint64_t m_lifecycleTicket{};
    uint64_t m_nextLifecycleTicket{1};
};
