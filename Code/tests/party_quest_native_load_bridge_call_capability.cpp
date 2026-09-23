#include <Structs/Skyrim/PartyQuestNativeLoadBridgeCallCapability.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
using Owner = PartyQuestNativeLoadBridgeOwnerState;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using RequestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase;
using Command = PartyQuestNativeLoadBridgeOwnerCommand;
using CommandKind = PartyQuestNativeLoadBridgeOwnerCommandKind;
using BindDisposition = PartyQuestNativeLoadBridgeOwnerBindDisposition;
using EffectKind = PartyQuestNativeLoadBridgeOwnerEffectKind;
using ForeignDisposition = PartyQuestNativeLoadBridgeOwnerForeignDisposition;
using Result = PartyQuestNativeLoadBridgeOwnerResult;
using ResultCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Outcome = PartyQuestNativeLoadBridgeOwnerForeignOutcome;
using Status = PartyQuestNativeLoadBridgeStatus;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;
using Reservation = PartyQuestNativeLoadBridgeReservationV1;
using Completion = PartyQuestNativeLoadBridgeCompletionV1;
using Capability = PartyQuestNativeLoadBridgeCallCapability;
using CapabilityStatus = PartyQuestNativeLoadBridgeCallCapabilityStatus;
using Authority = PartyQuestNativeLoadBridgeCallAuthority;
using Policy = PartyQuestNativeLoadBridgeCallCapabilityPolicy;

constexpr uint64_t kFingerprint = 0xA701A701A701A701ull;

Identity MakeIdentity(uint8_t aSeed = 1u) noexcept
{
    Identity identity{};
    identity.Length = 4u;
    identity.Bytes[0] = aSeed;
    identity.Bytes[1] = static_cast<uint8_t>(aSeed + 1u);
    identity.Bytes[2] = static_cast<uint8_t>(aSeed + 2u);
    identity.Bytes[3] = static_cast<uint8_t>(aSeed + 3u);
    return identity;
}

Command BindCommand(
    uint64_t aGeneration,
    uint64_t aFingerprint = kFingerprint) noexcept
{
    Command command{};
    command.Kind = CommandKind::Bind;
    command.BindDisposition = BindDisposition::Authenticated;
    command.RuntimeGeneration = aGeneration;
    command.RuntimeFingerprint = aFingerprint;
    return command;
}

Command ReserveCommand(const Identity& acIdentity = MakeIdentity()) noexcept
{
    Command command{};
    command.Kind = CommandKind::Reserve;
    command.Identity = acIdentity;
    return command;
}

Command NonceCommand(CommandKind aKind, uint64_t aNonce) noexcept
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

Command ShutdownCommand() noexcept
{
    Command command{};
    command.Kind = CommandKind::Shutdown;
    return command;
}

Reservation MakeReservation(
    uint64_t aNonce,
    const Identity& acIdentity) noexcept
{
    Reservation reservation{};
    reservation.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    reservation.StructSize = sizeof(reservation);
    reservation.AttemptNonce = aNonce;
    reservation.Identity = acIdentity;
    return reservation;
}

Completion MakeCompletion(
    uint64_t aNonce,
    uint64_t aSequence,
    const Identity& acIdentity,
    uint8_t aResult = 1u) noexcept
{
    Completion completion{};
    completion.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    completion.StructSize = sizeof(completion);
    completion.AttemptNonce = aNonce;
    completion.EventSequence = aSequence;
    completion.Identity = acIdentity;
    completion.Result = aResult;
    return completion;
}

Outcome ReturnedOutcome(
    EffectKind aKind,
    Status aStatus) noexcept
{
    Outcome outcome{};
    outcome.EffectKind = aKind;
    outcome.Disposition = ForeignDisposition::Returned;
    outcome.RawStatus = static_cast<uint32_t>(aStatus);
    return outcome;
}

Outcome ReserveSuccess(
    uint64_t aNonce,
    const Identity& acIdentity) noexcept
{
    auto outcome = ReturnedOutcome(EffectKind::Reserve, Status::Reserved);
    outcome.HasReservation = 1u;
    outcome.Reservation = MakeReservation(aNonce, acIdentity);
    return outcome;
}

