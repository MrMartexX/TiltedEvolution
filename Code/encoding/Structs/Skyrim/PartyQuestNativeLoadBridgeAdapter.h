#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadBridge.h>
#include <Structs/Skyrim/PartyQuestNativeLoadRequest.h>

#include <cstdint>

enum class PartyQuestNativeLoadBridgeAdapterState : uint8_t
{
    NotReady = 0,
    Ready = 1,
    Poisoned = 2
};

/**
 * Caller-serialized, allocation-free core for the native load bridge exports.
 *
 * Platform wrappers own pointer probing, SRWLOCK/SEH containment and hook
 * installation. This core accepts only copied POD values and never calls the
 * engine, callbacks or foreign code. Any impossible internal status poisons
 * the bridge permanently.
 */
class PartyQuestNativeLoadBridgeAdapter final
{
    friend class PartyQuestNativeLoadBridgeAdapterTestAccess;

public:
    PartyQuestNativeLoadBridgeAdapter() noexcept = default;

    PartyQuestNativeLoadBridgeAdapter(
        const PartyQuestNativeLoadBridgeAdapter&) = delete;
    PartyQuestNativeLoadBridgeAdapter& operator=(
        const PartyQuestNativeLoadBridgeAdapter&) = delete;
    PartyQuestNativeLoadBridgeAdapter(
        PartyQuestNativeLoadBridgeAdapter&&) = delete;
    PartyQuestNativeLoadBridgeAdapter& operator=(
        PartyQuestNativeLoadBridgeAdapter&&) = delete;

    [[nodiscard]] bool PublishReady(uint64_t aRuntimeFingerprint) noexcept;
    void Poison() noexcept;

    [[nodiscard]] PartyQuestNativeLoadBridgeDescriptorResult GetDescriptor(
        PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor) const noexcept;

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

    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Claim(
        uint64_t aAttemptNonce,
        const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus MarkTargetEntered(
        uint64_t aAttemptNonce) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus Complete(
        uint64_t aAttemptNonce,
        bool aResult) noexcept;

    [[nodiscard]] constexpr PartyQuestNativeLoadBridgeAdapterState GetState()
        const noexcept
    {
        return m_state;
    }

private:
    enum class Operation : uint8_t
    {
        Cancel,
        Poll,
        Retire,
        Claim,
        MarkTargetEntered,
        Complete
    };

    [[nodiscard]] PartyQuestNativeLoadBridgeStatus MapOperationStatus(
        Operation aOperation,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus AcceptReservation(
        const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
        const PartyQuestNativeLoadBeginResult& acResult,
        PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus AcceptCompletion(
        uint64_t aAttemptNonce,
        const PartyQuestNativeLoadPollResult& acResult,
        PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus FailInternal() noexcept;
    [[nodiscard]] static bool IdentityEquals(
        const PartyQuestNativeLoadIdentity& acLeft,
        const PartyQuestNativeLoadIdentity& acRight) noexcept;
    [[nodiscard]] static PartyQuestNativeLoadIdentity ToInternalIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept;
    [[nodiscard]] static PartyQuestNativeLoadBridgeIdentityV1 ToBridgeIdentity(
        const PartyQuestNativeLoadIdentity& acIdentity) noexcept;
    [[nodiscard]] PartyQuestNativeLoadBridgeStatus UnavailableStatus()
        const noexcept;

    PartyQuestNativeLoadBridgeAdapterState m_state{
        PartyQuestNativeLoadBridgeAdapterState::NotReady};
    uint8_t m_reserved[7]{};
    uint64_t m_runtimeFingerprint{};
    PartyQuestNativeLoadIdentity m_activeIdentity;
    PartyQuestNativeLoadRequest m_request;
};

static_assert(sizeof(PartyQuestNativeLoadBridgeAdapterState) == 1u);
