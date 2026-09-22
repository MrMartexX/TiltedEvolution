#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadRequest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class PartyQuestNativeLoadBridgeCapabilityBit : uint64_t
{
    ExclusiveRequest = 1ull << 0u,
    ExactNormalizedIdentity = 1ull << 1u,
    ClaimBeforeTargetEntry = 1ull << 2u,
    CompletionBeforeLegacyPostLoad = 1ull << 3u,
    FalseResultCompletion = 1ull << 4u,
    IdempotentPoll = 1ull << 5u,
    ExplicitRetirement = 1ull << 6u,
    MonotonicAttemptNonce = 1ull << 7u,
    MonotonicEventSequence = 1ull << 8u,
    PullOnlyNoCallbacks = 1ull << 9u,
    TargetReturnCompletion = 1ull << 10u
};

inline constexpr uint32_t kPartyQuestNativeLoadBridgeDescriptorAbi = 1u;
inline constexpr uint32_t kPartyQuestNativeLoadBridgePayloadAbi = 1u;
inline constexpr uint32_t kPartyQuestNativeLoadBridgeImplementationVersion = 1u;

inline constexpr uint64_t kPartyQuestNativeLoadBridgeFingerprint =
    0x315644414F4C5150ull; // "PQLOADV1", deterministic ABI identity.

inline constexpr uint64_t kPartyQuestRequiredNativeLoadBridgeCapabilities =
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::ExclusiveRequest) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::ExactNormalizedIdentity) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::ClaimBeforeTargetEntry) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::CompletionBeforeLegacyPostLoad) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::FalseResultCompletion) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::IdempotentPoll) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::ExplicitRetirement) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::MonotonicAttemptNonce) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::MonotonicEventSequence) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::PullOnlyNoCallbacks) |
    static_cast<uint64_t>(
        PartyQuestNativeLoadBridgeCapabilityBit::TargetReturnCompletion);

enum class PartyQuestNativeLoadBridgeDescriptorResult : uint32_t
{
    Unavailable = 0u,
    Available = 1u
};

enum class PartyQuestNativeLoadBridgeStatus : uint32_t
{
    Reserved = 1u,
    Cancelled = 2u,
    Pending = 3u,
    CompletionAvailable = 4u,
    Retired = 5u,
    Duplicate = 6u,

    InvalidArgument = 16u,
    UnsupportedAbi = 17u,
    InvalidStructSize = 18u,
    InvalidIdentity = 19u,
    IdentityMismatch = 20u,
    NonceMismatch = 21u,
    StaleNonce = 22u,
    InvalidState = 23u,

    CounterExhausted = 32u,
    Poisoned = 33u,
    BridgeUnavailable = 34u,
    InternalFailure = 35u
};

struct PartyQuestNativeLoadBridgeDescriptorV1 final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint32_t PayloadAbiVersion{};
    uint32_t ImplementationVersion{};
    uint64_t Capabilities{};
    uint32_t RuntimeMajor{};
    uint32_t RuntimeMinor{};
    uint32_t RuntimePatch{};
    uint32_t RuntimeBuild{};
    uint64_t RuntimeFingerprint{};
    uint64_t BridgeFingerprint{};
    uint64_t Reserved0{};
    uint64_t Reserved1{};
};

struct PartyQuestNativeLoadBridgeIdentityV1 final
{
    // Only Bytes[0..Length) participate in identity, comparison, hashing and
    // correlation. Tail bytes are outside the identity and must be ignored.
    uint16_t Length{};
    uint16_t Reserved0{};
    uint8_t Bytes[PartyQuestNativeLoadIdentity::kCapacity]{};
};

struct PartyQuestNativeLoadBridgeReserveRequestV1 final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    PartyQuestNativeLoadBridgeIdentityV1 Identity;
};

struct PartyQuestNativeLoadBridgeReservationV1 final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint64_t AttemptNonce{};
    PartyQuestNativeLoadBridgeIdentityV1 Identity;
};

struct PartyQuestNativeLoadBridgeCompletionV1 final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint64_t AttemptNonce{};
    uint64_t EventSequence{};
    PartyQuestNativeLoadBridgeIdentityV1 Identity;
    uint8_t Result{};
    uint8_t Reserved[7]{};
};

inline constexpr char kPartyQuestNativeLoadBridgeGetDescriptorExport[] =
    "PartyQuestSKSE_GetLoadBridgeDescriptor";
inline constexpr char kPartyQuestNativeLoadBridgeReserveExport[] =
    "PartyQuestSKSE_ReserveLoad";
inline constexpr char kPartyQuestNativeLoadBridgeCancelExport[] =
    "PartyQuestSKSE_CancelLoad";
inline constexpr char kPartyQuestNativeLoadBridgePollExport[] =
    "PartyQuestSKSE_PollLoad";
inline constexpr char kPartyQuestNativeLoadBridgeRetireExport[] =
    "PartyQuestSKSE_RetireLoad";