Outcome CompletionSuccess(
    uint64_t aNonce,
    uint64_t aSequence,
    const Identity& acIdentity,
    uint8_t aResult = 1u) noexcept
{
    auto outcome =
        ReturnedOutcome(EffectKind::Poll, Status::CompletionAvailable);
    outcome.HasCompletion = 1u;
    outcome.Completion =
        MakeCompletion(aNonce, aSequence, acIdentity, aResult);
    return outcome;
}

Outcome UnknownOutcome(EffectKind aKind) noexcept
{
    Outcome outcome{};
    outcome.EffectKind = aKind;
    outcome.Disposition = ForeignDisposition::Unknown;
    return outcome;
}

PartyQuestNativeLoadBridgeOwnerResult Apply(
    Owner& aOwner,
    Outcome aOutcome) noexcept
{
    aOutcome.EffectSequence =
        aOwner.Snapshot().PendingEffectSequence;
    return aOwner.ApplyForeignOutcome(aOutcome);
}

void BindOwner(Owner& aOwner, uint64_t aGeneration)
{
    const auto result = aOwner.Plan(BindCommand(aGeneration));
    REQUIRE(result.Code == ResultCode::Bound);
    REQUIRE(result.ReleaseCapability == 0u);
    REQUIRE(aOwner.Snapshot().Phase == Phase::Bound);
}

Capability RequireCapability(
    const Owner& acOwner,
    const Result& acPlan,
    EffectKind aKind,
    Authority aAuthority,
    uint64_t aNonce,
    const Identity* apReserveIdentity = nullptr)
{
    const auto authorized =
        Policy::Authorize(acOwner.Snapshot(), acPlan);
    REQUIRE(authorized.Status == CapabilityStatus::Authorized);
    REQUIRE(authorized.IsAuthorized());
    REQUIRE(authorized.Capability.EffectKind == aKind);
    REQUIRE(authorized.Capability.Authority == aAuthority);
    REQUIRE(authorized.Capability.PinnedModuleRequired == 1u);
    REQUIRE(authorized.Capability.AttemptNonce == aNonce);
    REQUIRE(authorized.Capability.EffectSequence ==
        acPlan.Effect.Sequence);
    REQUIRE(authorized.Capability.EffectSequence != 0u);

    if (aKind == EffectKind::Reserve)
    {
        REQUIRE(apReserveIdentity != nullptr);
        REQUIRE_FALSE(authorized.Capability.AuthorizesExactCall(
            aKind, aNonce));
        REQUIRE(authorized.Capability.AuthorizesExactReserve(
            *apReserveIdentity));
    }
    else
    {
        REQUIRE(apReserveIdentity == nullptr);
        REQUIRE(authorized.Capability.AuthorizesExactCall(aKind, aNonce));
    }

    if (aAuthority == Authority::CurrentGeneration)
    {
        REQUIRE(authorized.Capability.RequiresCurrentGenerationLease());
        REQUIRE(authorized.Capability.AuthorizesRuntimeGeneration(
            authorized.Capability.BoundGeneration));
    }
    else
    {
        REQUIRE(authorized.Capability.IsRequestDrain());
        REQUIRE_FALSE(authorized.Capability.RequiresCurrentGenerationLease());
        REQUIRE_FALSE(authorized.Capability.AuthorizesRuntimeGeneration(
            authorized.Capability.BoundGeneration));
        REQUIRE_FALSE(authorized.Capability.AuthorizesRuntimeGeneration(
            authorized.Capability.ObservedGeneration));
    }

    return authorized.Capability;
}

void ReserveOwner(
    Owner& aOwner,
    uint64_t aNonce,
    const Identity& acIdentity = MakeIdentity())
{
    const auto plan = aOwner.Plan(ReserveCommand(acIdentity));
    const auto capability = RequireCapability(
        aOwner,
        plan,
        EffectKind::Reserve,
        Authority::CurrentGeneration,
        0u,
        &acIdentity);
    REQUIRE(capability.BoundGeneration ==
        capability.ObservedGeneration);

    const auto applied =
        Apply(aOwner, ReserveSuccess(aNonce, acIdentity));
    REQUIRE(applied.Code == ResultCode::Reserved);
    REQUIRE(applied.ReleaseCapability == 0u);
    REQUIRE(aOwner.Snapshot().ActiveAttemptNonce == aNonce);
}

