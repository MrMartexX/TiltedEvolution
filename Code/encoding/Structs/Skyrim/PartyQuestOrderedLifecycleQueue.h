#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

enum class PartyQuestOrderedLifecycleReason : uint8_t
{
    Connected = 1,
    PartyJoined = 2,
    PartyLeft = 3,
    CampaignSwitch = 4,
    Disconnect = 5,
    LoadGame = 6,
    Shutdown = 7
};

enum class PartyQuestOrderedLifecycleAction : uint8_t
{
    ApplyConnectedBoundary = 1,
    ApplyPartyJoinedBoundary = 2,
    ReleasePartyLeft = 3,
    ReleaseCampaignSwitch = 4,
    ReleaseDisconnect = 5,
    RetireBlockedLoadAttempt = 6,
    ApplyShutdown = 7
};

using PartyQuestOrderedLifecycleEvidenceMask = uint16_t;

[[nodiscard]] constexpr PartyQuestOrderedLifecycleEvidenceMask
PartyQuestOrderedLifecycleEvidenceFor(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    switch (aReason)
    {
    case PartyQuestOrderedLifecycleReason::Connected:
        return 1u << 0u;
    case PartyQuestOrderedLifecycleReason::PartyJoined:
        return 1u << 1u;
    case PartyQuestOrderedLifecycleReason::PartyLeft:
        return 1u << 2u;
    case PartyQuestOrderedLifecycleReason::CampaignSwitch:
        return 1u << 3u;
    case PartyQuestOrderedLifecycleReason::Disconnect:
        return 1u << 4u;
    case PartyQuestOrderedLifecycleReason::LoadGame:
        return 1u << 5u;
    case PartyQuestOrderedLifecycleReason::Shutdown:
        return 1u << 6u;
    }

    return 0u;
}

struct PartyQuestOrderedLifecycleEpoch final
{
    uint64_t Sequence{};
    uint64_t Revision{};
    PartyQuestOrderedLifecycleEvidenceMask Evidence{};
    PartyQuestOrderedLifecycleAction Action{
        PartyQuestOrderedLifecycleAction::ApplyConnectedBoundary};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Sequence != 0u && Revision != 0u && Evidence != 0u;
    }
};

struct PartyQuestOrderedLifecycleClaim final
{
    uint64_t Sequence{};
    uint64_t Revision{};
    PartyQuestOrderedLifecycleEvidenceMask Evidence{};
    PartyQuestOrderedLifecycleAction Action{
        PartyQuestOrderedLifecycleAction::ApplyConnectedBoundary};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Sequence != 0u && Revision != 0u && Evidence != 0u;
    }
};

enum class PartyQuestOrderedLifecycleEnqueueStatus : uint8_t
{
    Queued,
    Coalesced,
    Duplicate,
    TerminalQueued,
    TerminalClosed,
    CounterExhausted,
    QueueCapacityExceeded,
    AllocationFailed,
    InvalidReason
};

struct PartyQuestOrderedLifecycleEnqueueResult final
{
    PartyQuestOrderedLifecycleEnqueueStatus Status{
        PartyQuestOrderedLifecycleEnqueueStatus::InvalidReason};
    std::optional<PartyQuestOrderedLifecycleEpoch> Epoch;
};

/**
 * Pure deterministic FIFO for client lifecycle actions.
 *
 * Connected, PartyJoined and LoadGame always create independent epochs.
 * Only adjacent unclaimed release epochs may coalesce. Coalescing preserves
 * evidence and uses the explicit release precedence Disconnect >
 * CampaignSwitch > PartyLeft; enum ordering is never authority.
 *
 * TryClaimFront grants at most one exact claim. A claimed front is immutable
 * until exact Acknowledge or Retry. Enqueue while a claim is active therefore
 * appends/coalesces only after that claim and can never rewrite it.
 *
 * Shutdown is terminal. Without an active claim it atomically replaces queued
 * work with one ApplyShutdown epoch whose evidence is the union of all queued
 * evidence plus Shutdown. With an active claim it also invalidates that claim,
 * drops all later unclaimed epochs, and publishes Shutdown next. Any callback
 * that already entered under the invalidated claim remains an external
 * execution/generation-fence quiescence dependency; this pure queue performs
 * no waiting and no I/O. The caller owns serialization; this value type
 * intentionally contains no mutex or callback lifetime primitive.
 *
 * Capacity is a compile-time invariant (kCapacity) and cannot be reconfigured
 * at runtime. Capacity limits only creation of a new distinct epoch. Safe
 * adjacent release coalescing remains admissible when full because it does not
 * increase queue size. Shutdown may still replace a full queue atomically with
 * one terminal epoch. QueueCapacityExceeded is fail-closed lifecycle evidence:
 * callers must escalate/retain the lifecycle boundary and must never drop,
 * reorder or silently merge an event to make room.
 *
 * RetireBlockedLoadAttempt represents only retirement of an already blocked
 * LoadGame attempt. This primitive never stores or replays Load_Impl.
 */
