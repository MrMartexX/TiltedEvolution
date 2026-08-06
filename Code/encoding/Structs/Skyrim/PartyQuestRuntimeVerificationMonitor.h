#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstdint>

class PartyQuestRuntimeGuardedSession;

enum class PartyQuestRuntimeVerificationMonitorStatus : uint8_t
{
    Inactive,
    Waiting,
    Stable,
    TimedOut,
    DivergenceBudgetExceeded,
    InvalidTransaction,
    InvalidClock,
    InvalidVerification
};

/**
 * Process-local bounded verification lease for a post-mutation runtime repair.
 *
 * The budget starts atomically with the guarded transition into Verifying. It is
 * deliberately not durable: after restart the already persisted
 * RuntimeMutationMayHaveOccurred marker routes the transaction through exact
 * crash recovery instead of attempting to resume an old verification window.
 *
 * Limits are fixed control-plane policy, not caller configuration. The monitor
 * counts total divergent observations during one verification window so an
 * alternating divergent/canonical sequence cannot livelock indefinitely. The
 * overall deadline is also enforced even after a Stable observation until the
 * guarded caller commits.
 *
 * Lifecycle mutation is private to PartyQuestRuntimeGuardedSession so callers
 * cannot reset/restart the window to extend the budget. Copy/move are forbidden
 * to prevent duplicated process-local verification state.
 */
class PartyQuestRuntimeVerificationMonitor final
{
public:
    static constexpr uint64_t kTimeoutMs = 10000;
    static constexpr uint32_t kMaxDivergentSamples = 3;

    PartyQuestRuntimeVerificationMonitor() noexcept = default;
    PartyQuestRuntimeVerificationMonitor(
        const PartyQuestRuntimeVerificationMonitor&) = delete;
    PartyQuestRuntimeVerificationMonitor& operator=(
        const PartyQuestRuntimeVerificationMonitor&) = delete;
    PartyQuestRuntimeVerificationMonitor(
        PartyQuestRuntimeVerificationMonitor&&) = delete;
    PartyQuestRuntimeVerificationMonitor& operator=(
        PartyQuestRuntimeVerificationMonitor&&) = delete;

    [[nodiscard]] uint64_t GetTransactionId() const noexcept
    {
        return m_transactionId;
    }

    [[nodiscard]] uint32_t GetDivergentSamples() const noexcept
    {
        return m_divergentSamples;
    }

    [[nodiscard]] PartyQuestRuntimeVerificationMonitorStatus GetStatus() const noexcept
    {
        return m_status;
    }

private:
    friend class PartyQuestRuntimeGuardedSession;

    [[nodiscard]] bool Begin(uint64_t aTransactionId, uint64_t aNowMs) noexcept;
    [[nodiscard]] PartyQuestRuntimeVerificationMonitorStatus Poll(
        uint64_t aTransactionId,
        uint64_t aNowMs) noexcept;
    [[nodiscard]] PartyQuestRuntimeVerificationMonitorStatus Observe(
        uint64_t aTransactionId,
        uint64_t aNowMs,
        PartyQuestRuntimeVerificationStatus aVerification) noexcept;
    void Cancel(uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestRuntimeVerificationMonitorStatus CheckTime(
        uint64_t aNowMs) noexcept;
    PartyQuestRuntimeVerificationMonitorStatus EnterTerminal(
        PartyQuestRuntimeVerificationMonitorStatus aStatus) noexcept;

    uint64_t m_transactionId{};
    uint64_t m_startedAtMs{};
    uint64_t m_lastNowMs{};
    uint32_t m_divergentSamples{};
    PartyQuestRuntimeVerificationMonitorStatus m_status{
        PartyQuestRuntimeVerificationMonitorStatus::Inactive};
};
