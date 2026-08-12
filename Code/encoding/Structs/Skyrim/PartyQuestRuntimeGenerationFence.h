#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>

class PartyQuestRuntimeGenerationFence final
{
public:
    class ExecutionLease final
    {
    public:
        ExecutionLease(ExecutionLease&&) noexcept = default;
        ExecutionLease& operator=(ExecutionLease&&) noexcept = default;
        ExecutionLease(const ExecutionLease&) = delete;
        ExecutionLease& operator=(const ExecutionLease&) = delete;

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

        ExecutionLease(
            std::shared_lock<std::shared_mutex>&& aLock,
            uint64_t aGeneration) noexcept
            : m_lock(std::move(aLock))
            , m_generation(aGeneration)
        {
        }

        std::shared_lock<std::shared_mutex> m_lock;
        uint64_t m_generation{};
    };

    /**
     * Exclusive lifecycle/resolver transition lease.
     *
     * Construction advances the process-local generation while holding the
     * exclusive side of the same mutex used by ExecutionLease. Keep this lease
     * alive for the complete state rebuild/teardown so a new dispatch cannot
     * observe the new generation until the corresponding runtime state is fully
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

    [[nodiscard]] uint64_t GetGeneration() const noexcept;

    /** Advance the generation and release the invalidation barrier immediately. */
    [[nodiscard]] uint64_t Invalidate() noexcept;

    /**
     * Advance the generation and retain the exclusive invalidation barrier until
     * the returned lease is destroyed.
     */
    [[nodiscard]] InvalidationLease BeginInvalidation() noexcept;

    [[nodiscard]] std::optional<ExecutionLease> TryAcquire(
        uint64_t aExpectedGeneration) const noexcept;

private:
    [[nodiscard]] uint64_t AdvanceGenerationLocked() noexcept;

    mutable std::shared_mutex m_mutex;
    uint64_t m_generation{1};
};
