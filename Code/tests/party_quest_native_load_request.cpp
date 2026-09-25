#include <Structs/Skyrim/PartyQuestNativeLoadRequest.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
using Request = PartyQuestNativeLoadRequest;
using State = PartyQuestNativeLoadRequestState;
using Status = PartyQuestNativeLoadRequestStatus;
using Identity = PartyQuestNativeLoadIdentity;
using Completion = PartyQuestNativeLoadCompletion;

Identity MakeIdentity(const char* apBytes, size_t aLength)
{
    Identity identity{};
    REQUIRE(aLength <= Identity::kCapacity);
    identity.Length = static_cast<uint16_t>(aLength);
    if (aLength != 0u)
        std::memcpy(identity.Bytes, apBytes, aLength);
    return identity;
}

Identity MakeFilledIdentity(char aValue, uint16_t aLength)
{
    Identity identity{};
    REQUIRE(aLength <= Identity::kCapacity);
    identity.Length = aLength;
    std::fill_n(identity.Bytes, aLength, aValue);
    return identity;
}

bool SameIdentity(const Identity& acLeft, const Identity& acRight) noexcept
{
    return acLeft.Length == acRight.Length &&
        std::memcmp(acLeft.Bytes, acRight.Bytes, acLeft.Length) == 0;
}

bool SameCompletion(
    const Completion& acLeft,
    const Completion& acRight) noexcept
{
    return acLeft.AttemptNonce == acRight.AttemptNonce &&
        acLeft.EventSequence == acRight.EventSequence &&
        acLeft.Result == acRight.Result &&
        SameIdentity(acLeft.Identity, acRight.Identity) &&
        std::memcmp(acLeft.Reserved, acRight.Reserved,
            sizeof(acLeft.Reserved)) == 0;
}

uint64_t BeginExact(Request& aRequest, const Identity& acIdentity)
{
    const auto begun = aRequest.Begin(acIdentity);
    REQUIRE(begun.Status == Status::Begun);
    REQUIRE(begun.HasReservation == 1u);
    REQUIRE(begun.Reservation.AttemptNonce != 0u);
    REQUIRE(SameIdentity(begun.Reservation.Identity, acIdentity));
    return begun.Reservation.AttemptNonce;
}

void ClaimAndEnter(
    Request& aRequest,
    uint64_t aNonce,
    const Identity& acIdentity)
{
    REQUIRE(aRequest.Claim(aNonce, acIdentity) == Status::Claimed);
    REQUIRE(aRequest.GetState() == State::Claimed);
    REQUIRE(aRequest.MarkTargetEntered(aNonce) == Status::TargetEntered);
}
}

class PartyQuestNativeLoadRequestTestAccess final
{
public:
    static void SetLastAttemptNonce(
        PartyQuestNativeLoadRequest& aRequest,
        uint64_t aValue) noexcept
    {
        aRequest.m_lastAttemptNonce = aValue;
    }

    static void SetLastEventSequence(
        PartyQuestNativeLoadRequest& aRequest,
        uint64_t aValue) noexcept
    {
        aRequest.m_lastEventSequence = aValue;
    }

    static uint64_t LastAttemptNonce(
        const PartyQuestNativeLoadRequest& aRequest) noexcept
    {
        return aRequest.m_lastAttemptNonce;
    }

    static uint64_t LastEventSequence(
        const PartyQuestNativeLoadRequest& aRequest) noexcept
    {
        return aRequest.m_lastEventSequence;
    }
};

TEST_CASE("Native load request completes true and polls immutable completion",
          "[quest.party-state][native-load-request]")
{
    Request request;
    const auto identity = MakeIdentity("slot-A", 6u);
    const auto nonce = BeginExact(request, identity);

    REQUIRE(request.GetState() == State::Reserved);
    ClaimAndEnter(request, nonce, identity);

    REQUIRE(request.Complete(nonce, true) == Status::Completed);
    REQUIRE(request.GetState() == State::Completed);

    const auto first = request.Poll(nonce);
    REQUIRE(first.Status == Status::CompletionAvailable);
    REQUIRE(first.HasCompletion == 1u);
    REQUIRE(first.Completion.AttemptNonce == nonce);
    REQUIRE(first.Completion.EventSequence == 1u);
    REQUIRE(first.Completion.Succeeded());
    REQUIRE(SameIdentity(first.Completion.Identity, identity));

    const auto duplicate = request.Poll(nonce);
    REQUIRE(duplicate.Status == Status::CompletionAvailable);
    REQUIRE(duplicate.HasCompletion == 1u);
    REQUIRE(SameCompletion(duplicate.Completion, first.Completion));
    REQUIRE(request.GetState() == State::Completed);
    REQUIRE(PartyQuestNativeLoadRequestTestAccess::LastEventSequence(
        request) == 1u);

    REQUIRE(request.Retire(nonce) == Status::Retired);
    REQUIRE(request.GetState() == State::Retired);
}

