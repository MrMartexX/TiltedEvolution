#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class PartyQuestNativeLoadRequestState : uint8_t
{
    Idle = 0,
    Reserved = 1,
    Claimed = 2,
    Completed = 3,
    Retired = 4,
    Poisoned = 5
};

enum class PartyQuestNativeLoadRequestStatus : uint8_t
{
    Begun = 1,
    Cancelled = 2,
    Claimed = 3,
    TargetEntered = 4,
    Completed = 5,
    CompletionAvailable = 6,
    Retired = 7,
    InvalidIdentity = 8,
    IdentityMismatch = 9,
    NonceMismatch = 10,
    StaleNonce = 11,
    InvalidState = 12,
    Duplicate = 13,
    CounterExhausted = 14,
    Poisoned = 15
};

struct PartyQuestNativeLoadIdentity final
{
    static constexpr uint16_t kCapacity = 260u;

    uint16_t Length{};
    char Bytes[kCapacity]{};
};

struct PartyQuestNativeLoadReservation final
{
    uint64_t AttemptNonce{};
    PartyQuestNativeLoadIdentity Identity;
    uint8_t Reserved[2]{};
};

struct PartyQuestNativeLoadCompletion final
{
    uint64_t AttemptNonce{};
    uint64_t EventSequence{};
    PartyQuestNativeLoadIdentity Identity;
    uint8_t Result{};
    uint8_t Reserved[9]{};

    [[nodiscard]] constexpr bool Succeeded() const noexcept
    {
        return Result != 0u;
    }
};

struct PartyQuestNativeLoadBeginResult final
{
    PartyQuestNativeLoadRequestStatus Status{
        PartyQuestNativeLoadRequestStatus::InvalidState};
    uint8_t HasReservation{};
    uint8_t Reserved[6]{};
    PartyQuestNativeLoadReservation Reservation;
};

struct PartyQuestNativeLoadPollResult final
{
    PartyQuestNativeLoadRequestStatus Status{
        PartyQuestNativeLoadRequestStatus::InvalidState};
    uint8_t HasCompletion{};
    uint8_t Reserved[6]{};
    PartyQuestNativeLoadCompletion Completion;
};

/**
 * Pure caller-serialized correlation contract for one native Skyrim LoadGame
 * request at a time.
 *
 * The caller supplies a bounded identity that is already normalized by its own
 * policy. This primitive deliberately invents no path/name rules: validity is
 * only nonzero bounded length, and correlation is strict Length plus byte-for-
 * byte equality over exactly that length. Bytes after Length are never part of
 * identity.
 *
 * Begin reserves a fresh nonzero monotonic attempt nonce. Claim correlates the
 * actual inner load name exactly once. MarkTargetEntered proves the claimed
 * target was entered. Complete is permitted only after that proof and records
 * one immutable completion with a fresh nonzero monotonic event sequence.
 * Poll is exact and idempotent: repeated exact polls return the same completion
 * without consuming or incrementing anything. Retire ends the completed
 * request. Cancel is valid only while Reserved and retires that reservation.
 *
 * Counter exhaustion poisons the contract before wraparound. All mismatch,
 * stale and duplicate operations are fail-closed and do not mutate state.
 * This primitive performs no allocation, locking, callbacks, I/O or exception
 * crossing. There is no recovery API for Poisoned state.
 */
class PartyQuestNativeLoadRequest final
{
public:
    PartyQuestNativeLoadRequest() noexcept = default;
    ~PartyQuestNativeLoadRequest() = default;

    PartyQuestNativeLoadRequest(const PartyQuestNativeLoadRequest&) = delete;
    PartyQuestNativeLoadRequest& operator=(
        const PartyQuestNativeLoadRequest&) = delete;
    PartyQuestNativeLoadRequest(PartyQuestNativeLoadRequest&&) = delete;
    PartyQuestNativeLoadRequest& operator=(
        PartyQuestNativeLoadRequest&&) = delete;