void RequireNoForeignAuthority(
    const Owner& acOwner,
    const Result& acPlan)
{
    const auto authorization =
        Policy::Authorize(acOwner.Snapshot(), acPlan);
    REQUIRE_FALSE(authorization.IsAuthorized());
    REQUIRE(authorization.HasCapability == 0u);
}

void EnterCompletion(
    Owner& aOwner,
    uint64_t aNonce,
    uint64_t aSequence,
    const Identity& acIdentity)
{
    const auto poll =
        aOwner.Plan(NonceCommand(CommandKind::Poll, aNonce));
    RequireCapability(
        aOwner,
        poll,
        EffectKind::Poll,
        Authority::RequestDrain,
        aNonce);
    const auto completed = Apply(aOwner, 
        CompletionSuccess(aNonce, aSequence, acIdentity));
    REQUIRE(completed.Code == ResultCode::CompletionAvailable);
    REQUIRE(completed.ReleaseCapability == 0u);
    REQUIRE(aOwner.Snapshot().RequestPhase ==
        RequestPhase::CompletionCached);
}
} // namespace

TEST_CASE(
    "Native load bridge call capability is fixed portable POD authority",
    "[quest.party-state][native-load-call-capability][abi]")
{
    STATIC_REQUIRE(sizeof(PartyQuestNativeLoadBridgeCallAuthority) == 1u);
    STATIC_REQUIRE(
        sizeof(PartyQuestNativeLoadBridgeCallCapabilityStatus) == 1u);
    STATIC_REQUIRE(sizeof(Capability) == 312u);
    STATIC_REQUIRE(alignof(Capability) == 8u);
    STATIC_REQUIRE(std::is_standard_layout_v<Capability>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Capability>);
    STATIC_REQUIRE(noexcept(
        Policy::Authorize(
            std::declval<const PartyQuestNativeLoadBridgeOwnerSnapshot&>(),
            std::declval<const Result&>())));

    Capability empty{};
    REQUIRE_FALSE(empty.IsAuthorized());
    REQUIRE_FALSE(empty.AuthorizesRuntimeGeneration(1u));
    REQUIRE_FALSE(empty.AuthorizesExactCall(EffectKind::Cancel, 1u));

    Capability forged{};
    forged.EffectKind = static_cast<EffectKind>(0xFFu);
    forged.Authority = Authority::RequestDrain;
    forged.PinnedModuleRequired = 1u;
    forged.BoundGeneration = 1u;
    forged.ObservedGeneration = 2u;
    forged.RuntimeFingerprint = kFingerprint;
    forged.AttemptNonce = 1u;
    forged.EffectSequence = 1u;
    REQUIRE_FALSE(forged.IsAuthorized());

    Capability forgedReserve{};
    forgedReserve.EffectKind = EffectKind::Reserve;
    forgedReserve.Authority = Authority::CurrentGeneration;
    forgedReserve.PinnedModuleRequired = 1u;
    forgedReserve.BoundGeneration = 1u;
    forgedReserve.ObservedGeneration = 1u;
    forgedReserve.RuntimeFingerprint = kFingerprint;
    forgedReserve.EffectSequence = 1u;
    REQUIRE_FALSE(forgedReserve.IsAuthorized());
}