TEST_CASE("Native load request records false completion exactly",
          "[quest.party-state][native-load-request]")
{
    Request request;
    const auto identity = MakeIdentity("slot-false", 10u);
    const auto nonce = BeginExact(request, identity);
    ClaimAndEnter(request, nonce, identity);

    REQUIRE(request.Complete(nonce, false) == Status::Completed);
    const auto polled = request.Poll(nonce);
    REQUIRE(polled.Status == Status::CompletionAvailable);
    REQUIRE(polled.HasCompletion == 1u);
    REQUIRE_FALSE(polled.Completion.Succeeded());
    REQUIRE(polled.Completion.Result == 0u);
    REQUIRE(request.Retire(nonce) == Status::Retired);
}

TEST_CASE("Completed retired A cannot affect later B",
          "[quest.party-state][native-load-request][aba]")
{
    Request request;
    const auto identityA = MakeIdentity("A", 1u);
    const auto identityB = MakeIdentity("B", 1u);

    const auto nonceA = BeginExact(request, identityA);
    ClaimAndEnter(request, nonceA, identityA);
    REQUIRE(request.Complete(nonceA, true) == Status::Completed);
    const auto completionA = request.Poll(nonceA);
    REQUIRE(completionA.Completion.EventSequence == 1u);
    REQUIRE(request.Retire(nonceA) == Status::Retired);

    const auto nonceB = BeginExact(request, identityB);
    REQUIRE(nonceB > nonceA);
    REQUIRE(request.GetState() == State::Reserved);

    REQUIRE(request.Cancel(nonceA) == Status::StaleNonce);
    REQUIRE(request.Claim(nonceA, identityA) == Status::StaleNonce);
    REQUIRE(request.MarkTargetEntered(nonceA) == Status::StaleNonce);
    REQUIRE(request.Complete(nonceA, false) == Status::StaleNonce);
    REQUIRE(request.Poll(nonceA).Status == Status::StaleNonce);
    REQUIRE(request.Retire(nonceA) == Status::StaleNonce);
    REQUIRE(request.GetState() == State::Reserved);
    REQUIRE(request.GetCurrentAttemptNonce() == nonceB);

    ClaimAndEnter(request, nonceB, identityB);
    REQUIRE(request.Complete(nonceB, true) == Status::Completed);
    const auto completionB = request.Poll(nonceB);
    REQUIRE(completionB.Completion.EventSequence == 2u);
    REQUIRE(completionB.Completion.AttemptNonce == nonceB);
    REQUIRE(request.Retire(nonceB) == Status::Retired);
}

TEST_CASE("Native load claim requires exact bounded name and nonce",
          "[quest.party-state][native-load-request][identity]")
{
    Request request;
    const auto exact = MakeIdentity("profile/save-A", 14u);
    const auto samePrefixLonger = MakeIdentity("profile/save-AB", 15u);
    const auto sameLengthDifferent = MakeIdentity("profile/save-B", 14u);
    const auto nonce = BeginExact(request, exact);

    REQUIRE(request.Claim(nonce + 1u, exact) == Status::NonceMismatch);
    REQUIRE(request.GetState() == State::Reserved);

    REQUIRE(request.Claim(nonce, samePrefixLonger) ==
        Status::IdentityMismatch);
    REQUIRE(request.GetState() == State::Reserved);

    REQUIRE(request.Claim(nonce, sameLengthDifferent) ==
        Status::IdentityMismatch);
    REQUIRE(request.GetState() == State::Reserved);

    REQUIRE(request.Claim(nonce, exact) == Status::Claimed);
    REQUIRE(request.GetState() == State::Claimed);
}

