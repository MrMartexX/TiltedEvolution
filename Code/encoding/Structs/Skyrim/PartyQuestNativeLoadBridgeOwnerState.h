#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridge.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class PartyQuestNativeLoadBridgeOwnerPhase : uint8_t
{
    Unbound = 0u,
    Bound = 1u,
    DrainOnly = 2u,
    ShutdownDrain = 3u,
    ShutdownComplete = 4u,
    PoisonedUnsafeToUnload = 5u
};

enum class PartyQuestNativeLoadBridgeOwnerRequestPhase : uint8_t
{
    None = 0u,
    Active = 1u,
    CompletionCached = 2u
};

enum class PartyQuestNativeLoadBridgeOwnerCommandKind : uint8_t
{
    Invalid = 0u,
    Bind = 1u,
    Reserve = 2u,
    Cancel = 3u,
    Poll = 4u,
    Retire = 5u,
    GenerationTransition = 6u,
    Shutdown = 7u
};

enum class PartyQuestNativeLoadBridgeOwnerBindDisposition : uint8_t
{
    Rejected = 0u,
    Authenticated = 1u
};

enum class PartyQuestNativeLoadBridgeOwnerEffectKind : uint8_t
{
    None = 0u,
    Reserve = 1u,
    Cancel = 2u,
    Poll = 3u,
    Retire = 4u
};

enum class PartyQuestNativeLoadBridgeOwnerForeignDisposition : uint8_t
{
    Returned = 1u,
    Unknown = 2u
};

enum class PartyQuestNativeLoadBridgeOwnerNonceClass : uint8_t
{
    ExactActive = 1u,
    ExactRetired = 2u,
    Stale = 3u,
    Mismatch = 4u
};

enum class PartyQuestNativeLoadBridgeOwnerResultCode : uint8_t
{
    EffectRequired = 1u,
    Bound = 2u,
    AlreadyBound = 3u,
    BindRejected = 4u,
    Unbound = 5u,
    Reserved = 6u,
    Cancelled = 7u,
    Pending = 8u,
    CompletionAvailable = 9u,
    Retired = 10u,
    Duplicate = 11u,
    StaleNonce = 12u,
    NonceMismatch = 13u,
    InvalidIdentity = 14u,
    InvalidState = 15u,
    NotBound = 16u,
    AdmissionClosed = 17u,
    StaleGeneration = 18u,
    OperationInFlight = 19u,
    DrainPending = 20u,
    ShutdownComplete = 21u,
    InvalidCommand = 22u,
    PoisonedUnsafeToUnload = 23u
};

struct PartyQuestNativeLoadBridgeOwnerCommand final
{
    PartyQuestNativeLoadBridgeOwnerCommandKind Kind{
        PartyQuestNativeLoadBridgeOwnerCommandKind::Invalid};
    PartyQuestNativeLoadBridgeOwnerBindDisposition BindDisposition{
        PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected};
    uint8_t Reserved[6]{};

    uint64_t RuntimeGeneration{};
    uint64_t RuntimeFingerprint{};
    uint64_t AttemptNonce{};

    PartyQuestNativeLoadBridgeIdentityV1 Identity;
};

struct PartyQuestNativeLoadBridgeOwnerEffect final
{
    PartyQuestNativeLoadBridgeOwnerEffectKind Kind{
        PartyQuestNativeLoadBridgeOwnerEffectKind::None};
    uint8_t Reserved[7]{};

    uint64_t AttemptNonce{};
    PartyQuestNativeLoadBridgeReserveRequestV1 ReserveRequest;
};

struct PartyQuestNativeLoadBridgeOwnerForeignOutcome final
{
    PartyQuestNativeLoadBridgeOwnerEffectKind EffectKind{
        PartyQuestNativeLoadBridgeOwnerEffectKind::None};
    PartyQuestNativeLoadBridgeOwnerForeignDisposition Disposition{
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Returned};
    uint8_t HasReservation{};
    uint8_t HasCompletion{};
    uint8_t Reserved0[4]{};

    uint32_t RawStatus{};
    uint32_t Reserved1{};

    PartyQuestNativeLoadBridgeReservationV1 Reservation;
    PartyQuestNativeLoadBridgeCompletionV1 Completion;
};