    [[nodiscard]] PartyQuestNativeLoadBeginResult Begin(
        const PartyQuestNativeLoadIdentity& acIdentity) noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus Cancel(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus Claim(
        uint64_t aAttemptNonce,
        const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus MarkTargetEntered(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus Complete(
        uint64_t aAttemptNonce,
        bool aResult) noexcept;

    [[nodiscard]] PartyQuestNativeLoadPollResult Poll(
        uint64_t aAttemptNonce) const noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus Retire(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] constexpr PartyQuestNativeLoadRequestState GetState()
        const noexcept
    {
        return m_state;
    }

    [[nodiscard]] constexpr uint64_t GetCurrentAttemptNonce() const noexcept
    {
        return m_currentAttemptNonce;
    }

private:
    friend class PartyQuestNativeLoadRequestTestAccess;

    [[nodiscard]] static bool IsValidIdentity(
        const PartyQuestNativeLoadIdentity& acIdentity) noexcept;
    [[nodiscard]] static bool IdentityEquals(
        const PartyQuestNativeLoadIdentity& acLeft,
        const PartyQuestNativeLoadIdentity& acRight) noexcept;

    [[nodiscard]] PartyQuestNativeLoadRequestStatus ClassifyNonce(
        uint64_t aAttemptNonce) const noexcept;

    void StoreIdentity(
        const PartyQuestNativeLoadIdentity& acIdentity) noexcept;
    void Poison() noexcept;

    PartyQuestNativeLoadRequestState m_state{
        PartyQuestNativeLoadRequestState::Idle};
    uint8_t m_targetEntered{};
    uint8_t m_reservedState[6]{};
    uint64_t m_lastAttemptNonce{};
    uint64_t m_lastEventSequence{};
    uint64_t m_currentAttemptNonce{};
    PartyQuestNativeLoadIdentity m_identity;
    PartyQuestNativeLoadCompletion m_completion;
};

static_assert(sizeof(PartyQuestNativeLoadRequestState) == 1u);
static_assert(sizeof(PartyQuestNativeLoadRequestStatus) == 1u);
static_assert(sizeof(PartyQuestNativeLoadIdentity) == 262u);
static_assert(alignof(PartyQuestNativeLoadIdentity) == 2u);
static_assert(offsetof(PartyQuestNativeLoadIdentity, Length) == 0u);
static_assert(offsetof(PartyQuestNativeLoadIdentity, Bytes) == 2u);
static_assert(sizeof(PartyQuestNativeLoadReservation) == 272u);
static_assert(alignof(PartyQuestNativeLoadReservation) == 8u);
static_assert(offsetof(PartyQuestNativeLoadReservation, AttemptNonce) == 0u);
static_assert(offsetof(PartyQuestNativeLoadReservation, Identity) == 8u);
static_assert(offsetof(PartyQuestNativeLoadReservation, Reserved) == 270u);
static_assert(sizeof(PartyQuestNativeLoadCompletion) == 288u);
static_assert(alignof(PartyQuestNativeLoadCompletion) == 8u);
static_assert(offsetof(PartyQuestNativeLoadCompletion, AttemptNonce) == 0u);
static_assert(offsetof(PartyQuestNativeLoadCompletion, EventSequence) == 8u);
static_assert(offsetof(PartyQuestNativeLoadCompletion, Identity) == 16u);
static_assert(offsetof(PartyQuestNativeLoadCompletion, Result) == 278u);
static_assert(offsetof(PartyQuestNativeLoadCompletion, Reserved) == 279u);
static_assert(sizeof(PartyQuestNativeLoadBeginResult) == 280u);
static_assert(sizeof(PartyQuestNativeLoadPollResult) == 296u);
static_assert(std::is_standard_layout_v<PartyQuestNativeLoadIdentity>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeLoadIdentity>);
static_assert(std::is_standard_layout_v<PartyQuestNativeLoadReservation>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeLoadReservation>);
static_assert(std::is_standard_layout_v<PartyQuestNativeLoadCompletion>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeLoadCompletion>);
static_assert(std::is_standard_layout_v<PartyQuestNativeLoadBeginResult>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeLoadBeginResult>);
static_assert(std::is_standard_layout_v<PartyQuestNativeLoadPollResult>);
static_assert(std::is_trivially_copyable_v<PartyQuestNativeLoadPollResult>);
static_assert(!std::is_copy_constructible_v<PartyQuestNativeLoadRequest>);
static_assert(!std::is_copy_assignable_v<PartyQuestNativeLoadRequest>);
static_assert(!std::is_move_constructible_v<PartyQuestNativeLoadRequest>);
static_assert(!std::is_move_assignable_v<PartyQuestNativeLoadRequest>);
