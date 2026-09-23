#include <Structs/Skyrim/PartyQuestNativeLoadBridgeAdapter.h>
#include <Structs/Skyrim/PartyQuestNativeLoadBridgeCallCapability.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <cstring>

namespace
{
using Adapter = PartyQuestNativeLoadBridgeAdapter;
using Owner = PartyQuestNativeLoadBridgeOwnerState;
using Policy = PartyQuestNativeLoadBridgeCallCapabilityPolicy;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using Command = PartyQuestNativeLoadBridgeOwnerCommand;
using CommandKind = PartyQuestNativeLoadBridgeOwnerCommandKind;
using BindDisposition = PartyQuestNativeLoadBridgeOwnerBindDisposition;
using EffectKind = PartyQuestNativeLoadBridgeOwnerEffectKind;
using ForeignDisposition = PartyQuestNativeLoadBridgeOwnerForeignDisposition;
using ResultCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Outcome = PartyQuestNativeLoadBridgeOwnerForeignOutcome;
using Status = PartyQuestNativeLoadBridgeStatus;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;

constexpr uint64_t kFingerprint = 0xC0DEC0DEC0DEC0DEull;

Identity MakeIdentity(
    const char* apName,
    uint8_t aTail = 0u) noexcept
{
    Identity identity{};
    const auto length = std::strlen(apName);
    if (length == 0u ||
        length > PartyQuestNativeLoadIdentity::kCapacity)
    {
        return identity;
    }
    identity.Length = static_cast<uint16_t>(length);
    std::memcpy(identity.Bytes, apName, length);
    if (aTail != 0u && length < sizeof(identity.Bytes))
        identity.Bytes[length] = aTail;
    return identity;
}

PartyQuestNativeLoadIdentity ToInternal(
    const Identity& acIdentity) noexcept
{
    PartyQuestNativeLoadIdentity identity{};
    identity.Length = acIdentity.Length;
    std::memcpy(
        identity.Bytes,
        acIdentity.Bytes,
        acIdentity.Length);
    return identity;
}

Command BindCommand(uint64_t aGeneration) noexcept
{
    Command command{};
    command.Kind = CommandKind::Bind;
    command.BindDisposition = BindDisposition::Authenticated;
    command.RuntimeGeneration = aGeneration;
    command.RuntimeFingerprint = kFingerprint;
    return command;
}

Command ReserveCommand(const Identity& acIdentity) noexcept
{
    Command command{};
    command.Kind = CommandKind::Reserve;
    command.Identity = acIdentity;
    return command;
}

Command NonceCommand(
    CommandKind aKind,
    uint64_t aNonce) noexcept
{
    Command command{};
    command.Kind = aKind;
    command.AttemptNonce = aNonce;
    return command;
}

Command GenerationCommand(uint64_t aGeneration) noexcept
{
    Command command{};
    command.Kind = CommandKind::GenerationTransition;
    command.RuntimeGeneration = aGeneration;
    return command;
}

Outcome BaseOutcome(
    const PartyQuestNativeLoadBridgeOwnerResult& acPlan,
    Status aStatus) noexcept
{
    Outcome outcome{};
    outcome.EffectKind = acPlan.Effect.Kind;
    outcome.Disposition = ForeignDisposition::Returned;
    outcome.RawStatus = static_cast<uint32_t>(aStatus);
    return outcome;
}

void Bind(Owner& aOwner, uint64_t aGeneration)
{
    const auto result = aOwner.Plan(BindCommand(aGeneration));
    REQUIRE(result.Code == ResultCode::Bound);
    REQUIRE(result.ReleaseCapability == 0u);
}

uint64_t Reserve(
    Owner& aOwner,
    Adapter& aAdapter,
    const Identity& acIdentity)
{
    const auto plan = aOwner.Plan(ReserveCommand(acIdentity));
    REQUIRE(plan.Code == ResultCode::EffectRequired);
    REQUIRE(plan.HasEffect == 1u);
    REQUIRE(plan.Effect.Kind == EffectKind::Reserve);
    REQUIRE(plan.Effect.Sequence != 0u);

    const auto authorization =
        Policy::Authorize(aOwner.Snapshot(), plan);
    REQUIRE(authorization.IsAuthorized());
    REQUIRE(authorization.Capability.AuthorizesExactReserve(acIdentity));
    REQUIRE(authorization.Capability.EffectSequence ==
        plan.Effect.Sequence);

    PartyQuestNativeLoadBridgeReservationV1 reservation{};
    const auto status =
        aAdapter.Reserve(plan.Effect.ReserveRequest, reservation);
    REQUIRE(status == Status::Reserved);

    auto outcome = BaseOutcome(plan, status);
    outcome.HasReservation = 1u;
    outcome.Reservation = reservation;

    const auto applied = aOwner.ApplyForeignOutcome(outcome);
    REQUIRE(applied.Code == ResultCode::Reserved);
    REQUIRE(applied.ReleaseCapability == 0u);
    REQUIRE(reservation.AttemptNonce != 0u);
    REQUIRE(aOwner.Snapshot().ActiveAttemptNonce ==
        reservation.AttemptNonce);
    return reservation.AttemptNonce;
}

PartyQuestNativeLoadBridgeOwnerResult ApplyCancel(
    Owner& aOwner,
    Adapter& aAdapter,
    uint64_t aNonce)
{
    const auto plan =
        aOwner.Plan(NonceCommand(CommandKind::Cancel, aNonce));
    REQUIRE(plan.Code == ResultCode::EffectRequired);

    const auto authorization =
        Policy::Authorize(aOwner.Snapshot(), plan);
    REQUIRE(authorization.IsAuthorized());
    REQUIRE(authorization.Capability.AuthorizesExactCall(
        EffectKind::Cancel, aNonce));

    const auto status = aAdapter.Cancel(aNonce);
    auto outcome = BaseOutcome(plan, status);
    return aOwner.ApplyForeignOutcome(outcome);
}

PartyQuestNativeLoadBridgeOwnerResult ApplyPoll(
    Owner& aOwner,
    Adapter& aAdapter,
    uint64_t aNonce)
{
    const auto plan =
        aOwner.Plan(NonceCommand(CommandKind::Poll, aNonce));
    REQUIRE(plan.Code == ResultCode::EffectRequired);

    const auto authorization =
        Policy::Authorize(aOwner.Snapshot(), plan);
    REQUIRE(authorization.IsAuthorized());
    REQUIRE(authorization.Capability.AuthorizesExactCall(
        EffectKind::Poll, aNonce));

    PartyQuestNativeLoadBridgeCompletionV1 completion{};
    const auto status = aAdapter.Poll(aNonce, completion);
    auto outcome = BaseOutcome(plan, status);
    if (status == Status::CompletionAvailable)
    {
        outcome.HasCompletion = 1u;
        outcome.Completion = completion;
    }

    return aOwner.ApplyForeignOutcome(outcome);
}

PartyQuestNativeLoadBridgeOwnerResult ApplyRetire(
    Owner& aOwner,
    Adapter& aAdapter,
    uint64_t aNonce)
{
    const auto plan =
        aOwner.Plan(NonceCommand(CommandKind::Retire, aNonce));
    REQUIRE(plan.Code == ResultCode::EffectRequired);

    const auto authorization =
        Policy::Authorize(aOwner.Snapshot(), plan);
    REQUIRE(authorization.IsAuthorized());
    REQUIRE(authorization.Capability.AuthorizesExactCall(
        EffectKind::Retire, aNonce));

    const auto status = aAdapter.Retire(aNonce);
    auto outcome = BaseOutcome(plan, status);
    return aOwner.ApplyForeignOutcome(outcome);
}
} // namespace

