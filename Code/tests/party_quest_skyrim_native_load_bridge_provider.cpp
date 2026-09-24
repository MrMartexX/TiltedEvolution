#include <catch2/catch.hpp>

#if defined(_WIN32)
#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeProvider.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace
{
using Provider = PartyQuestSkyrimNativeLoadBridgeProvider;
using ProviderAbi = PartyQuestSkyrimNativeLoadBridgeProviderAbi;
using BridgeStatus = PartyQuestNativeLoadBridgeStatus;
using DescriptorResult = PartyQuestNativeLoadBridgeDescriptorResult;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;
using Request = PartyQuestNativeLoadBridgeReserveRequestV1;
using Reservation = PartyQuestNativeLoadBridgeReservationV1;
using Completion = PartyQuestNativeLoadBridgeCompletionV1;

constexpr uint64_t kFingerprint = 0x50524F5649444552ull;

Identity MakeBridgeIdentity(const char* apText) noexcept
{
    Identity identity{};
    if (!apText)
        return identity;

    const size_t length = std::strlen(apText);
    if (length == 0u ||
        length > PartyQuestNativeLoadIdentity::kCapacity)
    {
        return identity;
    }

    identity.Length = static_cast<uint16_t>(length);
    std::memcpy(identity.Bytes, apText, length);
    return identity;
}

PartyQuestNativeLoadIdentity MakeInternalIdentity(
    const Identity& acIdentity) noexcept
{
    PartyQuestNativeLoadIdentity identity{};
    identity.Length = acIdentity.Length;
    if (identity.Length <= PartyQuestNativeLoadIdentity::kCapacity)
    {
        std::memcpy(
            identity.Bytes,
            acIdentity.Bytes,
            identity.Length);
    }
    return identity;
}

Request MakeRequest(const Identity& acIdentity) noexcept
{
    Request request{};
    request.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    request.StructSize = sizeof(request);
    request.Identity = acIdentity;
    return request;
}

BridgeStatus Reserve(
    Provider& aProvider,
    const Identity& acIdentity,
    Reservation& aReservation)
{
    const auto request = MakeRequest(acIdentity);
    return static_cast<BridgeStatus>(
        ProviderAbi::Reserve(
            aProvider,
            &request,
            sizeof(request),
            &aReservation,
            sizeof(aReservation)));
}

BridgeStatus Poll(
    Provider& aProvider,
    uint64_t aNonce,
    Completion& aCompletion)
{
    return static_cast<BridgeStatus>(
        ProviderAbi::Poll(
            aProvider,
            aNonce,
            &aCompletion,
            sizeof(aCompletion)));
}
} // namespace

static_assert(std::is_same_v<
    decltype(&PartyQuestProcessNativeLoadBridge_GetDescriptor),
    PartyQuestNativeLoadBridgeGetDescriptorExport>);
static_assert(std::is_same_v<
    decltype(&PartyQuestProcessNativeLoadBridge_Reserve),
    PartyQuestNativeLoadBridgeReserveExport>);
static_assert(std::is_same_v<
    decltype(&PartyQuestProcessNativeLoadBridge_Cancel),
    PartyQuestNativeLoadBridgeCancelExport>);
static_assert(std::is_same_v<
    decltype(&PartyQuestProcessNativeLoadBridge_Poll),
    PartyQuestNativeLoadBridgePollExport>);
static_assert(std::is_same_v<
    decltype(&PartyQuestProcessNativeLoadBridge_Retire),
    PartyQuestNativeLoadBridgeRetireExport>);

TEST_CASE(
    "Process native load provider publishes exact descriptor only after ready",
    "[quest.party-state][native-load-provider][descriptor]")
{
    Provider provider;

    PartyQuestNativeLoadBridgeDescriptorV1 descriptor{};
    REQUIRE(
        ProviderAbi::GetDescriptor(
            provider,
            &descriptor,
            sizeof(descriptor)) ==
        static_cast<uint32_t>(DescriptorResult::Unavailable));
    REQUIRE(descriptor.AbiVersion == 0u);

    REQUIRE(provider.PublishReady(kFingerprint));
    REQUIRE_FALSE(provider.PublishReady(kFingerprint));

    REQUIRE(
        ProviderAbi::GetDescriptor(
            provider,
            &descriptor,
            sizeof(descriptor)) ==
        static_cast<uint32_t>(DescriptorResult::Available));
    REQUIRE(
        PartyQuestNativeLoadBridgePolicy::IsApprovedDescriptor(
            descriptor,
            kFingerprint));
    REQUIRE(
        provider.GetState() ==
        PartyQuestNativeLoadBridgeAdapterState::Ready);
}