TEST_CASE(
    "Reserve capability is bound to the exact planned identity",
    "[quest.party-state][native-load-call-capability][reserve-identity]")
{
    Owner owner;
    BindOwner(owner, 3u);

    auto plannedIdentity = MakeIdentity(21u);
    plannedIdentity.Bytes[100] = 0xA5u; // Tail is outside semantic identity.

    const auto plan = owner.Plan(ReserveCommand(plannedIdentity));
    const auto authorized = Policy::Authorize(owner.Snapshot(), plan);
    REQUIRE(authorized.IsAuthorized());
    REQUIRE(authorized.Capability.EffectKind == EffectKind::Reserve);
    REQUIRE(authorized.Capability.AttemptNonce == 0u);
    REQUIRE(authorized.Capability.ReserveIdentity.Length ==
        plannedIdentity.Length);
    REQUIRE(authorized.Capability.ReserveIdentity.Bytes[100] == 0u);

    REQUIRE_FALSE(authorized.Capability.AuthorizesExactCall(
        EffectKind::Reserve, 0u));
    REQUIRE(authorized.Capability.AuthorizesExactReserve(plannedIdentity));

    auto differentFirst = plannedIdentity;
    ++differentFirst.Bytes[0];
    REQUIRE_FALSE(
        authorized.Capability.AuthorizesExactReserve(differentFirst));

    auto differentMiddle = plannedIdentity;
    ++differentMiddle.Bytes[2];
    REQUIRE_FALSE(
        authorized.Capability.AuthorizesExactReserve(differentMiddle));

    auto differentLast = plannedIdentity;
    ++differentLast.Bytes[3];
    REQUIRE_FALSE(
        authorized.Capability.AuthorizesExactReserve(differentLast));

    auto differentLength = plannedIdentity;
    differentLength.Length = 3u;
    REQUIRE_FALSE(
        authorized.Capability.AuthorizesExactReserve(differentLength));

    auto malformed = plannedIdentity;
    malformed.Reserved0 = 1u;
    REQUIRE_FALSE(
        authorized.Capability.AuthorizesExactReserve(malformed));

    auto tailOnlyDifference = plannedIdentity;
    tailOnlyDifference.Bytes[100] = 0x5Au;
    REQUIRE(
        authorized.Capability.AuthorizesExactReserve(tailOnlyDifference));

    REQUIRE_FALSE(authorized.Capability.AuthorizesExactReserve(Identity{}));
}

TEST_CASE(
    "Call capability rejects a stale Plan instance even when Reserve identity repeats",
    "[quest.party-state][native-load-call-capability][plan-sequence][aba]")
{
    Owner owner;
    const auto identity = MakeIdentity(27u);
    BindOwner(owner, 4u);

    const auto firstPlan = owner.Plan(ReserveCommand(identity));
    const auto firstCapability = RequireCapability(
        owner,
        firstPlan,
        EffectKind::Reserve,
        Authority::CurrentGeneration,
        0u,
        &identity);
    const uint64_t firstSequence = firstCapability.EffectSequence;

    REQUIRE(Apply(owner, 
        ReserveSuccess(1u, identity)).Code == ResultCode::Reserved);

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 1u));
    RequireCapability(
        owner,
        cancel,
        EffectKind::Cancel,
        Authority::CurrentGeneration,
        1u);
    REQUIRE(Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled)).Code ==
        ResultCode::Cancelled);

    const auto secondPlan = owner.Plan(ReserveCommand(identity));
    REQUIRE(secondPlan.Effect.Sequence != firstSequence);
    REQUIRE(owner.Snapshot().PendingEffectSequence ==
        secondPlan.Effect.Sequence);

    const auto stale =
        Policy::Authorize(owner.Snapshot(), firstPlan);
    REQUIRE(stale.Status == CapabilityStatus::StateMismatch);
    REQUIRE_FALSE(stale.IsAuthorized());
    REQUIRE(stale.HasCapability == 0u);

    const auto current = Policy::Authorize(owner.Snapshot(), secondPlan);
    REQUIRE(current.IsAuthorized());
    REQUIRE(current.Capability.EffectSequence ==
        secondPlan.Effect.Sequence);
    REQUIRE(current.Capability.AuthorizesExactReserve(identity));
}

