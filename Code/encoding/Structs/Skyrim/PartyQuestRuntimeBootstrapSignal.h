#pragma once

#include <atomic>

/**
 * Edge-triggered handoff for production runtime bootstrap retries.
 *
 * Producers publish only when new authoritative evidence may have arrived
 * (server repair/campaign evidence or completed Skyrim character load). The
 * game-thread consumer never scans for evidence on a timer: multiple edges may
 * coalesce because bootstrap always revalidates the complete current evidence
 * set under the runtime-generation lease before publication.
 */
class PartyQuestRuntimeBootstrapSignal final
{
public:
    void Publish() noexcept
    {
        m_pending.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool Consume() noexcept
    {
        return m_pending.exchange(false, std::memory_order_acq_rel);
    }

    void Reset() noexcept
    {
        m_pending.store(false, std::memory_order_release);
    }

private:
    std::atomic_bool m_pending{false};
};