TEST_CASE(
    "Native load adapter and owner agree on false-result completion across generation drain",
    "[quest.party-state][native-load-integration][generation][false-result]")
{
    Adapter adapter;
    Owner owner;

    REQUIRE(adapter.PublishReady(kFingerprint));
    Bind(owner, 10u);

    auto identity = MakeIdentity("ManualSave17.ess", 0xA5u);
    const uint64_t nonce = Reserve(owner, adapter, identity);

    // Provider correlation uses the exact semantic identity. Tail bytes after
    // Length were never part of identity and the reducer canonicalized them.
    REQUIRE(adapter.Claim(nonce, ToInternal(identity)) == Status::Pending);
    REQUIRE(adapter.MarkTargetEntered(nonce) == Status::Pending);

    const auto transition = owner.Plan(GenerationCommand(11u));
    REQUIRE(transition.Code == ResultCode::DrainPending);
    REQUIRE(transition.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);
    REQUIRE(owner.Snapshot().BoundGeneration == 10u);
    REQUIRE(owner.Snapshot().CurrentGeneration == 11u);

    REQUIRE(adapter.Complete(nonce, false) == Status::Pending);

    const auto completed = ApplyPoll(owner, adapter, nonce);
    REQUIRE(completed.Code == ResultCode::CompletionAvailable);
    REQUIRE(completed.HasCompletion == 1u);
    REQUIRE(completed.Completion.AttemptNonce == nonce);
    REQUIRE(completed.Completion.Result == 0u);
    REQUIRE(completed.ReleaseCapability == 0u);

    const auto retired = ApplyRetire(owner, adapter, nonce);
    REQUIRE(retired.Code == ResultCode::Retired);
    REQUIRE(retired.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(owner.Snapshot().CurrentGeneration == 11u);

    PartyQuestNativeLoadBridgeCompletionV1 afterRetire{};
    REQUIRE(adapter.Poll(nonce, afterRetire) == Status::InvalidState);
}

TEST_CASE(
    "Native load adapter and owner agree on cancellation before target claim",
    "[quest.party-state][native-load-integration][cancel]")
{
    Adapter adapter;
    Owner owner;

    REQUIRE(adapter.PublishReady(kFingerprint));
    Bind(owner, 20u);
    const uint64_t nonce =
        Reserve(owner, adapter, MakeIdentity("QuickLoad3.ess"));

    const auto transition = owner.Plan(GenerationCommand(21u));
    REQUIRE(transition.Code == ResultCode::DrainPending);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);

    const auto cancelled = ApplyCancel(owner, adapter, nonce);
    REQUIRE(cancelled.Code == ResultCode::Cancelled);
    REQUIRE(cancelled.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);

    // The provider physically retired the unclaimed reservation as part of
    // Cancelled, so a new owner binding may reserve another request.
    Bind(owner, 21u);
    const uint64_t next =
        Reserve(owner, adapter, MakeIdentity("QuickLoad4.ess"));
    REQUIRE(next > nonce);
}