TEST_CASE("Native load identity accepts exact full bounded capacity",
          "[quest.party-state][native-load-request][identity]")
{
    Request request;
    const auto exact =
        MakeFilledIdentity('X', Identity::kCapacity);
    auto different = exact;
    different.Bytes[Identity::kCapacity - 1u] = 'Y';

    const auto nonce = BeginExact(request, exact);
    REQUIRE(request.Claim(nonce, different) == Status::IdentityMismatch);
    REQUIRE(request.GetState() == State::Reserved);
    REQUIRE(request.Claim(nonce, exact) == Status::Claimed);
}

TEST_CASE("Native load identity is length plus bytes and ignores tail storage",
          "[quest.party-state][native-load-request][identity]")
{
    Request request;
    auto reserved = MakeIdentity("same", 4u);
    auto actual = reserved;
    reserved.Bytes[4] = 'A';
    actual.Bytes[4] = 'B';

    const auto nonce = BeginExact(request, reserved);
    REQUIRE(request.Claim(nonce, actual) == Status::Claimed);
}

TEST_CASE("Invalid native load identities fail without state or counter mutation",
          "[quest.party-state][native-load-request][failure]")
{
    Request request;
    Identity empty{};
    Identity tooLong{};
    tooLong.Length = Identity::kCapacity + 1u;
    const auto beforeNonce =
        PartyQuestNativeLoadRequestTestAccess::LastAttemptNonce(request);

    const auto invalid = request.Begin(empty);
    REQUIRE(invalid.Status == Status::InvalidIdentity);
    REQUIRE(invalid.HasReservation == 0u);
    REQUIRE(request.GetState() == State::Idle);
    REQUIRE(PartyQuestNativeLoadRequestTestAccess::LastAttemptNonce(request) ==
        beforeNonce);

    const auto overCapacity = request.Begin(tooLong);
    REQUIRE(overCapacity.Status == Status::InvalidIdentity);
    REQUIRE(overCapacity.HasReservation == 0u);
    REQUIRE(request.GetState() == State::Idle);
    REQUIRE(PartyQuestNativeLoadRequestTestAccess::LastAttemptNonce(request) ==
        beforeNonce);

    const auto exact = MakeIdentity("valid", 5u);
    const auto nonce = BeginExact(request, exact);
    REQUIRE(request.Claim(nonce, empty) == Status::InvalidIdentity);
    REQUIRE(request.GetState() == State::Reserved);
    REQUIRE(request.Claim(nonce, exact) == Status::Claimed);
}

TEST_CASE("Cancel is exact and valid only before claim",
          "[quest.party-state][native-load-request][cancel]")
{
    SECTION("reserved cancellation retires reservation")
    {
        Request request;
        const auto identity = MakeIdentity("cancel", 6u);
        const auto nonce = BeginExact(request, identity);

        REQUIRE(request.Cancel(nonce + 1u) == Status::NonceMismatch);
        REQUIRE(request.GetState() == State::Reserved);
        REQUIRE(request.Cancel(nonce) == Status::Cancelled);
        REQUIRE(request.GetState() == State::Retired);
        REQUIRE(request.Cancel(nonce) == Status::Duplicate);
        REQUIRE(request.MarkTargetEntered(nonce) == Status::InvalidState);
        REQUIRE(request.Complete(nonce, true) == Status::InvalidState);

        const auto next = BeginExact(request, identity);
        REQUIRE(next > nonce);
        REQUIRE(request.Cancel(nonce) == Status::StaleNonce);
    }

    SECTION("claim closes cancellation path")
    {
        Request request;
        const auto identity = MakeIdentity("claimed", 7u);
        const auto nonce = BeginExact(request, identity);
        REQUIRE(request.Claim(nonce, identity) == Status::Claimed);
        REQUIRE(request.Cancel(nonce) == Status::InvalidState);
        REQUIRE(request.GetState() == State::Claimed);
    }
}

