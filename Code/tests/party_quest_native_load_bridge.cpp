#include <Structs/Skyrim/PartyQuestNativeLoadBridge.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
using Capability = PartyQuestNativeLoadBridgeCapabilityBit;
using Descriptor = PartyQuestNativeLoadBridgeDescriptorV1;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;
using ReserveRequest = PartyQuestNativeLoadBridgeReserveRequestV1;
using Reservation = PartyQuestNativeLoadBridgeReservationV1;
using Completion = PartyQuestNativeLoadBridgeCompletionV1;
using Status = PartyQuestNativeLoadBridgeStatus;
using Policy = PartyQuestNativeLoadBridgePolicy;

constexpr uint64_t kExpectedRuntimeFingerprint =
    0xD00DFEED12345678ull;

Descriptor MakeApprovedDescriptor() noexcept
{
    Descriptor descriptor{};
    descriptor.AbiVersion = kPartyQuestNativeLoadBridgeDescriptorAbi;
    descriptor.StructSize = sizeof(Descriptor);
    descriptor.PayloadAbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    descriptor.ImplementationVersion =
        kPartyQuestNativeLoadBridgeImplementationVersion;
    descriptor.Capabilities = kPartyQuestRequiredNativeLoadBridgeCapabilities;
    descriptor.RuntimeMajor = 1u;
    descriptor.RuntimeMinor = 6u;
    descriptor.RuntimePatch = 1170u;
    descriptor.RuntimeBuild = 0u;
    descriptor.RuntimeFingerprint = kExpectedRuntimeFingerprint;
    descriptor.BridgeFingerprint = kPartyQuestNativeLoadBridgeFingerprint;
    return descriptor;
}

Identity MakeIdentity(uint16_t aLength, uint8_t aByte = 0x41u) noexcept
{
    Identity identity{};
    identity.Length = aLength;
    for (uint16_t index = 0u;
         index < aLength &&
         index < PartyQuestNativeLoadIdentity::kCapacity;
         ++index)
    {
        identity.Bytes[index] = aByte;
    }
    return identity;
}

ReserveRequest MakeReserveRequest() noexcept
{
    ReserveRequest request{};
    request.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    request.StructSize = sizeof(ReserveRequest);
    request.Identity = MakeIdentity(4u);
    return request;
}

Reservation MakeReservation() noexcept
{
    Reservation reservation{};
    reservation.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    reservation.StructSize = sizeof(Reservation);
    reservation.AttemptNonce = 1u;
    reservation.Identity = MakeIdentity(4u);
    return reservation;
}

Completion MakeCompletion(uint8_t aResult) noexcept
{
    Completion completion{};
    completion.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    completion.StructSize = sizeof(Completion);
    completion.AttemptNonce = 1u;
    completion.EventSequence = 1u;
    completion.Identity = MakeIdentity(4u);
    completion.Result = aResult;
    return completion;
}

bool SameBytes(const void* apLeft, const void* apRight, size_t aSize) noexcept
{
    return std::memcmp(apLeft, apRight, aSize) == 0;
}
}

TEST_CASE("Native load bridge accepts only exact approved descriptor",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    const auto descriptor = MakeApprovedDescriptor();
    REQUIRE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint));
}

TEST_CASE("Native load bridge rejects every descriptor version and size mismatch",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    SECTION("descriptor ABI")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.AbiVersion;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("descriptor size")
    {
        auto descriptor = MakeApprovedDescriptor();
        --descriptor.StructSize;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("payload ABI")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.PayloadAbiVersion;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("implementation version")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.ImplementationVersion;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }
}