TEST_CASE(
    "Generation fence completion cannot resurrect old generation authority for active native load drain",
    "[quest.party-state][native-load-call-capability][generation-fence]")
{
    PartyQuestRuntimeGenerationFence fence;
    Owner owner;

    const uint64_t generation = fence.GetGeneration();
    REQUIRE(generation != 0u);
    BindOwner(owner, generation);
    ReserveOwner(owner, 40u);

    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());
    REQUIRE(transition.Generation != generation);
    REQUIRE_FALSE(fence.TryAcquire(generation).has_value());
    REQUIRE_FALSE(fence.TryAcquire(transition.Generation).has_value());

    const auto ownerTransition =
        owner.Plan(GenerationCommand(transition.Generation));
    REQUIRE(ownerTransition.Code == ResultCode::DrainPending);
    REQUIRE(ownerTransition.ReleaseCapability == 0u);

    REQUIRE(fence.CompleteLifecycleTransition(transition));
    REQUIRE_FALSE(fence.TryAcquire(generation).has_value());

    auto newGenerationLease =
        fence.TryAcquire(transition.Generation);
    REQUIRE(newGenerationLease.has_value());
    REQUIRE(newGenerationLease->IsValid());
    newGenerationLease.reset();

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 40u));
    const auto capability = RequireCapability(
        owner,
        cancel,
        EffectKind::Cancel,
        Authority::RequestDrain,
        40u);

    REQUIRE(capability.BoundGeneration == generation);
    REQUIRE(capability.ObservedGeneration == transition.Generation);
    REQUIRE_FALSE(capability.AuthorizesRuntimeGeneration(generation));
    REQUIRE_FALSE(capability.AuthorizesRuntimeGeneration(
        transition.Generation));
}

TEST_CASE(
    "Active native load request crosses generation transition only through exact drain authority",
    "[quest.party-state][native-load-call-capability][generation][drain]")
{
    Owner owner;
    BindOwner(owner, 10u);
    ReserveOwner(owner, 41u);

    const auto transitioned = owner.Plan(GenerationCommand(11u));
    REQUIRE(transitioned.Code == ResultCode::DrainPending);
    REQUIRE(transitioned.ReleaseCapability == 0u);

    const auto snapshot = owner.Snapshot();
    REQUIRE(snapshot.Phase == Phase::DrainOnly);
    REQUIRE(snapshot.BoundGeneration == 10u);
    REQUIRE(snapshot.CurrentGeneration == 11u);
    REQUIRE(snapshot.CapabilityRetained == 1u);
    REQUIRE(snapshot.ActiveAttemptNonce == 41u);

    const auto reserve = owner.Plan(ReserveCommand());
    REQUIRE(reserve.Code == ResultCode::AdmissionClosed);
    RequireNoForeignAuthority(owner, reserve);

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 41u));
    const auto capability = RequireCapability(
        owner,
        cancel,
        EffectKind::Cancel,
        Authority::RequestDrain,
        41u);
    REQUIRE(capability.BoundGeneration == 10u);
    REQUIRE(capability.ObservedGeneration == 11u);
    REQUIRE_FALSE(capability.AuthorizesExactCall(
        EffectKind::Cancel, 40u));
    REQUIRE_FALSE(capability.AuthorizesExactCall(
        EffectKind::Reserve, 0u));
}

TEST_CASE(
    "Cancel uses generation authority before transition and request drain authority after transition",
    "[quest.party-state][native-load-call-capability][cancel]")
{
    Owner owner;
    BindOwner(owner, 20u);
    ReserveOwner(owner, 50u);

    const auto before =
        owner.Plan(NonceCommand(CommandKind::Cancel, 50u));
    const auto beforeCapability = RequireCapability(
        owner,
        before,
        EffectKind::Cancel,
        Authority::CurrentGeneration,
        50u);
    REQUIRE(beforeCapability.AuthorizesRuntimeGeneration(20u));

    const auto stillActive = Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::InvalidState));
    REQUIRE(stillActive.Code == ResultCode::Pending);
    REQUIRE(stillActive.ReleaseCapability == 0u);

    REQUIRE(owner.Plan(GenerationCommand(21u)).Code ==
        ResultCode::DrainPending);

    const auto after =
        owner.Plan(NonceCommand(CommandKind::Cancel, 50u));
    const auto afterCapability = RequireCapability(
        owner,
        after,
        EffectKind::Cancel,
        Authority::RequestDrain,
        50u);
    REQUIRE(afterCapability.BoundGeneration == 20u);
    REQUIRE(afterCapability.ObservedGeneration == 21u);

    const auto cancelled = Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
    REQUIRE(cancelled.Code == ResultCode::Cancelled);
    REQUIRE(cancelled.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
    REQUIRE(owner.Snapshot().CurrentGeneration == 21u);
}