TEST_CASE(
    "Process native load provider completes exact false-result target lifecycle",
    "[quest.party-state][native-load-provider][target-return][false-result]")
{
    Provider provider;
    REQUIRE(provider.PublishReady(kFingerprint));

    const auto bridgeIdentity =
        MakeBridgeIdentity("Save42_TEST_Whiterun_000001");
    Reservation reservation{};
    REQUIRE(
        Reserve(provider, bridgeIdentity, reservation) ==
        BridgeStatus::Reserved);
    REQUIRE(reservation.AttemptNonce != 0u);
    REQUIRE(
        provider.GetActiveAttemptNonce() ==
        reservation.AttemptNonce);

    const auto claim =
        provider.ClaimReserved(
            MakeInternalIdentity(bridgeIdentity));
    REQUIRE(claim.IsClaimed());
    REQUIRE(claim.AttemptNonce == reservation.AttemptNonce);

    REQUIRE(
        provider.MarkTargetEntered(claim.AttemptNonce) ==
        BridgeStatus::Pending);

    Completion beforeReturn{};
    REQUIRE(
        Poll(provider, claim.AttemptNonce, beforeReturn) ==
        BridgeStatus::Pending);
    REQUIRE(beforeReturn.AttemptNonce == 0u);

    REQUIRE(
        provider.CompleteTarget(claim.AttemptNonce, false) ==
        BridgeStatus::Pending);

    Completion completion{};
    REQUIRE(
        Poll(provider, claim.AttemptNonce, completion) ==
        BridgeStatus::CompletionAvailable);
    REQUIRE(
        PartyQuestNativeLoadBridgePolicy::IsValidCompletion(
            completion));
    REQUIRE(completion.AttemptNonce == claim.AttemptNonce);
    REQUIRE(completion.EventSequence != 0u);
    REQUIRE(completion.Result == 0u);
    REQUIRE(completion.Identity.Length == bridgeIdentity.Length);
    REQUIRE(
        std::memcmp(
            completion.Identity.Bytes,
            bridgeIdentity.Bytes,
            bridgeIdentity.Length) == 0);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Retire(provider, claim.AttemptNonce)) ==
        BridgeStatus::Retired);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);
}

TEST_CASE(
    "Process native load provider cancels exact reservation before target claim",
    "[quest.party-state][native-load-provider][cancel]")
{
    Provider provider;
    REQUIRE(provider.PublishReady(kFingerprint));

    Reservation reservation{};
    REQUIRE(
        Reserve(
            provider,
            MakeBridgeIdentity("SaveCancel_TEST"),
            reservation) ==
        BridgeStatus::Reserved);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Cancel(
                provider,
                reservation.AttemptNonce)) ==
        BridgeStatus::Cancelled);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);

    Reservation replacement{};
    REQUIRE(
        Reserve(
            provider,
            MakeBridgeIdentity("SaveReplacement_TEST"),
            replacement) ==
        BridgeStatus::Reserved);
    REQUIRE(replacement.AttemptNonce > reservation.AttemptNonce);
}