TEST_CASE("Native load bridge requires exact capability mask",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    constexpr std::array<uint64_t, 11> requiredBits{
        static_cast<uint64_t>(Capability::ExclusiveRequest),
        static_cast<uint64_t>(Capability::ExactNormalizedIdentity),
        static_cast<uint64_t>(Capability::ClaimBeforeTargetEntry),
        static_cast<uint64_t>(Capability::CompletionBeforeLegacyPostLoad),
        static_cast<uint64_t>(Capability::FalseResultCompletion),
        static_cast<uint64_t>(Capability::IdempotentPoll),
        static_cast<uint64_t>(Capability::ExplicitRetirement),
        static_cast<uint64_t>(Capability::MonotonicAttemptNonce),
        static_cast<uint64_t>(Capability::MonotonicEventSequence),
        static_cast<uint64_t>(Capability::PullOnlyNoCallbacks),
        static_cast<uint64_t>(Capability::TargetReturnCompletion)};

    for (const auto bit : requiredBits)
    {
        auto descriptor = MakeApprovedDescriptor();
        descriptor.Capabilities &= ~bit;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    auto unknown = MakeApprovedDescriptor();
    unknown.Capabilities |= 1ull << 63u;
    REQUIRE_FALSE(Policy::IsApprovedDescriptor(
        unknown,
        kExpectedRuntimeFingerprint));
}

TEST_CASE("Native load bridge rejects every runtime version mismatch",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    SECTION("major")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.RuntimeMajor;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("minor")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.RuntimeMinor;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("patch")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.RuntimePatch;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("build")
    {
        auto descriptor = MakeApprovedDescriptor();
        ++descriptor.RuntimeBuild;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }
}

TEST_CASE("Native load bridge requires supplied exact runtime fingerprint",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    const auto descriptor = MakeApprovedDescriptor();

    REQUIRE_FALSE(Policy::IsApprovedDescriptor(descriptor, 0u));
    REQUIRE_FALSE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint + 1u));

    auto mismatch = descriptor;
    ++mismatch.RuntimeFingerprint;
    REQUIRE_FALSE(Policy::IsApprovedDescriptor(
        mismatch,
        kExpectedRuntimeFingerprint));

    REQUIRE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint));
}

TEST_CASE("Native load bridge rejects bridge fingerprint mismatch",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    auto descriptor = MakeApprovedDescriptor();
    ++descriptor.BridgeFingerprint;
    REQUIRE_FALSE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint));
}

TEST_CASE("Native load bridge requires every descriptor reserved field zero",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    SECTION("reserved0")
    {
        auto descriptor = MakeApprovedDescriptor();
        descriptor.Reserved0 = 1u;
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }

    SECTION("reserved1")
    {
        auto descriptor = MakeApprovedDescriptor();
        descriptor.Reserved1 =
            std::numeric_limits<uint64_t>::max();
        REQUIRE_FALSE(Policy::IsApprovedDescriptor(
            descriptor,
            kExpectedRuntimeFingerprint));
    }
}

TEST_CASE("Native load bridge validation never mutates descriptor",
          "[quest.party-state][native-load-bridge][descriptor]")
{
    auto descriptor = MakeApprovedDescriptor();
    const auto before = descriptor;

    REQUIRE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint));
    REQUIRE(SameBytes(&descriptor, &before, sizeof(descriptor)));

    descriptor.Reserved0 = 1u;
    const auto rejectedBefore = descriptor;
    REQUIRE_FALSE(Policy::IsApprovedDescriptor(
        descriptor,
        kExpectedRuntimeFingerprint));
    REQUIRE(SameBytes(
        &descriptor,
        &rejectedBefore,
        sizeof(descriptor)));
}

TEST_CASE("Native load bridge status domain is exact uint32 and rejects unknowns",
          "[quest.party-state][native-load-bridge][status]")
{
    constexpr std::array<uint32_t, 18> known{
        static_cast<uint32_t>(Status::Reserved),
        static_cast<uint32_t>(Status::Cancelled),
        static_cast<uint32_t>(Status::Pending),
        static_cast<uint32_t>(Status::CompletionAvailable),
        static_cast<uint32_t>(Status::Retired),
        static_cast<uint32_t>(Status::Duplicate),
        static_cast<uint32_t>(Status::InvalidArgument),
        static_cast<uint32_t>(Status::UnsupportedAbi),
        static_cast<uint32_t>(Status::InvalidStructSize),
        static_cast<uint32_t>(Status::InvalidIdentity),
        static_cast<uint32_t>(Status::IdentityMismatch),
        static_cast<uint32_t>(Status::NonceMismatch),
        static_cast<uint32_t>(Status::StaleNonce),
        static_cast<uint32_t>(Status::InvalidState),
        static_cast<uint32_t>(Status::CounterExhausted),
        static_cast<uint32_t>(Status::Poisoned),
        static_cast<uint32_t>(Status::BridgeUnavailable),
        static_cast<uint32_t>(Status::InternalFailure)};

    for (const auto value : known)
        REQUIRE(Policy::IsKnownStatus(value));

    for (const auto value : {
             0u,
             7u,
             15u,
             24u,
             31u,
             36u,
             std::numeric_limits<uint32_t>::max()})
    {
        REQUIRE_FALSE(Policy::IsKnownStatus(value));
    }
}

