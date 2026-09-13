#pragma once

#include <cstdint>
#include <optional>
#include <utility>

class PartyQuestPapyrusQuiescenceAuthorization final
{
public:
    PartyQuestPapyrusQuiescenceAuthorization() noexcept = default;
    PartyQuestPapyrusQuiescenceAuthorization(
        const PartyQuestPapyrusQuiescenceAuthorization&) = delete;
    PartyQuestPapyrusQuiescenceAuthorization& operator=(
        const PartyQuestPapyrusQuiescenceAuthorization&) = delete;

    PartyQuestPapyrusQuiescenceAuthorization(
        PartyQuestPapyrusQuiescenceAuthorization&& aOther) noexcept
    {
        *this = std::move(aOther);
    }

    PartyQuestPapyrusQuiescenceAuthorization& operator=(
        PartyQuestPapyrusQuiescenceAuthorization&& aOther) noexcept
    {
        if (this == &aOther)
            return *this;

        m_transactionId = aOther.m_transactionId;
        m_sessionNonce = aOther.m_sessionNonce;
        m_eventGeneration = aOther.m_eventGeneration;
        m_observationRevision = aOther.m_observationRevision;
        aOther.Invalidate();
        return *this;
    }

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_transactionId != 0 &&
            m_sessionNonce != 0 &&
            m_observationRevision != 0;
    }

    [[nodiscard]] uint64_t GetTransactionId() const noexcept
    {
        return m_transactionId;
    }

private:
    friend class PartyQuestPapyrusQuiescenceTracker;

    PartyQuestPapyrusQuiescenceAuthorization(
        uint64_t aTransactionId,
        uint64_t aSessionNonce,
        uint64_t aEventGeneration,
        uint64_t aObservationRevision) noexcept
        : m_transactionId(aTransactionId)
        , m_sessionNonce(aSessionNonce)
        , m_eventGeneration(aEventGeneration)
        , m_observationRevision(aObservationRevision)
    {
    }

    void Invalidate() noexcept
    {
        m_transactionId = 0;
        m_sessionNonce = 0;
        m_eventGeneration = 0;
        m_observationRevision = 0;
    }

    uint64_t m_transactionId{};
    uint64_t m_sessionNonce{};
    uint64_t m_eventGeneration{};
    uint64_t m_observationRevision{};
};

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
 *
 * Authorize() deliberately produces only a process-local capability over the
 * tracker's current observations. It is not evidence that those observations
 * came from Skyrim; the trusted runtime observer is a separate integration
 * boundary. Any later valid observation changes the revision and makes a prior
 * authorization stale. Consume() is one-shot and resets the tracker session.
 *
 * The tracker itself is also one-shot process state. Copying or moving it would
 * duplicate an already observed session and could preserve replayable evidence
 * after another copy was consumed, so all copy/move operations are forbidden.
 */
class PartyQuestPapyrusQuiescenceTracker final
{
public:
    static constexpr uint32_t kRequiredStableSamples = 2;

    PartyQuestPapyrusQuiescenceTracker() noexcept = default;
    PartyQuestPapyrusQuiescenceTracker(
        const PartyQuestPapyrusQuiescenceTracker&) = delete;
    PartyQuestPapyrusQuiescenceTracker& operator=(
        const PartyQuestPapyrusQuiescenceTracker&) = delete;
    PartyQuestPapyrusQuiescenceTracker(
        PartyQuestPapyrusQuiescenceTracker&&) = delete;
    PartyQuestPapyrusQuiescenceTracker& operator=(
        PartyQuestPapyrusQuiescenceTracker&&) = delete;

    bool Begin(uint64_t aTransactionId) noexcept;

    [[nodiscard]] PartyQuestPapyrusQuiescenceStatus Observe(
        uint64_t aTransactionId,
        uint32_t aPendingEventCount,
        uint64_t aQuestEventGeneration) noexcept;

    [[nodiscard]] std::optional<PartyQuestPapyrusQuiescenceAuthorization>
    Authorize() const noexcept;

    [[nodiscard]] bool Consume(
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization) noexcept;

    bool Reset(uint64_t aTransactionId) noexcept;

    [[nodiscard]] uint64_t GetTransactionId() const noexcept { return m_transactionId; }
    [[nodiscard]] uint32_t GetStableSamples() const noexcept { return m_stableSamples; }
    [[nodiscard]] bool IsQuiescent() const noexcept { return m_quiescent; }

private:
    void Clear() noexcept;

    uint64_t m_transactionId{};
    uint64_t m_sessionNonce{};
    uint64_t m_lastGeneration{};
    uint64_t m_observationRevision{};
    uint32_t m_stableSamples{};
    bool m_hasGeneration{};
    bool m_quiescent{};
};