struct PartyQuestNativeLoadBridgeOwnerResult final
{
    PartyQuestNativeLoadBridgeOwnerResultCode Code{
        PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState};
    uint8_t ReleaseCapability{};
    uint8_t HasEffect{};
    uint8_t HasCompletion{};
    uint8_t Reserved[4]{};

    PartyQuestNativeLoadBridgeOwnerEffect Effect;
    PartyQuestNativeLoadBridgeCompletionV1 Completion;
};

struct PartyQuestNativeLoadBridgeOwnerSnapshot final
{
    PartyQuestNativeLoadBridgeOwnerPhase Phase{
        PartyQuestNativeLoadBridgeOwnerPhase::Unbound};
    PartyQuestNativeLoadBridgeOwnerRequestPhase RequestPhase{
        PartyQuestNativeLoadBridgeOwnerRequestPhase::None};
    PartyQuestNativeLoadBridgeOwnerEffectKind PendingEffect{
        PartyQuestNativeLoadBridgeOwnerEffectKind::None};
    uint8_t CapabilityRetained{};
    uint8_t HasCachedCompletion{};
    uint8_t Reserved[3]{};

    uint64_t CurrentGeneration{};
    uint64_t BoundGeneration{};
    uint64_t RuntimeFingerprint{};

    uint64_t ActiveAttemptNonce{};
    uint64_t LastAttemptNonce{};
    uint64_t LastCompletionSequence{};

    PartyQuestNativeLoadBridgeIdentityV1 ActiveIdentity;
    PartyQuestNativeLoadBridgeCompletionV1 CachedCompletion;
};

/**
 * Pure, caller-serialized owner-state reducer for the future native LoadGame
 * bridge. It performs no foreign calls and contains no function pointers,
 * Windows types, locks, callbacks, resolver state, SEH or runtime objects.
 *
 * Foreign work is always two-phase. Plan() may publish exactly one fixed Effect
 * and records it as pending. The caller executes that effect elsewhere and
 * injects one POD ForeignOutcome through ApplyForeignOutcome(). No second
 * effect can be planned while one is pending.
 *
 * An unknown foreign-call outcome, unknown/impossible status, malformed
 * successful payload, correlation mismatch or non-monotonic native counter
 * permanently transitions to PoisonedUnsafeToUnload. That terminal state never
 * releases capability ownership because the native request state is no longer
 * provable.
 */
class PartyQuestNativeLoadBridgeOwnerState final
{
public:
    PartyQuestNativeLoadBridgeOwnerState() noexcept = default;
    ~PartyQuestNativeLoadBridgeOwnerState() = default;