TEST_CASE("Native load bridge identity accepts exact bounded normalized bytes",
          "[quest.party-state][native-load-bridge][identity]")
{
    auto one = MakeIdentity(1u, 0x00u);
    REQUIRE(Policy::IsValidIdentity(one));

    auto maximum = MakeIdentity(
        PartyQuestNativeLoadIdentity::kCapacity,
        0xFFu);
    REQUIRE(Policy::IsValidIdentity(maximum));

    auto empty = MakeIdentity(0u);
    REQUIRE_FALSE(Policy::IsValidIdentity(empty));

    Identity tooLong{};
    tooLong.Length =
        static_cast<uint16_t>(PartyQuestNativeLoadIdentity::kCapacity + 1u);
    REQUIRE_FALSE(Policy::IsValidIdentity(tooLong));

    auto reserved = MakeIdentity(4u);
    reserved.Reserved0 = 1u;
    REQUIRE_FALSE(Policy::IsValidIdentity(reserved));
}

TEST_CASE("Native load bridge identity policy does not invent tail rules",
          "[quest.party-state][native-load-bridge][identity]")
{
    auto identity = MakeIdentity(4u, 0x41u);
    identity.Bytes[4] = 0x11u;
    identity.Bytes[259] = 0x22u;
    REQUIRE(Policy::IsValidIdentity(identity));
}

TEST_CASE("Native load bridge reserve request validates exact ABI and identity",
          "[quest.party-state][native-load-bridge][payload]")
{
    auto request = MakeReserveRequest();
    REQUIRE(Policy::IsValidReserveRequest(request));

    const auto before = request;
    REQUIRE(Policy::IsValidReserveRequest(request));
    REQUIRE(SameBytes(&request, &before, sizeof(request)));

    request.AbiVersion++;
    REQUIRE_FALSE(Policy::IsValidReserveRequest(request));

    request = MakeReserveRequest();
    request.StructSize--;
    REQUIRE_FALSE(Policy::IsValidReserveRequest(request));

    request = MakeReserveRequest();
    request.Identity.Length = 0u;
    REQUIRE_FALSE(Policy::IsValidReserveRequest(request));

    request = MakeReserveRequest();
    request.Identity.Reserved0 = 1u;
    REQUIRE_FALSE(Policy::IsValidReserveRequest(request));
}

TEST_CASE("Native load bridge reservation requires exact ABI nonce and identity",
          "[quest.party-state][native-load-bridge][payload]")
{
    auto reservation = MakeReservation();
    REQUIRE(Policy::IsValidReservation(reservation));

    reservation.AbiVersion++;
    REQUIRE_FALSE(Policy::IsValidReservation(reservation));

    reservation = MakeReservation();
    reservation.StructSize--;
    REQUIRE_FALSE(Policy::IsValidReservation(reservation));

    reservation = MakeReservation();
    reservation.AttemptNonce = 0u;
    REQUIRE_FALSE(Policy::IsValidReservation(reservation));

    reservation = MakeReservation();
    reservation.Identity.Length = 0u;
    REQUIRE_FALSE(Policy::IsValidReservation(reservation));
}

