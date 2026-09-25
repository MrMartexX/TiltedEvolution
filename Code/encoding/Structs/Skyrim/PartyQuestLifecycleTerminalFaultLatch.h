#pragma once

#include <Structs/Skyrim/PartyQuestOrderedLifecycleQueue.h>

#include <cstdint>
#include <type_traits>

enum class PartyQuestLifecycleTerminalFaultLatchResult : uint8_t
{
    Latched,
    Observed,
    DuplicateEvidence,
    NotLatched,
    NonFatalStatus,
    InvalidStatus,
    InvalidReason
};

struct PartyQuestLifecycleTerminalFaultSnapshot final
{
    PartyQuestOrderedLifecycleEvidenceMask ObservedEvidence{};
    PartyQuestOrderedLifecycleReason FirstRejectedReason{
        PartyQuestOrderedLifecycleReason::Connected};
    PartyQuestOrderedLifecycleEnqueueStatus Cause{
        PartyQuestOrderedLifecycleEnqueueStatus::InvalidReason};
    uint8_t Latched{};
    uint8_t ShutdownObserved{};

    [[nodiscard]] constexpr bool HasFault() const noexcept
    {
        return Latched != 0u;
    }

    [[nodiscard]] constexpr bool HasShutdown() const noexcept
    {
        return ShutdownObserved != 0u;
    }
};

/**
 * One-way fail-closed record for a lifecycle boundary that could not be
 * represented by PartyQuestOrderedLifecycleQueue.
 *
 * Only QueueCapacityExceeded, CounterExhausted and AllocationFailed may create
 * the terminal fault. The exact first rejected reason and cause are immutable
 * once latched. Later valid reasons only extend ObservedEvidence and may mark
 * ShutdownObserved; they never become executable actions and never replace the
 * original fault identity.
 *
 * This primitive owns no heap storage, callbacks, locks or recovery path.
 * Callers own serialization. There is deliberately no reset/clear API: recovery
 * from a terminal queue fault requires an external process-lifetime policy.
 */
class PartyQuestLifecycleTerminalFaultLatch final
{
public:
    PartyQuestLifecycleTerminalFaultLatch() noexcept = default;
    ~PartyQuestLifecycleTerminalFaultLatch() = default;

    PartyQuestLifecycleTerminalFaultLatch(
        const PartyQuestLifecycleTerminalFaultLatch&) = delete;
    PartyQuestLifecycleTerminalFaultLatch& operator=(
        const PartyQuestLifecycleTerminalFaultLatch&) = delete;
    PartyQuestLifecycleTerminalFaultLatch(
        PartyQuestLifecycleTerminalFaultLatch&&) = delete;
    PartyQuestLifecycleTerminalFaultLatch& operator=(
        PartyQuestLifecycleTerminalFaultLatch&&) = delete;

    [[nodiscard]] PartyQuestLifecycleTerminalFaultLatchResult LatchFatal(
        PartyQuestOrderedLifecycleReason aRejectedReason,
        PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept;

    [[nodiscard]] PartyQuestLifecycleTerminalFaultLatchResult Observe(
        PartyQuestOrderedLifecycleReason aReason) noexcept;

    [[nodiscard]] PartyQuestLifecycleTerminalFaultSnapshot Snapshot()
        const noexcept;

    [[nodiscard]] bool IsLatched() const noexcept
    {
        return m_latched != 0u;
    }

    [[nodiscard]] bool IsShutdownObserved() const noexcept
    {
        return m_shutdownObserved != 0u;
    }

private:
    [[nodiscard]] static bool IsKnownStatus(
        PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept;
    [[nodiscard]] static bool IsFatalStatus(
        PartyQuestOrderedLifecycleEnqueueStatus aStatus) noexcept;
    [[nodiscard]] static bool IsValidReason(
        PartyQuestOrderedLifecycleReason aReason) noexcept;

    [[nodiscard]] PartyQuestLifecycleTerminalFaultLatchResult ObserveKnownReason(
        PartyQuestOrderedLifecycleReason aReason) noexcept;

    PartyQuestOrderedLifecycleEvidenceMask m_observedEvidence{};
    PartyQuestOrderedLifecycleReason m_firstRejectedReason{
        PartyQuestOrderedLifecycleReason::Connected};
    PartyQuestOrderedLifecycleEnqueueStatus m_cause{
        PartyQuestOrderedLifecycleEnqueueStatus::InvalidReason};
    uint8_t m_latched{};
    uint8_t m_shutdownObserved{};
};

static_assert(sizeof(PartyQuestLifecycleTerminalFaultLatchResult) == 1u);
static_assert(sizeof(PartyQuestLifecycleTerminalFaultSnapshot) == 6u);
static_assert(sizeof(PartyQuestLifecycleTerminalFaultLatch) == 6u);
static_assert(std::is_standard_layout_v<
    PartyQuestLifecycleTerminalFaultSnapshot>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestLifecycleTerminalFaultSnapshot>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestLifecycleTerminalFaultLatch>);
static_assert(!std::is_copy_assignable_v<
    PartyQuestLifecycleTerminalFaultLatch>);
static_assert(!std::is_move_constructible_v<
    PartyQuestLifecycleTerminalFaultLatch>);
static_assert(!std::is_move_assignable_v<
    PartyQuestLifecycleTerminalFaultLatch>);
