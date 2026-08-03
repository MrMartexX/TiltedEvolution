#pragma once

#include <cstdint>

enum class PartyQuestPapyrusQuiescenceStatus : uint8_t
{
    Waiting,
    Quiescent,
    InvalidTransaction
};

/**
 * Deterministic observation gate for the future Papyrus/event-queue integration.
 *
 * Runtime hooks provide queue depth plus a monotonically increasing quest-event
 * generation. Quiescence requires consecutive empty-queue samples with an
 * unchanged generation. Any queued work or new quest event resets stability.
 */
class PartyQuestPapyrusQuiescenceTracker final
{
public:
    static constexpr uint32_t kRequiredStableSamples = 2;

    bool Begin(uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestPapyrusQuiescenceStatus Observe(
        uint64_t aTransactionId,
        uint32_t aPendingEventCount,
        uint64_t aQuestEventGeneration) noexcept;

    bool Reset(uint64_t aTransactionId) noexcept;

    [[nodiscard]] uint64_t GetTransactionId() const noexcept { return m_transactionId; }
    [[nodiscard]] uint32_t GetStableSamples() const noexcept { return m_stableSamples; }
    [[nodiscard]] bool IsQuiescent() const noexcept { return m_quiescent; }

private:
    uint64_t m_transactionId{};
    uint64_t m_lastGeneration{};
    uint32_t m_stableSamples{};
    bool m_hasGeneration{};
    bool m_quiescent{};
};