/**
 * Exact cross-DLL callable signatures for the five v1 exports.
 *
 * Deliberately do not put noexcept into these function types. Since C++17,
 * noexcept participates in the function type, and the portable contract must
 * not require identical compiler type-system treatment merely to call a C ABI
 * export. Native implementations still have the separate hard requirement to
 * contain C++ exceptions and platform structured exceptions before crossing
 * the DLL boundary.
 *
 * These function pointers are resolver-side callable types only. They are not
 * fields of any ABI payload and establish no callback ownership.
 */
using PartyQuestNativeLoadBridgeGetDescriptorExport =
    uint32_t (*)(
        PartyQuestNativeLoadBridgeDescriptorV1*,
        uint32_t);

using PartyQuestNativeLoadBridgeReserveExport =
    uint32_t (*)(
        const PartyQuestNativeLoadBridgeReserveRequestV1*,
        uint32_t,
        PartyQuestNativeLoadBridgeReservationV1*,
        uint32_t);

using PartyQuestNativeLoadBridgeCancelExport =
    uint32_t (*)(uint64_t);

using PartyQuestNativeLoadBridgePollExport =
    uint32_t (*)(
        uint64_t,
        PartyQuestNativeLoadBridgeCompletionV1*,
        uint32_t);

using PartyQuestNativeLoadBridgeRetireExport =
    uint32_t (*)(uint64_t);

/**
 * Pure compatibility policy for the future native LoadGame completion bridge.
 *
 * Descriptor compatibility is not module/source authentication. A future
 * trusted resolver must supply the exact expected runtime fingerprint proven
 * for its accepted native research artifact. This header intentionally defines
 * no runtime fingerprint constant because none is established by the current
 * portable repository evidence.
 *
 * ABI payloads contain only fixed-width integers and inline byte arrays. They
 * carry no pointers, bool, STL objects, callbacks, ownership handles or
 * variable-size strings across the DLL boundary.
 */
struct PartyQuestNativeLoadBridgePolicy final
{
    [[nodiscard]] static constexpr bool IsKnownDescriptorResult(
        uint32_t aResult) noexcept
    {
        switch (
            static_cast<PartyQuestNativeLoadBridgeDescriptorResult>(aResult))
        {
        case PartyQuestNativeLoadBridgeDescriptorResult::Unavailable:
        case PartyQuestNativeLoadBridgeDescriptorResult::Available:
            return true;
        }

        return false;
    }

    [[nodiscard]] static constexpr bool IsApprovedDescriptor(
        const PartyQuestNativeLoadBridgeDescriptorV1& acDescriptor,
        uint64_t aExpectedRuntimeFingerprint) noexcept
    {
        return aExpectedRuntimeFingerprint != 0u &&
            acDescriptor.AbiVersion ==
                kPartyQuestNativeLoadBridgeDescriptorAbi &&
            acDescriptor.StructSize ==
                sizeof(PartyQuestNativeLoadBridgeDescriptorV1) &&
            acDescriptor.PayloadAbiVersion ==
                kPartyQuestNativeLoadBridgePayloadAbi &&
            acDescriptor.ImplementationVersion ==
                kPartyQuestNativeLoadBridgeImplementationVersion &&
            acDescriptor.Capabilities ==
                kPartyQuestRequiredNativeLoadBridgeCapabilities &&
            acDescriptor.RuntimeMajor == 1u &&
            acDescriptor.RuntimeMinor == 6u &&
            acDescriptor.RuntimePatch == 1170u &&
            acDescriptor.RuntimeBuild == 0u &&
            acDescriptor.RuntimeFingerprint == aExpectedRuntimeFingerprint &&
            acDescriptor.BridgeFingerprint ==
                kPartyQuestNativeLoadBridgeFingerprint &&
            acDescriptor.Reserved0 == 0u &&
            acDescriptor.Reserved1 == 0u;
    }

    [[nodiscard]] static constexpr bool IsKnownStatus(
        uint32_t aStatus) noexcept
    {
        switch (static_cast<PartyQuestNativeLoadBridgeStatus>(aStatus))
        {
        case PartyQuestNativeLoadBridgeStatus::Reserved:
        case PartyQuestNativeLoadBridgeStatus::Cancelled:
        case PartyQuestNativeLoadBridgeStatus::Pending:
        case PartyQuestNativeLoadBridgeStatus::CompletionAvailable:
        case PartyQuestNativeLoadBridgeStatus::Retired:
        case PartyQuestNativeLoadBridgeStatus::Duplicate:
        case PartyQuestNativeLoadBridgeStatus::InvalidArgument:
        case PartyQuestNativeLoadBridgeStatus::UnsupportedAbi:
        case PartyQuestNativeLoadBridgeStatus::InvalidStructSize:
        case PartyQuestNativeLoadBridgeStatus::InvalidIdentity:
        case PartyQuestNativeLoadBridgeStatus::IdentityMismatch:
        case PartyQuestNativeLoadBridgeStatus::NonceMismatch:
        case PartyQuestNativeLoadBridgeStatus::StaleNonce:
        case PartyQuestNativeLoadBridgeStatus::InvalidState:
        case PartyQuestNativeLoadBridgeStatus::CounterExhausted:
        case PartyQuestNativeLoadBridgeStatus::Poisoned:
        case PartyQuestNativeLoadBridgeStatus::BridgeUnavailable:
        case PartyQuestNativeLoadBridgeStatus::InternalFailure:
            return true;
        }

        return false;
    }