TEST_CASE(
    "Claimed native load survives cancel race and drains through poll then retire",
    "[quest.party-state][native-load-integration][cancel-race][drain]")
{
    Adapter adapter;
    Owner owner;

    REQUIRE(adapter.PublishReady(kFingerprint));
    Bind(owner, 30u);

    const auto identity = MakeIdentity("AutoSave9.ess");
    const uint64_t nonce = Reserve(owner, adapter, identity);
    REQUIRE(adapter.Claim(nonce, ToInternal(identity)) == Status::Pending);

    const auto transition = owner.Plan(GenerationCommand(31u));
    REQUIRE(transition.Code == ResultCode::DrainPending);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);

    // Once provider Claim won, cancellation is no longer physical retirement.
    // Owner converts InvalidState into Pending and retains the old capability.
    const auto cancel = ApplyCancel(owner, adapter, nonce);
    REQUIRE(cancel.Code == ResultCode::Pending);
    REQUIRE(cancel.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);

    const auto pending = ApplyPoll(owner, adapter, nonce);
    REQUIRE(pending.Code == ResultCode::Pending);
    REQUIRE(pending.ReleaseCapability == 0u);

    REQUIRE(adapter.MarkTargetEntered(nonce) == Status::Pending);
    REQUIRE(adapter.Complete(nonce, true) == Status::Pending);

    const auto completed = ApplyPoll(owner, adapter, nonce);
    REQUIRE(completed.Code == ResultCode::CompletionAvailable);
    REQUIRE(completed.HasCompletion == 1u);
    REQUIRE(completed.Completion.Result == 1u);
    REQUIRE(completed.ReleaseCapability == 0u);

    const auto retired = ApplyRetire(owner, adapter, nonce);
    REQUIRE(retired.Code == ResultCode::Retired);
    REQUIRE(retired.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
}

TEST_CASE(
    "Adapter-owner composition never gives Reserve authority while draining",
    "[quest.party-state][native-load-integration][admission]")
{
    Adapter adapter;
    Owner owner;

    REQUIRE(adapter.PublishReady(kFingerprint));
    Bind(owner, 40u);
    const uint64_t nonce =
        Reserve(owner, adapter, MakeIdentity("ManualSave22.ess"));

    REQUIRE(owner.Plan(GenerationCommand(41u)).Code ==
        ResultCode::DrainPending);

    const auto blocked =
        owner.Plan(ReserveCommand(MakeIdentity("ManualSave23.ess")));
    REQUIRE(blocked.Code == ResultCode::AdmissionClosed);
    REQUIRE(blocked.HasEffect == 0u);

    const auto authorization =
        Policy::Authorize(owner.Snapshot(), blocked);
    REQUIRE_FALSE(authorization.IsAuthorized());
    REQUIRE(authorization.HasCapability == 0u);

    const auto cancelled = ApplyCancel(owner, adapter, nonce);
    REQUIRE(cancelled.Code == ResultCode::Cancelled);
    REQUIRE(cancelled.ReleaseCapability == 1u);
}
