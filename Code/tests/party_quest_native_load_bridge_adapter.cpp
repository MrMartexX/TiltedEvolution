#include <Structs/Skyrim/PartyQuestNativeLoadBridgeAdapter.h>

#include <catch2/catch.hpp>

#include <cstring>
#include <type_traits>

class PartyQuestNativeLoadBridgeAdapterTestAccess
{
public:
    static PartyQuestNativeLoadBridgeStatus MapCancel(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::Cancel, aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus MapPoll(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::Poll, aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus MapRetire(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::Retire, aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus MapClaim(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::Claim, aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus MapMarkTargetEntered(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::MarkTargetEntered,
            aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus MapComplete(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        PartyQuestNativeLoadRequestStatus aStatus) noexcept
    {
        return aAdapter.MapOperationStatus(
            PartyQuestNativeLoadBridgeAdapter::Operation::Complete, aStatus);
    }

    static PartyQuestNativeLoadBridgeStatus AcceptReservation(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
        const PartyQuestNativeLoadBeginResult& acResult,
        PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept
    {
        return aAdapter.AcceptReservation(
            acRequest, acResult, aReservation);
    }

    static PartyQuestNativeLoadBridgeStatus AcceptCompletion(
        PartyQuestNativeLoadBridgeAdapter& aAdapter,
        uint64_t aAttemptNonce,
        const PartyQuestNativeLoadPollResult& acResult,
        PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept
    {
        return aAdapter.AcceptCompletion(
            aAttemptNonce, acResult, aCompletion);
    }
};

namespace
{
PartyQuestNativeLoadBridgeReserveRequestV1 MakeRequest(const char* apName)
{
    PartyQuestNativeLoadBridgeReserveRequestV1 request{};
    request.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    request.StructSize = sizeof(request);
    request.Identity.Length = static_cast<uint16_t>(std::strlen(apName));
    std::memcpy(
        request.Identity.Bytes,
        apName,
        request.Identity.Length);
    return request;
}
}

TEST_CASE("Native load bridge adapter stays unavailable until exact readiness",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    PartyQuestNativeLoadBridgeDescriptorV1 descriptor;
    std::memset(&descriptor, 0xCD, sizeof(descriptor));

    REQUIRE(adapter.GetDescriptor(descriptor) ==
        PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    REQUIRE(descriptor.StructSize == 0u);
    REQUIRE_FALSE(adapter.PublishReady(0u));
    REQUIRE(adapter.PublishReady(0x1234u));
    REQUIRE_FALSE(adapter.PublishReady(0x5678u));
    REQUIRE(adapter.GetDescriptor(descriptor) ==
        PartyQuestNativeLoadBridgeDescriptorResult::Available);
    REQUIRE(PartyQuestNativeLoadBridgePolicy::IsApprovedDescriptor(
        descriptor, 0x1234u));
}

TEST_CASE("Native load bridge adapter completes and retires exact load",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    REQUIRE(adapter.PublishReady(0x1234u));
    const auto request = MakeRequest("manual-1.ess");
    PartyQuestNativeLoadBridgeReservationV1 reservation;
    REQUIRE(adapter.Reserve(request, reservation) ==
        PartyQuestNativeLoadBridgeStatus::Reserved);
    REQUIRE(reservation.AttemptNonce != 0u);

    PartyQuestNativeLoadBridgeCompletionV1 completion;
    REQUIRE(adapter.Poll(reservation.AttemptNonce, completion) ==
        PartyQuestNativeLoadBridgeStatus::Pending);

    PartyQuestNativeLoadIdentity actual{};
    actual.Length = request.Identity.Length;
    std::memcpy(actual.Bytes, request.Identity.Bytes, actual.Length);
    REQUIRE(adapter.Claim(reservation.AttemptNonce, actual) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.MarkTargetEntered(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.Complete(reservation.AttemptNonce, false) ==
        PartyQuestNativeLoadBridgeStatus::Pending);

    REQUIRE(adapter.Poll(reservation.AttemptNonce, completion) ==
        PartyQuestNativeLoadBridgeStatus::CompletionAvailable);
    REQUIRE(completion.Result == 0u);
    REQUIRE(completion.AttemptNonce == reservation.AttemptNonce);
    REQUIRE(completion.Identity.Length == request.Identity.Length);
    REQUIRE(adapter.Retire(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Retired);
    REQUIRE(adapter.Poll(reservation.AttemptNonce, completion) ==
        PartyQuestNativeLoadBridgeStatus::InvalidState);
    REQUIRE(completion.StructSize == 0u);
}

TEST_CASE("Native load bridge adapter preserves cancellation race semantics",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    REQUIRE(adapter.PublishReady(0x1234u));
    const auto request = MakeRequest("quick-1.ess");
    PartyQuestNativeLoadBridgeReservationV1 reservation;
    REQUIRE(adapter.Reserve(request, reservation) ==
        PartyQuestNativeLoadBridgeStatus::Reserved);
    REQUIRE(adapter.Cancel(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Cancelled);

    REQUIRE(adapter.Reserve(request, reservation) ==
        PartyQuestNativeLoadBridgeStatus::Reserved);
    PartyQuestNativeLoadIdentity actual{};
    actual.Length = request.Identity.Length;
    std::memcpy(actual.Bytes, request.Identity.Bytes, actual.Length);
    REQUIRE(adapter.Claim(reservation.AttemptNonce, actual) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.Cancel(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::InvalidState);
}

TEST_CASE("Native load bridge adapter makes duplicate load events idempotent",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    REQUIRE(adapter.PublishReady(0x1234u));
    const auto request = MakeRequest("replay-1.ess");
    PartyQuestNativeLoadBridgeReservationV1 reservation;
    REQUIRE(adapter.Reserve(request, reservation) ==
        PartyQuestNativeLoadBridgeStatus::Reserved);

    PartyQuestNativeLoadIdentity actual{};
    actual.Length = request.Identity.Length;
    std::memcpy(actual.Bytes, request.Identity.Bytes, actual.Length);
    auto mismatched = actual;
    mismatched.Bytes[0] = 'x';
    REQUIRE(adapter.Claim(reservation.AttemptNonce, mismatched) ==
        PartyQuestNativeLoadBridgeStatus::IdentityMismatch);
    REQUIRE(adapter.Claim(reservation.AttemptNonce, actual) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.Claim(reservation.AttemptNonce, actual) ==
        PartyQuestNativeLoadBridgeStatus::Duplicate);
    REQUIRE(adapter.MarkTargetEntered(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.MarkTargetEntered(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Duplicate);
    REQUIRE(adapter.Complete(reservation.AttemptNonce, true) ==
        PartyQuestNativeLoadBridgeStatus::Pending);
    REQUIRE(adapter.Complete(reservation.AttemptNonce, false) ==
        PartyQuestNativeLoadBridgeStatus::Duplicate);

    PartyQuestNativeLoadBridgeCompletionV1 first;
    PartyQuestNativeLoadBridgeCompletionV1 replay;
    REQUIRE(adapter.Poll(reservation.AttemptNonce, first) ==
        PartyQuestNativeLoadBridgeStatus::CompletionAvailable);
    REQUIRE(adapter.Poll(reservation.AttemptNonce, replay) ==
        PartyQuestNativeLoadBridgeStatus::CompletionAvailable);
    REQUIRE(replay.EventSequence == first.EventSequence);
    REQUIRE(replay.Result == first.Result);
    REQUIRE(adapter.Retire(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Retired);
    REQUIRE(adapter.Retire(reservation.AttemptNonce) ==
        PartyQuestNativeLoadBridgeStatus::Duplicate);
}

TEST_CASE("Native load bridge adapter zeroes outputs on rejected operations",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    auto invalid = MakeRequest("bad");
    invalid.StructSize = 0u;
    PartyQuestNativeLoadBridgeReservationV1 reservation;
    std::memset(&reservation, 0xCD, sizeof(reservation));
    REQUIRE(adapter.Reserve(invalid, reservation) ==
        PartyQuestNativeLoadBridgeStatus::BridgeUnavailable);
    REQUIRE(reservation.AttemptNonce == 0u);

    REQUIRE(adapter.PublishReady(0x1234u));
    std::memset(&reservation, 0xCD, sizeof(reservation));
    REQUIRE(adapter.Reserve(invalid, reservation) ==
        PartyQuestNativeLoadBridgeStatus::InvalidStructSize);
    REQUIRE(reservation.AttemptNonce == 0u);

    invalid = MakeRequest("bad");
    invalid.AbiVersion = 99u;
    REQUIRE(adapter.Reserve(invalid, reservation) ==
        PartyQuestNativeLoadBridgeStatus::UnsupportedAbi);
    invalid = MakeRequest("bad");
    invalid.Identity.Length = 0u;
    REQUIRE(adapter.Reserve(invalid, reservation) ==
        PartyQuestNativeLoadBridgeStatus::InvalidIdentity);

    PartyQuestNativeLoadBridgeCompletionV1 completion;
    std::memset(&completion, 0xCD, sizeof(completion));
    REQUIRE(adapter.Poll(77u, completion) ==
        PartyQuestNativeLoadBridgeStatus::NonceMismatch);
    REQUIRE(completion.EventSequence == 0u);
}

TEST_CASE("Native load bridge adapter poison is terminal",
          "[quest.party-state][native-load-bridge-adapter]")
{
    PartyQuestNativeLoadBridgeAdapter adapter;
    REQUIRE(adapter.PublishReady(0x1234u));
    adapter.Poison();
    REQUIRE(adapter.GetState() ==
        PartyQuestNativeLoadBridgeAdapterState::Poisoned);

    PartyQuestNativeLoadBridgeDescriptorV1 descriptor;
    REQUIRE(adapter.GetDescriptor(descriptor) ==
        PartyQuestNativeLoadBridgeDescriptorResult::Unavailable);
    PartyQuestNativeLoadBridgeReservationV1 reservation;
    REQUIRE(adapter.Reserve(MakeRequest("save.ess"), reservation) ==
        PartyQuestNativeLoadBridgeStatus::Poisoned);
    REQUIRE_FALSE(adapter.PublishReady(0x5678u));
}

TEST_CASE("Native load bridge adapter poisons impossible operation results",
          "[quest.party-state][native-load-bridge-adapter]")
{
    using Access = PartyQuestNativeLoadBridgeAdapterTestAccess;

    const auto requirePoisoned = [](auto aMap) {
        PartyQuestNativeLoadBridgeAdapter adapter;
        REQUIRE(adapter.PublishReady(0x1234u));
        REQUIRE(aMap(adapter) ==
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
        REQUIRE(adapter.GetState() ==
            PartyQuestNativeLoadBridgeAdapterState::Poisoned);
    };

    requirePoisoned([](auto& aAdapter) {
        return Access::MapCancel(
            aAdapter, PartyQuestNativeLoadRequestStatus::Completed);
    });
    requirePoisoned([](auto& aAdapter) {
        return Access::MapPoll(
            aAdapter, PartyQuestNativeLoadRequestStatus::Cancelled);
    });
    requirePoisoned([](auto& aAdapter) {
        return Access::MapRetire(
            aAdapter, PartyQuestNativeLoadRequestStatus::Claimed);
    });
    requirePoisoned([](auto& aAdapter) {
        return Access::MapClaim(
            aAdapter, PartyQuestNativeLoadRequestStatus::Retired);
    });
    requirePoisoned([](auto& aAdapter) {
        return Access::MapMarkTargetEntered(
            aAdapter, PartyQuestNativeLoadRequestStatus::Begun);
    });
    requirePoisoned([](auto& aAdapter) {
        return Access::MapComplete(
            aAdapter, PartyQuestNativeLoadRequestStatus::TargetEntered);
    });
}

TEST_CASE("Native load bridge adapter rejects miscorrelated core payloads",
          "[quest.party-state][native-load-bridge-adapter]")
{
    using Access = PartyQuestNativeLoadBridgeAdapterTestAccess;
    const auto request = MakeRequest("exact-load.ess");

    SECTION("reservation identity differs")
    {
        PartyQuestNativeLoadBridgeAdapter adapter;
        REQUIRE(adapter.PublishReady(0x1234u));
        PartyQuestNativeLoadBeginResult result;
        result.Status = PartyQuestNativeLoadRequestStatus::Begun;
        result.HasReservation = 1u;
        result.Reservation.AttemptNonce = 1u;
        result.Reservation.Identity.Length = request.Identity.Length;
        std::memcpy(
            result.Reservation.Identity.Bytes,
            request.Identity.Bytes,
            request.Identity.Length);
        result.Reservation.Identity.Bytes[0] = 'x';
        PartyQuestNativeLoadBridgeReservationV1 reservation;
        std::memset(&reservation, 0xCD, sizeof(reservation));
        REQUIRE(Access::AcceptReservation(
            adapter, request, result, reservation) ==
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
        REQUIRE(reservation.AttemptNonce == 0u);
        REQUIRE(adapter.GetState() ==
            PartyQuestNativeLoadBridgeAdapterState::Poisoned);
    }

    SECTION("reservation nonce differs")
    {
        PartyQuestNativeLoadBridgeAdapter adapter;
        REQUIRE(adapter.PublishReady(0x1234u));
        PartyQuestNativeLoadBridgeReservationV1 accepted;
        REQUIRE(adapter.Reserve(request, accepted) ==
            PartyQuestNativeLoadBridgeStatus::Reserved);
        PartyQuestNativeLoadBeginResult result;
        result.Status = PartyQuestNativeLoadRequestStatus::Begun;
        result.HasReservation = 1u;
        result.Reservation.AttemptNonce = accepted.AttemptNonce + 1u;
        result.Reservation.Identity.Length = request.Identity.Length;
        std::memcpy(
            result.Reservation.Identity.Bytes,
            request.Identity.Bytes,
            request.Identity.Length);
        PartyQuestNativeLoadBridgeReservationV1 reservation;
        std::memset(&reservation, 0xCD, sizeof(reservation));
        REQUIRE(Access::AcceptReservation(
            adapter, request, result, reservation) ==
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
        REQUIRE(reservation.AttemptNonce == 0u);
        REQUIRE(adapter.GetState() ==
            PartyQuestNativeLoadBridgeAdapterState::Poisoned);
    }

    SECTION("completion nonce differs")
    {
        PartyQuestNativeLoadBridgeAdapter adapter;
        REQUIRE(adapter.PublishReady(0x1234u));
        PartyQuestNativeLoadBridgeReservationV1 reservation;
        REQUIRE(adapter.Reserve(request, reservation) ==
            PartyQuestNativeLoadBridgeStatus::Reserved);
        PartyQuestNativeLoadPollResult result;
        result.Status = PartyQuestNativeLoadRequestStatus::CompletionAvailable;
        result.HasCompletion = 1u;
        result.Completion.AttemptNonce = reservation.AttemptNonce + 1u;
        result.Completion.EventSequence = 1u;
        result.Completion.Identity.Length = request.Identity.Length;
        std::memcpy(
            result.Completion.Identity.Bytes,
            request.Identity.Bytes,
            request.Identity.Length);
        result.Completion.Result = 1u;
        PartyQuestNativeLoadBridgeCompletionV1 completion;
        std::memset(&completion, 0xCD, sizeof(completion));
        REQUIRE(Access::AcceptCompletion(
            adapter, reservation.AttemptNonce, result, completion) ==
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
        REQUIRE(completion.EventSequence == 0u);
        REQUIRE(adapter.GetState() ==
            PartyQuestNativeLoadBridgeAdapterState::Poisoned);
    }

    SECTION("completion identity differs")
    {
        PartyQuestNativeLoadBridgeAdapter adapter;
        REQUIRE(adapter.PublishReady(0x1234u));
        PartyQuestNativeLoadBridgeReservationV1 reservation;
        REQUIRE(adapter.Reserve(request, reservation) ==
            PartyQuestNativeLoadBridgeStatus::Reserved);
        PartyQuestNativeLoadPollResult result;
        result.Status = PartyQuestNativeLoadRequestStatus::CompletionAvailable;
        result.HasCompletion = 1u;
        result.Completion.AttemptNonce = reservation.AttemptNonce;
        result.Completion.EventSequence = 1u;
        result.Completion.Identity.Length = request.Identity.Length;
        std::memcpy(
            result.Completion.Identity.Bytes,
            request.Identity.Bytes,
            request.Identity.Length);
        result.Completion.Identity.Bytes[0] = 'x';
        result.Completion.Result = 1u;
        PartyQuestNativeLoadBridgeCompletionV1 completion;
        REQUIRE(Access::AcceptCompletion(
            adapter, reservation.AttemptNonce, result, completion) ==
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
        REQUIRE(adapter.GetState() ==
            PartyQuestNativeLoadBridgeAdapterState::Poisoned);
    }
}

TEST_CASE("Native load bridge adapter identity is fixed and caller serialized",
          "[quest.party-state][native-load-bridge-adapter]")
{
    STATIC_REQUIRE_FALSE(
        std::is_copy_constructible_v<PartyQuestNativeLoadBridgeAdapter>);
    STATIC_REQUIRE_FALSE(
        std::is_move_constructible_v<PartyQuestNativeLoadBridgeAdapter>);
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestNativeLoadBridgeAdapter&>().PublishReady(1u)));
    STATIC_REQUIRE(noexcept(
        std::declval<PartyQuestNativeLoadBridgeAdapter&>().Poison()));
}