TEST_CASE(
    "Completion and retirement remain exact request drain work after generation transition",
    "[quest.party-state][native-load-call-capability][poll][retire]")
{
    Owner owner;
    const auto identity = MakeIdentity(7u);
    BindOwner(owner, 30u);
    ReserveOwner(owner, 60u, identity);

    REQUIRE(owner.Plan(GenerationCommand(31u)).Code ==
        ResultCode::DrainPending);

    EnterCompletion(owner, 60u, 1u, identity);

    const auto cachedPoll =
        owner.Plan(NonceCommand(CommandKind::Poll, 60u));
    REQUIRE(cachedPoll.Code == ResultCode::CompletionAvailable);
    REQUIRE(cachedPoll.HasCompletion == 1u);
    REQUIRE(cachedPoll.HasEffect == 0u);
    RequireNoForeignAuthority(owner, cachedPoll);

    const auto retire =
        owner.Plan(NonceCommand(CommandKind::Retire, 60u));
    const auto capability = RequireCapability(
        owner,
        retire,
        EffectKind::Retire,
        Authority::RequestDrain,
        60u);
    REQUIRE(capability.BoundGeneration == 30u);
    REQUIRE(capability.ObservedGeneration == 31u);

    const auto retired = Apply(owner, 
        ReturnedOutcome(EffectKind::Retire, Status::Retired));
    REQUIRE(retired.Code == ResultCode::Retired);
    REQUIRE(retired.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::Unbound);

    const auto duplicate =
        owner.Plan(NonceCommand(CommandKind::Retire, 60u));
    REQUIRE(duplicate.Code == ResultCode::Duplicate);
    RequireNoForeignAuthority(owner, duplicate);
}

TEST_CASE(
    "Stale and ABA nonces never gain drain capability across rebind",
    "[quest.party-state][native-load-call-capability][aba]")
{
    Owner owner;
    BindOwner(owner, 40u);
    ReserveOwner(owner, 70u);
    REQUIRE(owner.Plan(GenerationCommand(41u)).Code ==
        ResultCode::DrainPending);

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 70u));
    RequireCapability(
        owner,
        cancel,
        EffectKind::Cancel,
        Authority::RequestDrain,
        70u);
    REQUIRE(Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled)).
        ReleaseCapability == 1u);

    BindOwner(owner, 41u);
    ReserveOwner(owner, 71u, MakeIdentity(9u));

    for (const auto kind : {
             CommandKind::Cancel,
             CommandKind::Poll,
             CommandKind::Retire})
    {
        const auto stale = owner.Plan(NonceCommand(kind, 70u));
        REQUIRE(stale.Code == ResultCode::StaleNonce);
        RequireNoForeignAuthority(owner, stale);

        const auto mismatch = owner.Plan(NonceCommand(kind, 72u));
        REQUIRE(mismatch.Code == ResultCode::NonceMismatch);
        RequireNoForeignAuthority(owner, mismatch);
    }

    REQUIRE(owner.Snapshot().ActiveAttemptNonce == 71u);
    REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
}

TEST_CASE(
    "Shutdown drain never turns retained request authority into runtime generation authority",
    "[quest.party-state][native-load-call-capability][shutdown]")
{
    Owner owner;
    const auto identity = MakeIdentity(11u);
    BindOwner(owner, 50u);
    ReserveOwner(owner, 80u, identity);

    const auto shutdown = owner.Plan(ShutdownCommand());
    REQUIRE(owner.Snapshot().Phase == Phase::ShutdownDrain);
    const auto cancelCapability = RequireCapability(
        owner,
        shutdown,
        EffectKind::Cancel,
        Authority::RequestDrain,
        80u);
    REQUIRE(cancelCapability.BoundGeneration == 50u);
    REQUIRE(cancelCapability.ObservedGeneration == 50u);
    REQUIRE_FALSE(cancelCapability.AuthorizesRuntimeGeneration(50u));

    const auto claimed = Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::InvalidState));
    REQUIRE(claimed.Code == ResultCode::Pending);
    REQUIRE(claimed.ReleaseCapability == 0u);

    EnterCompletion(owner, 80u, 2u, identity);

    const auto shutdownAgain = owner.Plan(ShutdownCommand());
    const auto retireCapability = RequireCapability(
        owner,
        shutdownAgain,
        EffectKind::Retire,
        Authority::RequestDrain,
        80u);
    REQUIRE_FALSE(retireCapability.AuthorizesRuntimeGeneration(50u));

    const auto retired = Apply(owner, 
        ReturnedOutcome(EffectKind::Retire, Status::Retired));
    REQUIRE(retired.Code == ResultCode::Retired);
    REQUIRE(retired.ReleaseCapability == 1u);
    REQUIRE(owner.Snapshot().Phase == Phase::ShutdownComplete);

    const auto shutdownComplete = owner.Plan(ShutdownCommand());
    REQUIRE(shutdownComplete.Code == ResultCode::ShutdownComplete);
    REQUIRE(shutdownComplete.ReleaseCapability == 0u);
    RequireNoForeignAuthority(owner, shutdownComplete);
}