TEST_CASE(
    "Process native load provider rejects identity substitution without losing reservation",
    "[quest.party-state][native-load-provider][identity]")
{
    Provider provider;
    REQUIRE(provider.PublishReady(kFingerprint));

    const auto expected =
        MakeBridgeIdentity("SaveExpected_TEST");
    Reservation reservation{};
    REQUIRE(
        Reserve(provider, expected, reservation) ==
        BridgeStatus::Reserved);

    const auto mismatched =
        provider.ClaimReserved(
            MakeInternalIdentity(
                MakeBridgeIdentity("SaveOther_TEST")));
    REQUIRE_FALSE(mismatched.IsClaimed());
    REQUIRE(mismatched.Status == BridgeStatus::IdentityMismatch);
    REQUIRE(provider.GetActiveAttemptNonce() ==
        reservation.AttemptNonce);

    const auto exact =
        provider.ClaimReserved(
            MakeInternalIdentity(expected));
    REQUIRE(exact.IsClaimed());
    REQUIRE(exact.AttemptNonce == reservation.AttemptNonce);

    REQUIRE(
        provider.MarkTargetEntered(exact.AttemptNonce) ==
        BridgeStatus::Pending);
    REQUIRE(
        provider.CompleteTarget(exact.AttemptNonce, true) ==
        BridgeStatus::Pending);

    Completion completion{};
    REQUIRE(
        Poll(provider, exact.AttemptNonce, completion) ==
        BridgeStatus::CompletionAvailable);
    REQUIRE(completion.Result == 1u);
    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Retire(
                provider,
                exact.AttemptNonce)) ==
        BridgeStatus::Retired);
}

TEST_CASE(
    "Process native load provider preserves first reservation across duplicate reserve",
    "[quest.party-state][native-load-provider][exclusive]")
{
    Provider provider;
    REQUIRE(provider.PublishReady(kFingerprint));

    Reservation first{};
    REQUIRE(
        Reserve(
            provider,
            MakeBridgeIdentity("SaveFirst_TEST"),
            first) ==
        BridgeStatus::Reserved);

    Reservation duplicate{};
    REQUIRE(
        Reserve(
            provider,
            MakeBridgeIdentity("SaveSecond_TEST"),
            duplicate) ==
        BridgeStatus::InvalidState);
    REQUIRE(duplicate.AttemptNonce == 0u);
    REQUIRE(provider.GetActiveAttemptNonce() == first.AttemptNonce);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Cancel(
                provider,
                first.AttemptNonce)) ==
        BridgeStatus::Cancelled);
}

TEST_CASE(
    "Process native load provider ABI contains invalid foreign pointers",
    "[quest.party-state][native-load-provider][abi][seh]")
{
    Provider provider;
    REQUIRE(provider.PublishReady(kFingerprint));

    auto* badRequest =
        reinterpret_cast<const Request*>(uintptr_t{1u});
    auto* badReservation =
        reinterpret_cast<Reservation*>(uintptr_t{1u});
    auto* badDescriptor =
        reinterpret_cast<
            PartyQuestNativeLoadBridgeDescriptorV1*>(
                uintptr_t{1u});
    auto* badCompletion =
        reinterpret_cast<Completion*>(uintptr_t{1u});

    Reservation reservation{};
    const auto validRequest =
        MakeRequest(MakeBridgeIdentity("SaveAbi_TEST"));

    REQUIRE(
        ProviderAbi::GetDescriptor(
            provider,
            badDescriptor,
            sizeof(PartyQuestNativeLoadBridgeDescriptorV1)) ==
        static_cast<uint32_t>(DescriptorResult::Unavailable));

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Reserve(
                provider,
                badRequest,
                sizeof(Request),
                &reservation,
                sizeof(reservation))) ==
        BridgeStatus::InvalidArgument);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Reserve(
                provider,
                &validRequest,
                sizeof(Request),
                badReservation,
                sizeof(Reservation))) ==
        BridgeStatus::InvalidArgument);
    REQUIRE(provider.GetActiveAttemptNonce() == 0u);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Reserve(
                provider,
                &validRequest,
                sizeof(Request) - 1u,
                &reservation,
                sizeof(reservation))) ==
        BridgeStatus::InvalidStructSize);

    REQUIRE(
        static_cast<BridgeStatus>(
            ProviderAbi::Poll(
                provider,
                1u,
                badCompletion,
                sizeof(Completion))) ==
        BridgeStatus::InvalidArgument);

    REQUIRE(
        provider.GetState() ==
        PartyQuestNativeLoadBridgeAdapterState::Ready);
}
#endif
