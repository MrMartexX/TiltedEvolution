#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridgeAdapter.h>

#include <Windows.h>

#include <cstdint>
#include <type_traits>

struct PartyQuestSkyrimNativeLoadBridgeHookClaimResult final
{
    PartyQuestNativeLoadBridgeStatus Status{
        PartyQuestNativeLoadBridgeStatus::BridgeUnavailable};
    uint32_t Reserved{};
    uint64_t AttemptNonce{};

    [[nodiscard]] bool IsClaimed() const noexcept
    {
        return Status == PartyQuestNativeLoadBridgeStatus::Pending &&
            AttemptNonce != 0u;
    }
};

/**
 * Windows process-image wrapper around PartyQuestNativeLoadBridgeAdapter.
 *
 * The portable adapter owns only the request state machine. This wrapper owns
 * the physical concurrency/ABI boundary:
 * - one process-local SRWLOCK serializes exports and Load_Impl hook operations;
 * - ABI pointers are copied/probed under Windows SEH before reaching the core;
 * - no engine pointer or borrowed string is retained;
 * - hook-side Claim/TargetEntered/Complete accepts copied POD identity only;
 * - an output fault after a state-changing foreign operation poisons the
 *   adapter instead of pretending the caller observed a deterministic result.
 *
 * PublishReady() is deliberately separate from construction. Production must
 * publish only after the exact runtime/provider evidence has been accepted.
 * This slice does not call PublishReady() from startup and therefore enables no
 * production LoadGame provider behavior by itself.
 */
class PartyQuestSkyrimNativeLoadBridgeProvider final
{
public:
    PartyQuestSkyrimNativeLoadBridgeProvider() noexcept = default;

    PartyQuestSkyrimNativeLoadBridgeProvider(
        const PartyQuestSkyrimNativeLoadBridgeProvider&) = delete;
    PartyQuestSkyrimNativeLoadBridgeProvider& operator=(
        const PartyQuestSkyrimNativeLoadBridgeProvider&) = delete;
    PartyQuestSkyrimNativeLoadBridgeProvider(
        PartyQuestSkyrimNativeLoadBridgeProvider&&) = delete;
    PartyQuestSkyrimNativeLoadBridgeProvider& operator=(
        PartyQuestSkyrimNativeLoadBridgeProvider&&) = delete;

    [[nodiscard]] static PartyQuestSkyrimNativeLoadBridgeProvider&
    GetProcessProvider() noexcept;

    [[nodiscard]] bool PublishReady(uint64_t aRuntimeFingerprint) noexcept;
    void Poison() noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeDescriptorResult GetDescriptor(
        PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Reserve(
        const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
        PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Cancel(
        uint64_t aAttemptNonce) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Poll(
        uint64_t aAttemptNonce,
        PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Retire(
        uint64_t aAttemptNonce) noexcept;

    /**
     * Load_Impl side. Uses the exact currently reserved nonce; identity
     * mismatch never substitutes or advances to another request.
     */
    [[nodiscard]] PartyQuestSkyrimNativeLoadBridgeHookClaimResult
    ClaimReserved(
        const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeStatus MarkTargetEntered(
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeStatus CompleteTarget(
        uint64_t aAttemptNonce,
        bool aResult) noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeAdapterState GetState() noexcept;
    [[nodiscard]] uint64_t GetActiveAttemptNonce() noexcept;

private:
    friend class PartyQuestSkyrimNativeLoadBridgeProviderAbi;

    void PoisonLocked() noexcept;
    void ClearActiveNonceIfTerminalLocked(
        PartyQuestNativeLoadBridgeStatus aStatus) noexcept;

    SRWLOCK m_lock = SRWLOCK_INIT;
    PartyQuestNativeLoadBridgeAdapter m_adapter;
    uint64_t m_activeAttemptNonce{};
};

/**
 * Pointer-safe ABI façade. Tests use these methods with local provider
 * instances; process-image thunks below use the process singleton.
 */
class PartyQuestSkyrimNativeLoadBridgeProviderAbi final
{
public:
    [[nodiscard]] static uint32_t GetDescriptor(
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
        PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
        uint32_t aDescriptorSize) noexcept;

    [[nodiscard]] static uint32_t Reserve(
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
        const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
        uint32_t aRequestSize,
        PartyQuestNativeLoadBridgeReservationV1* apReservation,
        uint32_t aReservationSize) noexcept;

    [[nodiscard]] static uint32_t Cancel(
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
        uint64_t aAttemptNonce) noexcept;

    [[nodiscard]] static uint32_t Poll(
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
        uint64_t aAttemptNonce,
        PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
        uint32_t aCompletionSize) noexcept;

    [[nodiscard]] static uint32_t Retire(
        PartyQuestSkyrimNativeLoadBridgeProvider& aProvider,
        uint64_t aAttemptNonce) noexcept;
};

/**
 * Direct process-image call targets for
 * PartyQuestSkyrimNativeLoadBridgeSourceAuthorization::ProcessImage.
 *
 * They intentionally are not DLL exports. SkyrimTogetherClient is linked into
 * the launcher executable and the launcher later manual-maps Skyrim over the
 * main PE headers. The trusted process-image resolver authenticates these exact
 * addresses with the preserved pre-remap STR image predicate instead.
 */
extern "C" uint32_t PartyQuestProcessNativeLoadBridge_GetDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1* apDescriptor,
    uint32_t aDescriptorSize);

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Reserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1* apRequest,
    uint32_t aRequestSize,
    PartyQuestNativeLoadBridgeReservationV1* apReservation,
    uint32_t aReservationSize);

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Cancel(
    uint64_t aAttemptNonce);

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Poll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1* apCompletion,
    uint32_t aCompletionSize);

extern "C" uint32_t PartyQuestProcessNativeLoadBridge_Retire(
    uint64_t aAttemptNonce);

static_assert(std::is_standard_layout_v<
    PartyQuestSkyrimNativeLoadBridgeHookClaimResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestSkyrimNativeLoadBridgeHookClaimResult>);