TEST_CASE(
    "Unknown malformed and impossible drain outcomes poison without releasing capability",
    "[quest.party-state][native-load-call-capability][poison]")
{
    SECTION("unknown foreign outcome")
    {
        Owner owner;
        BindOwner(owner, 60u);
        ReserveOwner(owner, 90u);
        REQUIRE(owner.Plan(GenerationCommand(61u)).Code ==
            ResultCode::DrainPending);

        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 90u));
        RequireCapability(
            owner,
            poll,
            EffectKind::Poll,
            Authority::RequestDrain,
            90u);

        const auto poisoned =
            Apply(owner, UnknownOutcome(EffectKind::Poll));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(poisoned.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().Phase ==
            Phase::PoisonedUnsafeToUnload);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    }

    SECTION("nonce mismatch in completion")
    {
        Owner owner;
        const auto identity = MakeIdentity(13u);
        BindOwner(owner, 62u);
        ReserveOwner(owner, 91u, identity);
        REQUIRE(owner.Plan(GenerationCommand(63u)).Code ==
            ResultCode::DrainPending);

        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 91u));
        RequireCapability(
            owner,
            poll,
            EffectKind::Poll,
            Authority::RequestDrain,
            91u);

        const auto poisoned = Apply(owner, 
            CompletionSuccess(92u, 1u, identity));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(poisoned.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    }

    SECTION("malformed completion")
    {
        Owner owner;
        const auto identity = MakeIdentity(15u);
        BindOwner(owner, 64u);
        ReserveOwner(owner, 92u, identity);
        REQUIRE(owner.Plan(GenerationCommand(65u)).Code ==
            ResultCode::DrainPending);

        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 92u));
        RequireCapability(
            owner,
            poll,
            EffectKind::Poll,
            Authority::RequestDrain,
            92u);

        auto malformed = CompletionSuccess(92u, 1u, identity);
        malformed.Completion.Reserved[0] = 1u;
        const auto poisoned = Apply(owner, malformed);
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(poisoned.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    }

    SECTION("impossible status")
    {
        Owner owner;
        BindOwner(owner, 66u);
        ReserveOwner(owner, 93u);
        REQUIRE(owner.Plan(GenerationCommand(67u)).Code ==
            ResultCode::DrainPending);

        const auto cancel =
            owner.Plan(NonceCommand(CommandKind::Cancel, 93u));
        RequireCapability(
            owner,
            cancel,
            EffectKind::Cancel,
            Authority::RequestDrain,
            93u);

        auto impossible =
            ReturnedOutcome(EffectKind::Cancel, Status::Cancelled);
        impossible.RawStatus = std::numeric_limits<uint32_t>::max();
        const auto poisoned =
            Apply(owner, impossible);
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(poisoned.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);
    }
}

TEST_CASE(
    "Drain capability release is emitted exactly once by reducer terminal proof",
    "[quest.party-state][native-load-call-capability][release]")
{
    Owner owner;
    BindOwner(owner, 70u);
    ReserveOwner(owner, 100u);
    REQUIRE(owner.Plan(GenerationCommand(71u)).Code ==
        ResultCode::DrainPending);

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 100u));
    RequireCapability(
        owner,
        cancel,
        EffectKind::Cancel,
        Authority::RequestDrain,
        100u);

    const auto released = Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
    REQUIRE(released.ReleaseCapability == 1u);

    const auto duplicate =
        owner.Plan(NonceCommand(CommandKind::Cancel, 100u));
    REQUIRE(duplicate.Code == ResultCode::Duplicate);
    REQUIRE(duplicate.ReleaseCapability == 0u);
    RequireNoForeignAuthority(owner, duplicate);

    const auto spurious = Apply(owner, 
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
    REQUIRE(spurious.Code == ResultCode::InvalidState);
    REQUIRE(spurious.ReleaseCapability == 0u);
    REQUIRE(owner.Snapshot().CapabilityRetained == 0u);
}