TEST_CASE("Target entry is exact and duplicate entry is inert",
          "[quest.party-state][native-load-request][entry]")
{
    Request request;
    const auto identity = MakeIdentity("enter", 5u);
    const auto nonce = BeginExact(request, identity);

    REQUIRE(request.MarkTargetEntered(nonce) == Status::InvalidState);
    REQUIRE(request.GetState() == State::Reserved);

    REQUIRE(request.Claim(nonce, identity) == Status::Claimed);
    REQUIRE(request.MarkTargetEntered(nonce + 1u) == Status::NonceMismatch);
    REQUIRE(request.MarkTargetEntered(nonce) == Status::TargetEntered);
    REQUIRE(request.MarkTargetEntered(nonce) == Status::Duplicate);
    REQUIRE(request.GetState() == State::Claimed);

    REQUIRE(request.Complete(nonce, true) == Status::Completed);
    REQUIRE(request.MarkTargetEntered(nonce) == Status::Duplicate);
    REQUIRE(request.GetState() == State::Completed);
}

TEST_CASE("Complete requires target entry and duplicate completion is inert",
          "[quest.party-state][native-load-request][completion]")
{
    Request request;
    const auto identity = MakeIdentity("complete", 8u);
    const auto nonce = BeginExact(request, identity);

    REQUIRE(request.Complete(nonce, true) == Status::InvalidState);
    REQUIRE(request.Claim(nonce, identity) == Status::Claimed);
    REQUIRE(request.Complete(nonce, true) == Status::InvalidState);
    REQUIRE(request.MarkTargetEntered(nonce) == Status::TargetEntered);

    REQUIRE(request.Complete(nonce, true) == Status::Completed);
    const auto exact = request.Poll(nonce);
    REQUIRE(exact.HasCompletion == 1u);

    REQUIRE(request.Complete(nonce, false) == Status::Duplicate);
    const auto afterDuplicate = request.Poll(nonce);
    REQUIRE(afterDuplicate.HasCompletion == 1u);
    REQUIRE(SameCompletion(
        afterDuplicate.Completion,
        exact.Completion));
}

TEST_CASE("Poll is exact idempotent and never consumes completion twice",
          "[quest.party-state][native-load-request][poll]")
{
    Request request;
    const auto identity = MakeIdentity("poll", 4u);
    const auto nonce = BeginExact(request, identity);
    ClaimAndEnter(request, nonce, identity);
    REQUIRE(request.Complete(nonce, false) == Status::Completed);

    const auto first = request.Poll(nonce);
    const auto second = request.Poll(nonce);
    const auto third = request.Poll(nonce);
    REQUIRE(first.Status == Status::CompletionAvailable);
    REQUIRE(second.Status == Status::CompletionAvailable);
    REQUIRE(third.Status == Status::CompletionAvailable);
    REQUIRE(SameCompletion(first.Completion, second.Completion));
    REQUIRE(SameCompletion(second.Completion, third.Completion));
    REQUIRE(PartyQuestNativeLoadRequestTestAccess::LastEventSequence(
        request) == first.Completion.EventSequence);

    REQUIRE(request.Poll(nonce + 1u).Status == Status::NonceMismatch);
    REQUIRE(request.GetState() == State::Completed);
}

TEST_CASE("Retire is exact once and stale retire cannot touch replacement",
          "[quest.party-state][native-load-request][retire]")
{
    Request request;
    const auto identity = MakeIdentity("retire", 6u);
    const auto nonce = BeginExact(request, identity);
    ClaimAndEnter(request, nonce, identity);

    REQUIRE(request.Retire(nonce) == Status::InvalidState);
    REQUIRE(request.Complete(nonce, true) == Status::Completed);
    REQUIRE(request.Retire(nonce + 1u) == Status::NonceMismatch);
    REQUIRE(request.Retire(nonce) == Status::Retired);
    REQUIRE(request.Retire(nonce) == Status::Duplicate);
    REQUIRE(request.Poll(nonce).Status == Status::InvalidState);

    const auto next = BeginExact(request, identity);
    REQUIRE(request.Retire(nonce) == Status::StaleNonce);
    REQUIRE(request.GetCurrentAttemptNonce() == next);
}

TEST_CASE("Begin is exclusive until request is cancelled or retired",
          "[quest.party-state][native-load-request]")
{
    Request request;
    const auto a = MakeIdentity("A", 1u);
    const auto b = MakeIdentity("B", 1u);
    const auto nonce = BeginExact(request, a);

    const auto busy = request.Begin(b);
    REQUIRE(busy.Status == Status::InvalidState);
    REQUIRE(busy.HasReservation == 0u);
    REQUIRE(request.GetState() == State::Reserved);
    REQUIRE(request.GetCurrentAttemptNonce() == nonce);

    REQUIRE(request.Cancel(nonce) == Status::Cancelled);
    const auto replacement = request.Begin(b);
    REQUIRE(replacement.Status == Status::Begun);
    REQUIRE(replacement.Reservation.AttemptNonce > nonce);
}