    [[nodiscard]] static constexpr bool IsValidIdentity(
        const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
    {
        return acIdentity.Length != 0u &&
            acIdentity.Length <= PartyQuestNativeLoadIdentity::kCapacity &&
            acIdentity.Reserved0 == 0u;
    }

    [[nodiscard]] static constexpr bool IsValidReserveRequest(
        const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest) noexcept
    {
        return acRequest.AbiVersion == kPartyQuestNativeLoadBridgePayloadAbi &&
            acRequest.StructSize ==
                sizeof(PartyQuestNativeLoadBridgeReserveRequestV1) &&
            IsValidIdentity(acRequest.Identity);
    }

    [[nodiscard]] static constexpr bool IsValidReservation(
        const PartyQuestNativeLoadBridgeReservationV1& acReservation) noexcept
    {
        return acReservation.AbiVersion ==
                kPartyQuestNativeLoadBridgePayloadAbi &&
            acReservation.StructSize ==
                sizeof(PartyQuestNativeLoadBridgeReservationV1) &&
            acReservation.AttemptNonce != 0u &&
            IsValidIdentity(acReservation.Identity);
    }

    [[nodiscard]] static constexpr bool IsValidCompletion(
        const PartyQuestNativeLoadBridgeCompletionV1& acCompletion) noexcept
    {
        if (acCompletion.AbiVersion != kPartyQuestNativeLoadBridgePayloadAbi ||
            acCompletion.StructSize !=
                sizeof(PartyQuestNativeLoadBridgeCompletionV1) ||
            acCompletion.AttemptNonce == 0u ||
            acCompletion.EventSequence == 0u ||
            !IsValidIdentity(acCompletion.Identity) ||
            acCompletion.Result > 1u)
        {
            return false;
        }

        for (const auto value : acCompletion.Reserved)
        {
            if (value != 0u)
                return false;
        }

        return true;
    }
};

static_assert(sizeof(PartyQuestNativeLoadBridgeDescriptorResult) == 4u);
static_assert(sizeof(PartyQuestNativeLoadBridgeStatus) == 4u);

static_assert(sizeof(PartyQuestNativeLoadBridgeDescriptorV1) == 72u);
static_assert(alignof(PartyQuestNativeLoadBridgeDescriptorV1) == 8u);
static_assert(offsetof(PartyQuestNativeLoadBridgeDescriptorV1, AbiVersion) == 0u);
static_assert(offsetof(PartyQuestNativeLoadBridgeDescriptorV1, StructSize) == 4u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, PayloadAbiVersion) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, ImplementationVersion) == 12u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, Capabilities) == 16u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, RuntimeMajor) == 24u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, RuntimeFingerprint) == 40u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, BridgeFingerprint) == 48u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, Reserved0) == 56u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeDescriptorV1, Reserved1) == 64u);

static_assert(sizeof(PartyQuestNativeLoadBridgeIdentityV1) == 264u);
static_assert(alignof(PartyQuestNativeLoadBridgeIdentityV1) == 2u);
static_assert(offsetof(PartyQuestNativeLoadBridgeIdentityV1, Length) == 0u);
static_assert(offsetof(PartyQuestNativeLoadBridgeIdentityV1, Reserved0) == 2u);
static_assert(offsetof(PartyQuestNativeLoadBridgeIdentityV1, Bytes) == 4u);
static_assert(
    sizeof(PartyQuestNativeLoadBridgeIdentityV1{}.Bytes) ==
    PartyQuestNativeLoadIdentity::kCapacity);

static_assert(sizeof(PartyQuestNativeLoadBridgeReserveRequestV1) == 272u);
static_assert(alignof(PartyQuestNativeLoadBridgeReserveRequestV1) == 4u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReserveRequestV1, AbiVersion) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReserveRequestV1, StructSize) == 4u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReserveRequestV1, Identity) == 8u);

static_assert(sizeof(PartyQuestNativeLoadBridgeReservationV1) == 280u);
static_assert(alignof(PartyQuestNativeLoadBridgeReservationV1) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReservationV1, AbiVersion) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReservationV1, StructSize) == 4u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReservationV1, AttemptNonce) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeReservationV1, Identity) == 16u);

static_assert(sizeof(PartyQuestNativeLoadBridgeCompletionV1) == 296u);
static_assert(alignof(PartyQuestNativeLoadBridgeCompletionV1) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, AbiVersion) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, StructSize) == 4u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, AttemptNonce) == 8u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, EventSequence) == 16u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, Identity) == 24u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, Result) == 288u);
static_assert(offsetof(
    PartyQuestNativeLoadBridgeCompletionV1, Reserved) == 289u);

static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeDescriptorV1>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeDescriptorV1>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeIdentityV1>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeIdentityV1>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeReserveRequestV1>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeReserveRequestV1>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeReservationV1>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeReservationV1>);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadBridgeCompletionV1>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadBridgeCompletionV1>);