TEST_CASE(
    "Deterministic drain traces preserve exact nonce pin and no-generation-authority properties",
    "[quest.party-state][native-load-call-capability][trace]")
{
    for (uint64_t index = 0u; index < 128u; ++index)
    {
        Owner owner;
        const uint64_t generation = 1000u + index * 2u;
        const uint64_t nonce = 2000u + index;
        const auto identity =
            MakeIdentity(static_cast<uint8_t>(1u + index % 200u));

        BindOwner(owner, generation);
        ReserveOwner(owner, nonce, identity);
        REQUIRE(owner.Plan(GenerationCommand(generation + 1u)).Code ==
            ResultCode::DrainPending);

        const auto reserve = owner.Plan(ReserveCommand(identity));
        REQUIRE(reserve.Code == ResultCode::AdmissionClosed);
        RequireNoForeignAuthority(owner, reserve);

        if ((index & 1u) == 0u)
        {
            const auto cancel =
                owner.Plan(NonceCommand(CommandKind::Cancel, nonce));
            const auto capability = RequireCapability(
                owner,
                cancel,
                EffectKind::Cancel,
                Authority::RequestDrain,
                nonce);
            REQUIRE(capability.BoundGeneration == generation);
            REQUIRE(capability.ObservedGeneration == generation + 1u);
            REQUIRE_FALSE(capability.AuthorizesExactCall(
                EffectKind::Cancel, nonce + 1u));

            const auto released = Apply(owner, 
                ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
            REQUIRE(released.ReleaseCapability == 1u);
        }
        else
        {
            const auto cancel =
                owner.Plan(NonceCommand(CommandKind::Cancel, nonce));
            RequireCapability(
                owner,
                cancel,
                EffectKind::Cancel,
                Authority::RequestDrain,
                nonce);
            REQUIRE(Apply(owner, 
                ReturnedOutcome(EffectKind::Cancel, Status::InvalidState)).
                ReleaseCapability == 0u);

            const uint64_t pendingPolls = index % 4u;
            for (uint64_t pollIndex = 0u;
                 pollIndex < pendingPolls;
                 ++pollIndex)
            {
                const auto poll =
                    owner.Plan(NonceCommand(CommandKind::Poll, nonce));
                RequireCapability(
                    owner,
                    poll,
                    EffectKind::Poll,
                    Authority::RequestDrain,
                    nonce);
                const auto pending = Apply(owner, 
                    ReturnedOutcome(EffectKind::Poll, Status::Pending));
                REQUIRE(pending.Code == ResultCode::Pending);
                REQUIRE(pending.ReleaseCapability == 0u);
            }

            EnterCompletion(
                owner,
                nonce,
                1u + index,
                identity);

            const auto retire =
                owner.Plan(NonceCommand(CommandKind::Retire, nonce));
            RequireCapability(
                owner,
                retire,
                EffectKind::Retire,
                Authority::RequestDrain,
                nonce);
            const auto released = Apply(owner, 
                ReturnedOutcome(EffectKind::Retire, Status::Retired));
            REQUIRE(released.ReleaseCapability == 1u);
        }

        const auto snapshot = owner.Snapshot();
        REQUIRE(snapshot.Phase == Phase::Unbound);
        REQUIRE(snapshot.CurrentGeneration == generation + 1u);
        REQUIRE(snapshot.CapabilityRetained == 0u);
        REQUIRE(snapshot.ActiveAttemptNonce == 0u);

        const auto duplicate =
            owner.Plan(NonceCommand(CommandKind::Poll, nonce));
        REQUIRE(duplicate.Code == ResultCode::Duplicate);
        RequireNoForeignAuthority(owner, duplicate);
    }
}