TEST_CASE("Native load bridge completion represents false as valid uint8 zero",
          "[quest.party-state][native-load-bridge][payload]")
{
    const auto completion = MakeCompletion(0u);
    REQUIRE(Policy::IsValidCompletion(completion));
    REQUIRE(completion.Result == 0u);
}

TEST_CASE("Native load bridge completion accepts true one and rejects other result values",
          "[quest.party-state][native-load-bridge][payload]")
{
    REQUIRE(Policy::IsValidCompletion(MakeCompletion(1u)));
    REQUIRE_FALSE(Policy::IsValidCompletion(MakeCompletion(2u)));
    REQUIRE_FALSE(Policy::IsValidCompletion(MakeCompletion(0xFFu)));
}

TEST_CASE("Native load bridge completion validates all exact fields",
          "[quest.party-state][native-load-bridge][payload]")
{
    SECTION("ABI")
    {
        auto completion = MakeCompletion(1u);
        ++completion.AbiVersion;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }

    SECTION("size")
    {
        auto completion = MakeCompletion(1u);
        --completion.StructSize;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }

    SECTION("attempt nonce")
    {
        auto completion = MakeCompletion(1u);
        completion.AttemptNonce = 0u;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }

    SECTION("event sequence")
    {
        auto completion = MakeCompletion(1u);
        completion.EventSequence = 0u;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }

    SECTION("identity")
    {
        auto completion = MakeCompletion(1u);
        completion.Identity.Length = 0u;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }

    SECTION("reserved")
    {
        auto completion = MakeCompletion(1u);
        completion.Reserved[3] = 1u;
        REQUIRE_FALSE(Policy::IsValidCompletion(completion));
    }
}

TEST_CASE("Native load bridge payload validation never mutates inputs",
          "[quest.party-state][native-load-bridge][payload]")
{
    auto request = MakeReserveRequest();
    auto reservation = MakeReservation();
    auto completion = MakeCompletion(0u);

    const auto requestBefore = request;
    const auto reservationBefore = reservation;
    const auto completionBefore = completion;

    REQUIRE(Policy::IsValidReserveRequest(request));
    REQUIRE(Policy::IsValidReservation(reservation));
    REQUIRE(Policy::IsValidCompletion(completion));

    REQUIRE(SameBytes(&request, &requestBefore, sizeof(request)));
    REQUIRE(SameBytes(
        &reservation,
        &reservationBefore,
        sizeof(reservation)));
    REQUIRE(SameBytes(
        &completion,
        &completionBefore,
        sizeof(completion)));
}