TEST_CASE("Failure paths preserve exact state and current request",
          "[quest.party-state][native-load-request][failure]")
{
    Request request;
    const auto exact = MakeIdentity("stable", 6u);
    const auto wrong = MakeIdentity("stablE", 6u);
    const auto nonce = BeginExact(request, exact);

    REQUIRE(request.Claim(nonce + 7u, exact) == Status::NonceMismatch);
    REQUIRE(request.Claim(nonce, wrong) == Status::IdentityMismatch);
    REQUIRE(request.GetState() == State::Reserved);
    REQUIRE(request.GetCurrentAttemptNonce() == nonce);

    REQUIRE(request.Claim(nonce, exact) == Status::Claimed);
    REQUIRE(request.Complete(nonce, false) == Status::InvalidState);
    REQUIRE(request.GetState() == State::Claimed);
    REQUIRE(request.GetCurrentAttemptNonce() == nonce);

    REQUIRE(request.MarkTargetEntered(nonce) == Status::TargetEntered);
    REQUIRE(request.Poll(nonce).Status == Status::InvalidState);
    REQUIRE(request.GetState() == State::Claimed);

    REQUIRE(request.Complete(nonce, false) == Status::Completed);
    const auto completion = request.Poll(nonce);
    REQUIRE(completion.HasCompletion == 1u);
    REQUIRE_FALSE(completion.Completion.Succeeded());
}

TEST_CASE("Attempt nonce exhaustion poisons before wraparound",
          "[quest.party-state][native-load-request][exhaustion]")
{
    Request request;
    PartyQuestNativeLoadRequestTestAccess::SetLastAttemptNonce(
        request,
        std::numeric_limits<uint64_t>::max());

    const auto identity = MakeIdentity("nonce", 5u);
    const auto begun = request.Begin(identity);
    REQUIRE(begun.Status == Status::CounterExhausted);
    REQUIRE(begun.HasReservation == 0u);
    REQUIRE(request.GetState() == State::Poisoned);
    REQUIRE(request.GetCurrentAttemptNonce() == 0u);

    REQUIRE(request.Begin(identity).Status == Status::Poisoned);
    REQUIRE(request.Cancel(1u) == Status::Poisoned);
    REQUIRE(request.Claim(1u, identity) == Status::Poisoned);
    REQUIRE(request.MarkTargetEntered(1u) == Status::Poisoned);
    REQUIRE(request.Complete(1u, true) == Status::Poisoned);
    REQUIRE(request.Poll(1u).Status == Status::Poisoned);
    REQUIRE(request.Retire(1u) == Status::Poisoned);
}

TEST_CASE("Event sequence exhaustion poisons completion before wraparound",
          "[quest.party-state][native-load-request][exhaustion]")
{
    Request request;
    const auto identity = MakeIdentity("sequence", 8u);
    const auto nonce = BeginExact(request, identity);
    ClaimAndEnter(request, nonce, identity);

    PartyQuestNativeLoadRequestTestAccess::SetLastEventSequence(
        request,
        std::numeric_limits<uint64_t>::max());

    REQUIRE(request.Complete(nonce, true) == Status::CounterExhausted);
    REQUIRE(request.GetState() == State::Poisoned);
    REQUIRE(PartyQuestNativeLoadRequestTestAccess::LastEventSequence(
        request) == std::numeric_limits<uint64_t>::max());
    REQUIRE(request.Poll(nonce).Status == Status::Poisoned);
    REQUIRE(request.Retire(nonce) == Status::Poisoned);
}

TEST_CASE("Native load counters use UINT64_MAX as final valid value without wrap",
          "[quest.party-state][native-load-request][exhaustion]")
{
    Request request;
    PartyQuestNativeLoadRequestTestAccess::SetLastAttemptNonce(
        request,
        std::numeric_limits<uint64_t>::max() - 1u);

    const auto identity = MakeIdentity("last", 4u);
    const auto begun = request.Begin(identity);
    REQUIRE(begun.Status == Status::Begun);
    REQUIRE(begun.Reservation.AttemptNonce ==
        std::numeric_limits<uint64_t>::max());
    REQUIRE(request.Cancel(begun.Reservation.AttemptNonce) ==
        Status::Cancelled);

    const auto exhausted = request.Begin(identity);
    REQUIRE(exhausted.Status == Status::CounterExhausted);
    REQUIRE(request.GetState() == State::Poisoned);
}

