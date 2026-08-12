#pragma once

#include <cstdint>
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

    [[nodiscard]] uint64_t GetGeneration() const noexcept;
    [[nodiscard]] uint64_t Invalidate() noexcept;
    [[nodiscard]] std::optional<ExecutionLease> TryAcquire(
        uint64_t aExpectedGeneration) const noexcept;

private:
    mutable std::shared_mutex m_mutex;
    uint64_t m_generation{1};
};