TEST_CASE("Native load bridge fixed ABI layout is exact and portable",
          "[quest.party-state][native-load-bridge][abi]")
{
    STATIC_REQUIRE(sizeof(Status) == 4u);

    STATIC_REQUIRE(sizeof(Descriptor) == 72u);
    STATIC_REQUIRE(alignof(Descriptor) == 8u);
    STATIC_REQUIRE(offsetof(Descriptor, AbiVersion) == 0u);
    STATIC_REQUIRE(offsetof(Descriptor, StructSize) == 4u);
    STATIC_REQUIRE(offsetof(Descriptor, PayloadAbiVersion) == 8u);
    STATIC_REQUIRE(offsetof(Descriptor, ImplementationVersion) == 12u);
    STATIC_REQUIRE(offsetof(Descriptor, Capabilities) == 16u);
    STATIC_REQUIRE(offsetof(Descriptor, RuntimeMajor) == 24u);
    STATIC_REQUIRE(offsetof(Descriptor, RuntimeFingerprint) == 40u);
    STATIC_REQUIRE(offsetof(Descriptor, BridgeFingerprint) == 48u);
    STATIC_REQUIRE(offsetof(Descriptor, Reserved0) == 56u);
    STATIC_REQUIRE(offsetof(Descriptor, Reserved1) == 64u);

    STATIC_REQUIRE(sizeof(Identity) == 264u);
    STATIC_REQUIRE(alignof(Identity) == 2u);
    STATIC_REQUIRE(offsetof(Identity, Length) == 0u);
    STATIC_REQUIRE(offsetof(Identity, Reserved0) == 2u);
    STATIC_REQUIRE(offsetof(Identity, Bytes) == 4u);
    STATIC_REQUIRE(
        sizeof(Identity{}.Bytes) ==
        PartyQuestNativeLoadIdentity::kCapacity);

    STATIC_REQUIRE(sizeof(ReserveRequest) == 272u);
    STATIC_REQUIRE(alignof(ReserveRequest) == 4u);
    STATIC_REQUIRE(offsetof(ReserveRequest, AbiVersion) == 0u);
    STATIC_REQUIRE(offsetof(ReserveRequest, StructSize) == 4u);
    STATIC_REQUIRE(offsetof(ReserveRequest, Identity) == 8u);

    STATIC_REQUIRE(sizeof(Reservation) == 280u);
    STATIC_REQUIRE(alignof(Reservation) == 8u);
    STATIC_REQUIRE(offsetof(Reservation, AbiVersion) == 0u);
    STATIC_REQUIRE(offsetof(Reservation, StructSize) == 4u);
    STATIC_REQUIRE(offsetof(Reservation, AttemptNonce) == 8u);
    STATIC_REQUIRE(offsetof(Reservation, Identity) == 16u);

    STATIC_REQUIRE(sizeof(Completion) == 296u);
    STATIC_REQUIRE(alignof(Completion) == 8u);
    STATIC_REQUIRE(offsetof(Completion, AbiVersion) == 0u);
    STATIC_REQUIRE(offsetof(Completion, StructSize) == 4u);
    STATIC_REQUIRE(offsetof(Completion, AttemptNonce) == 8u);
    STATIC_REQUIRE(offsetof(Completion, EventSequence) == 16u);
    STATIC_REQUIRE(offsetof(Completion, Identity) == 24u);
    STATIC_REQUIRE(offsetof(Completion, Result) == 288u);
    STATIC_REQUIRE(offsetof(Completion, Reserved) == 289u);

    STATIC_REQUIRE(std::is_standard_layout_v<Descriptor>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Descriptor>);
    STATIC_REQUIRE(std::is_standard_layout_v<Identity>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Identity>);
    STATIC_REQUIRE(std::is_standard_layout_v<ReserveRequest>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ReserveRequest>);
    STATIC_REQUIRE(std::is_standard_layout_v<Reservation>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Reservation>);
    STATIC_REQUIRE(std::is_standard_layout_v<Completion>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Completion>);

    STATIC_REQUIRE(noexcept(Policy::IsApprovedDescriptor(
        std::declval<const Descriptor&>(),
        uint64_t{})));
    STATIC_REQUIRE(noexcept(Policy::IsKnownStatus(uint32_t{})));
    STATIC_REQUIRE(noexcept(Policy::IsValidIdentity(
        std::declval<const Identity&>())));
    STATIC_REQUIRE(noexcept(Policy::IsValidReserveRequest(
        std::declval<const ReserveRequest&>())));
    STATIC_REQUIRE(noexcept(Policy::IsValidReservation(
        std::declval<const Reservation&>())));
    STATIC_REQUIRE(noexcept(Policy::IsValidCompletion(
        std::declval<const Completion&>())));
}

TEST_CASE("Native load bridge ABI constants are fixed and capability mask has no unknown bits",
          "[quest.party-state][native-load-bridge][abi]")
{
    STATIC_REQUIRE(kPartyQuestNativeLoadBridgeDescriptorAbi == 1u);
    STATIC_REQUIRE(kPartyQuestNativeLoadBridgePayloadAbi == 1u);
    STATIC_REQUIRE(kPartyQuestNativeLoadBridgeImplementationVersion == 1u);
    STATIC_REQUIRE(
        kPartyQuestNativeLoadBridgeFingerprint ==
        0x315644414F4C5150ull);
    STATIC_REQUIRE(
        static_cast<uint64_t>(Capability::TargetReturnCompletion) ==
        (1ull << 10u));
    STATIC_REQUIRE(
        kPartyQuestRequiredNativeLoadBridgeCapabilities ==
        ((1ull << 11u) - 1ull));
}