TEST_CASE("UINT64_MAX is the final valid native load event sequence",
          "[quest.party-state][native-load-request][exhaustion]")
{
    Request request;
    const auto identity = MakeIdentity("event-max", 9u);
    const auto firstNonce = BeginExact(request, identity);
    ClaimAndEnter(request, firstNonce, identity);

    PartyQuestNativeLoadRequestTestAccess::SetLastEventSequence(
        request,
        std::numeric_limits<uint64_t>::max() - 1u);

    REQUIRE(request.Complete(firstNonce, true) == Status::Completed);
    const auto final = request.Poll(firstNonce);
    REQUIRE(final.HasCompletion == 1u);
    REQUIRE(final.Completion.EventSequence ==
        std::numeric_limits<uint64_t>::max());
    REQUIRE(request.Retire(firstNonce) == Status::Retired);

    const auto nextNonce = BeginExact(request, identity);
    ClaimAndEnter(request, nextNonce, identity);
    REQUIRE(request.Complete(nextNonce, true) == Status::CounterExhausted);
    REQUIRE(request.GetState() == State::Poisoned);
}

TEST_CASE("Native load completion event sequence is monotonic across requests",
          "[quest.party-state][native-load-request][sequence]")
{
    Request request;
    const auto identity = MakeIdentity("seq", 3u);

    uint64_t previousSequence{};
    for (size_t index = 0u; index < 4u; ++index)
    {
        const auto nonce = BeginExact(request, identity);
        ClaimAndEnter(request, nonce, identity);
        REQUIRE(request.Complete(nonce, (index % 2u) == 0u) ==
            Status::Completed);
        const auto polled = request.Poll(nonce);
        REQUIRE(polled.HasCompletion == 1u);
        REQUIRE(polled.Completion.EventSequence > previousSequence);
        previousSequence = polled.Completion.EventSequence;
        REQUIRE(request.Retire(nonce) == Status::Retired);
    }

    REQUIRE(previousSequence == 4u);
}

TEST_CASE("Native load ABI structs are fixed layout and contract is noexcept identity-bound",
          "[quest.party-state][native-load-request][abi]")
{
    STATIC_REQUIRE(sizeof(State) == 1u);
    STATIC_REQUIRE(sizeof(Status) == 1u);
    STATIC_REQUIRE(sizeof(Identity) == 262u);
    STATIC_REQUIRE(alignof(Identity) == 2u);
    STATIC_REQUIRE(offsetof(Identity, Length) == 0u);
    STATIC_REQUIRE(offsetof(Identity, Bytes) == 2u);
    STATIC_REQUIRE(sizeof(PartyQuestNativeLoadReservation) == 272u);
    STATIC_REQUIRE(sizeof(Completion) == 288u);
    STATIC_REQUIRE(sizeof(PartyQuestNativeLoadBeginResult) == 280u);
    STATIC_REQUIRE(sizeof(PartyQuestNativeLoadPollResult) == 296u);

    STATIC_REQUIRE(std::is_standard_layout_v<Identity>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Identity>);
    STATIC_REQUIRE(std::is_standard_layout_v<
        PartyQuestNativeLoadReservation>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<
        PartyQuestNativeLoadReservation>);
    STATIC_REQUIRE(std::is_standard_layout_v<Completion>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Completion>);

    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Request>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Request>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Request>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Request>);

    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().Begin(std::declval<const Identity&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().Cancel(uint64_t{})));
    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().Claim(
            uint64_t{},
            std::declval<const Identity&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().MarkTargetEntered(uint64_t{})));
    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().Complete(uint64_t{}, true)));
    STATIC_REQUIRE(noexcept(
        std::declval<const Request&>().Poll(uint64_t{})));
    STATIC_REQUIRE(noexcept(
        std::declval<Request&>().Retire(uint64_t{})));
    STATIC_REQUIRE(noexcept(
        std::declval<const Request&>().GetState()));
    STATIC_REQUIRE(noexcept(
        std::declval<const Request&>().GetCurrentAttemptNonce()));
}