    PartyQuestNativeLoadBridgeOwnerState(
        const PartyQuestNativeLoadBridgeOwnerState&) = delete;
    PartyQuestNativeLoadBridgeOwnerState& operator=(
        const PartyQuestNativeLoadBridgeOwnerState&) = delete;
    PartyQuestNativeLoadBridgeOwnerState(
        PartyQuestNativeLoadBridgeOwnerState&&) = delete;
    PartyQuestNativeLoadBridgeOwnerState& operator=(
        PartyQuestNativeLoadBridgeOwnerState&&) = delete;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult Plan(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult ApplyForeignOutcome(
        const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerSnapshot Snapshot()
        const noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerNonceClass ClassifyNonce(
        uint64_t aAttemptNonce) const noexcept;

private:
    [[nodiscard]] static bool IdentityEquals(
        const PartyQuestNativeLoadBridgeIdentityV1& acLeft,
        const PartyQuestNativeLoadBridgeIdentityV1& acRight) noexcept;
    [[nodiscard]] static PartyQuestNativeLoadBridgeIdentityV1
    CanonicalizeIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept;

    [[nodiscard]] static bool IsZeroIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept;
    [[nodiscard]] static bool IsZeroReserveRequest(
        const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest) noexcept;
    [[nodiscard]] static bool IsZeroReservation(
        const PartyQuestNativeLoadBridgeReservationV1& acReservation) noexcept;
    [[nodiscard]] static bool IsZeroCompletion(
        const PartyQuestNativeLoadBridgeCompletionV1& acCompletion) noexcept;

    [[nodiscard]] static bool AreZero(
        const uint8_t* apBytes,
        size_t aSize) noexcept;

    [[nodiscard]] bool IsCommandShapeValid(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand,
        PartyQuestNativeLoadBridgeOwnerResultCode& aFailure) const noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PlanBind(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PlanReserve(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PlanRequestCommand(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PlanGenerationTransition(
        const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PlanShutdown() noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult ApplyReserve(
        const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult ApplyCancel(
        const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult ApplyPoll(
        const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult ApplyRetire(
        const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode aCode) const noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult PublishEffect(
        PartyQuestNativeLoadBridgeOwnerEffect aEffect) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult Poison() noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeOwnerResult
    FinishExactRequest(
        PartyQuestNativeLoadBridgeOwnerResultCode aCode) noexcept;

    void ClearPendingEffect() noexcept;
    void ClearRequest() noexcept;
    void ClearBinding() noexcept;

    PartyQuestNativeLoadBridgeOwnerPhase m_phase{
        PartyQuestNativeLoadBridgeOwnerPhase::Unbound};
    PartyQuestNativeLoadBridgeOwnerRequestPhase m_requestPhase{
        PartyQuestNativeLoadBridgeOwnerRequestPhase::None};
    uint8_t m_capabilityRetained{};

    uint64_t m_currentGeneration{};
    uint64_t m_boundGeneration{};
    uint64_t m_runtimeFingerprint{};

    uint64_t m_activeAttemptNonce{};
    uint64_t m_lastAttemptNonce{};
    uint64_t m_lastCompletionSequence{};

    PartyQuestNativeLoadBridgeIdentityV1 m_activeIdentity;
    PartyQuestNativeLoadBridgeCompletionV1 m_cachedCompletion;
    PartyQuestNativeLoadBridgeOwnerEffect m_pendingEffect;
};

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerPhase) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerRequestPhase) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerCommandKind) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerBindDisposition) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerEffectKind) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerForeignDisposition) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerNonceClass) == 1u);
static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerResultCode) == 1u);

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerCommand) == 296u);
static_assert(alignof(PartyQuestNativeLoadBridgeOwnerCommand) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, Kind) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, BindDisposition) == 1u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, RuntimeGeneration) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, RuntimeFingerprint) == 16u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, AttemptNonce) == 24u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerCommand, Identity) == 32u);

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerEffect) == 288u);
static_assert(alignof(PartyQuestNativeLoadBridgeOwnerEffect) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerEffect, Kind) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerEffect, AttemptNonce) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerEffect, ReserveRequest) == 16u);

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerForeignOutcome) == 592u);
static_assert(alignof(PartyQuestNativeLoadBridgeOwnerForeignOutcome) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerForeignOutcome, EffectKind) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerForeignOutcome, Disposition) == 1u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerForeignOutcome, RawStatus) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerForeignOutcome, Reservation) == 16u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerForeignOutcome, Completion) == 296u);

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerResult) == 592u);
static_assert(alignof(PartyQuestNativeLoadBridgeOwnerResult) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerResult, Code) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerResult, Effect) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerResult, Completion) == 296u);

static_assert(sizeof(PartyQuestNativeLoadBridgeOwnerSnapshot) == 616u);
static_assert(alignof(PartyQuestNativeLoadBridgeOwnerSnapshot) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerSnapshot, Phase) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerSnapshot, CurrentGeneration) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerSnapshot, ActiveIdentity) == 56u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeOwnerSnapshot, CachedCompletion) == 320u);

static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeOwnerCommand>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeOwnerCommand>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeOwnerEffect>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeOwnerEffect>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeOwnerForeignOutcome>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeOwnerForeignOutcome>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeOwnerResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeOwnerResult>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeOwnerSnapshot>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeOwnerSnapshot>);

static_assert(!std::is_copy_constructible_v<
    PartyQuestNativeLoadBridgeOwnerState>);
static_assert(!std::is_copy_assignable_v<
    PartyQuestNativeLoadBridgeOwnerState>);
static_assert(!std::is_move_constructible_v<
    PartyQuestNativeLoadBridgeOwnerState>);
static_assert(!std::is_move_assignable_v<
    PartyQuestNativeLoadBridgeOwnerState>);
