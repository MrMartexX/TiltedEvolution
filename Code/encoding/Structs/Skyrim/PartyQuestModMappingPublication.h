#pragma once

#include <atomic>

/**
 * Fail-closed publication bit for the server<->local FormID mapping.
 *
 * A rebuild revokes the previous mapping before any candidate work starts.
 * Only a completely built and atomically published candidate may mark the
 * mapping ready again. Any exception/early return before Commit() therefore
 * leaves all conversion entry points unavailable instead of exposing stale or
 * partially rebuilt identity state.
 */
class PartyQuestModMappingPublication final
{
public:
    void BeginRebuild() noexcept
    {
        m_ready.store(false, std::memory_order_release);
    }

    void Commit() noexcept
    {
        m_ready.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool IsReady() const noexcept
    {
        return m_ready.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool m_ready{false};
};