class PartyQuestOrderedLifecycleQueue final
{
public:
    static constexpr size_t kCapacity = 32u;

    PartyQuestOrderedLifecycleQueue() noexcept = default;
    ~PartyQuestOrderedLifecycleQueue() = default;

    PartyQuestOrderedLifecycleQueue(
        const PartyQuestOrderedLifecycleQueue&) = delete;
    PartyQuestOrderedLifecycleQueue& operator=(
        const PartyQuestOrderedLifecycleQueue&) = delete;
    PartyQuestOrderedLifecycleQueue(
        PartyQuestOrderedLifecycleQueue&&) noexcept = default;
    PartyQuestOrderedLifecycleQueue& operator=(
        PartyQuestOrderedLifecycleQueue&&) noexcept = default;

    [[nodiscard]] PartyQuestOrderedLifecycleEnqueueResult Enqueue(
        PartyQuestOrderedLifecycleReason aReason) noexcept;

    [[nodiscard]] std::optional<PartyQuestOrderedLifecycleClaim>
    TryClaimFront() noexcept;

    [[nodiscard]] bool IsCurrent(
        const PartyQuestOrderedLifecycleClaim& acClaim) const noexcept;

    [[nodiscard]] bool Acknowledge(
        const PartyQuestOrderedLifecycleClaim& acClaim) noexcept;

    [[nodiscard]] bool Retry(
        const PartyQuestOrderedLifecycleClaim& acClaim) noexcept;

    [[nodiscard]] bool IsTerminal() const noexcept { return m_terminal; }
    [[nodiscard]] bool IsExhausted() const noexcept { return m_exhausted; }

private:
    friend class PartyQuestOrderedLifecycleQueueTestAccess;

    [[nodiscard]] static bool IsValidReason(
        PartyQuestOrderedLifecycleReason aReason) noexcept;
    [[nodiscard]] static bool IsReleaseReason(
        PartyQuestOrderedLifecycleReason aReason) noexcept;
    [[nodiscard]] static bool IsReleaseAction(
        PartyQuestOrderedLifecycleAction aAction) noexcept;
    [[nodiscard]] static PartyQuestOrderedLifecycleAction ActionForReason(
        PartyQuestOrderedLifecycleReason aReason) noexcept;
    [[nodiscard]] static PartyQuestOrderedLifecycleAction MergeReleaseAction(
        PartyQuestOrderedLifecycleAction aCurrent,
        PartyQuestOrderedLifecycleReason aIncoming) noexcept;
    [[nodiscard]] static bool SameClaim(
        const PartyQuestOrderedLifecycleClaim& acLeft,
        const PartyQuestOrderedLifecycleClaim& acRight) noexcept;
    [[nodiscard]] static bool ClaimMatchesEpoch(
        const PartyQuestOrderedLifecycleClaim& acClaim,
        const PartyQuestOrderedLifecycleEpoch& acEpoch) noexcept;

    void CommitNewEpochCounters() noexcept;
    void CommitRevisionCounter() noexcept;
    [[nodiscard]] PartyQuestOrderedLifecycleEvidenceMask
    AccumulatedEvidence() const noexcept;

    std::vector<PartyQuestOrderedLifecycleEpoch> m_epochs;
    std::optional<PartyQuestOrderedLifecycleClaim> m_activeClaim;
    uint64_t m_nextSequence{1u};
    uint64_t m_nextRevision{1u};
    bool m_terminal{};
    bool m_exhausted{};
};

static_assert(PartyQuestOrderedLifecycleQueue::kCapacity > 0u);
static_assert(sizeof(PartyQuestOrderedLifecycleReason) == 1u);
static_assert(sizeof(PartyQuestOrderedLifecycleAction) == 1u);
static_assert(sizeof(PartyQuestOrderedLifecycleEvidenceMask) == 2u);
static_assert(sizeof(uint64_t) == 8u);
static_assert(std::is_standard_layout_v<PartyQuestOrderedLifecycleEpoch>);
static_assert(std::is_trivially_copyable_v<PartyQuestOrderedLifecycleEpoch>);
static_assert(std::is_standard_layout_v<PartyQuestOrderedLifecycleClaim>);
static_assert(std::is_trivially_copyable_v<PartyQuestOrderedLifecycleClaim>);
static_assert(!std::is_copy_constructible_v<PartyQuestOrderedLifecycleQueue>);
static_assert(!std::is_copy_assignable_v<PartyQuestOrderedLifecycleQueue>);
static_assert(std::is_nothrow_move_constructible_v<
    PartyQuestOrderedLifecycleQueue>);
static_assert(std::is_nothrow_move_assignable_v<
    PartyQuestOrderedLifecycleQueue>);
