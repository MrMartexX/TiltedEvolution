#include <Structs/Skyrim/PartyQuestNativeLoadBridgeOwnerState.h>

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
using Owner = PartyQuestNativeLoadBridgeOwnerState;
using Phase = PartyQuestNativeLoadBridgeOwnerPhase;
using RequestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase;
using CommandKind = PartyQuestNativeLoadBridgeOwnerCommandKind;
using BindDisposition = PartyQuestNativeLoadBridgeOwnerBindDisposition;
using EffectKind = PartyQuestNativeLoadBridgeOwnerEffectKind;
using ForeignDisposition = PartyQuestNativeLoadBridgeOwnerForeignDisposition;
using NonceClass = PartyQuestNativeLoadBridgeOwnerNonceClass;
using ResultCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using Command = PartyQuestNativeLoadBridgeOwnerCommand;
using Effect = PartyQuestNativeLoadBridgeOwnerEffect;
using Outcome = PartyQuestNativeLoadBridgeOwnerForeignOutcome;
using Result = PartyQuestNativeLoadBridgeOwnerResult;
using Snapshot = PartyQuestNativeLoadBridgeOwnerSnapshot;
using Identity = PartyQuestNativeLoadBridgeIdentityV1;
using Reservation = PartyQuestNativeLoadBridgeReservationV1;
using Completion = PartyQuestNativeLoadBridgeCompletionV1;
using Status = PartyQuestNativeLoadBridgeStatus;

constexpr uint64_t kFingerprint = 0x123456789ABCDEF0ull;

constexpr std::array<Status, 18> kKnownStatuses{
    Status::Reserved,
    Status::Cancelled,
    Status::Pending,
    Status::CompletionAvailable,
    Status::Retired,
    Status::Duplicate,
    Status::InvalidArgument,
    Status::UnsupportedAbi,
    Status::InvalidStructSize,
    Status::InvalidIdentity,
    Status::IdentityMismatch,
    Status::NonceMismatch,
    Status::StaleNonce,
    Status::InvalidState,
    Status::CounterExhausted,
    Status::Poisoned,
    Status::BridgeUnavailable,
    Status::InternalFailure};

Identity MakeIdentity(
    uint16_t aLength = 4u,
    uint8_t aBase = 0x41u) noexcept
{
    Identity identity{};
    identity.Length = aLength;
    for (uint16_t index = 0u;
         index < aLength &&
         index < PartyQuestNativeLoadIdentity::kCapacity;
         ++index)
    {
        identity.Bytes[index] =
            static_cast<uint8_t>(aBase + (index % 17u));
    }
    return identity;
}

bool IdentityEqual(
    const Identity& acLeft,
    const Identity& acRight) noexcept
{
    return acLeft.Length == acRight.Length &&
        acLeft.Length != 0u &&
        std::memcmp(
            acLeft.Bytes,
            acRight.Bytes,
            acLeft.Length) == 0;
}

bool IdentityStorageZero(const Identity& acIdentity) noexcept
{
    if (acIdentity.Length != 0u || acIdentity.Reserved0 != 0u)
        return false;
    for (const auto value : acIdentity.Bytes)
    {
        if (value != 0u)
            return false;
    }
    return true;
}

bool CompletionStorageZero(const Completion& acCompletion) noexcept
{
    if (acCompletion.AbiVersion != 0u ||
        acCompletion.StructSize != 0u ||
        acCompletion.AttemptNonce != 0u ||
        acCompletion.EventSequence != 0u ||
        !IdentityStorageZero(acCompletion.Identity) ||
        acCompletion.Result != 0u)
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

Command BindCommand(
    uint64_t aGeneration = 1u,
    uint64_t aFingerprint = kFingerprint) noexcept
{
    Command command{};
    command.Kind = CommandKind::Bind;
    command.BindDisposition = BindDisposition::Authenticated;
    command.RuntimeGeneration = aGeneration;
    command.RuntimeFingerprint = aFingerprint;
    return command;
}

Command RejectedBindCommand() noexcept
{
    Command command{};
    command.Kind = CommandKind::Bind;
    command.BindDisposition = BindDisposition::Rejected;
    return command;
}

Command ReserveCommand(const Identity& acIdentity = MakeIdentity()) noexcept
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
    reservation.StructSize = sizeof(Reservation);
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
    completion.StructSize = sizeof(Completion);
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

Outcome UnknownOutcome(EffectKind aKind) noexcept
{
    Outcome outcome{};
    outcome.EffectKind = aKind;
    outcome.Disposition = ForeignDisposition::Unknown;
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

void RequireEffect(
    const Result& acResult,
    EffectKind aKind,
    uint64_t aNonce = 0u)
{
    REQUIRE(acResult.Code == ResultCode::EffectRequired);
    REQUIRE(acResult.HasEffect == 1u);
    REQUIRE(acResult.ReleaseCapability == 0u);
    REQUIRE(acResult.HasCompletion == 0u);
    REQUIRE(acResult.Effect.Kind == aKind);
    REQUIRE(acResult.Effect.Sequence != 0u);
    REQUIRE(acResult.Effect.AttemptNonce == aNonce);
}

void RequireSnapshotInvariant(const Snapshot& acSnapshot)
{
    REQUIRE(acSnapshot.CapabilityRetained <= 1u);
    REQUIRE(acSnapshot.HasCachedCompletion <= 1u);

    if (acSnapshot.RequestPhase == RequestPhase::None)
    {
        REQUIRE(acSnapshot.ActiveAttemptNonce == 0u);
        REQUIRE(acSnapshot.HasCachedCompletion == 0u);
        REQUIRE(IdentityStorageZero(acSnapshot.ActiveIdentity));
        REQUIRE(CompletionStorageZero(acSnapshot.CachedCompletion));
    }
    else
    {
        REQUIRE(acSnapshot.ActiveAttemptNonce != 0u);
        REQUIRE(acSnapshot.ActiveAttemptNonce ==
            acSnapshot.LastAttemptNonce);
        REQUIRE(PartyQuestNativeLoadBridgePolicy::IsValidIdentity(
            acSnapshot.ActiveIdentity));
    }

    if (acSnapshot.RequestPhase == RequestPhase::Active)
    {
        REQUIRE(acSnapshot.HasCachedCompletion == 0u);
        REQUIRE(CompletionStorageZero(acSnapshot.CachedCompletion));
    }

    if (acSnapshot.RequestPhase == RequestPhase::CompletionCached)
    {
        REQUIRE(acSnapshot.HasCachedCompletion == 1u);
        REQUIRE(PartyQuestNativeLoadBridgePolicy::IsValidCompletion(
            acSnapshot.CachedCompletion));
        REQUIRE(acSnapshot.CachedCompletion.AttemptNonce ==
            acSnapshot.ActiveAttemptNonce);
        REQUIRE(acSnapshot.CachedCompletion.EventSequence ==
            acSnapshot.LastCompletionSequence);
        REQUIRE(IdentityEqual(
            acSnapshot.CachedCompletion.Identity,
            acSnapshot.ActiveIdentity));
    }

    switch (acSnapshot.Phase)
    {
    case Phase::Unbound:
        REQUIRE(acSnapshot.CapabilityRetained == 0u);
        REQUIRE(acSnapshot.BoundGeneration == 0u);
        REQUIRE(acSnapshot.RuntimeFingerprint == 0u);
        REQUIRE(acSnapshot.RequestPhase == RequestPhase::None);
        break;

    case Phase::Bound:
        REQUIRE(acSnapshot.CapabilityRetained == 1u);
        REQUIRE(acSnapshot.BoundGeneration != 0u);
        REQUIRE(acSnapshot.BoundGeneration ==
            acSnapshot.CurrentGeneration);
        REQUIRE(acSnapshot.RuntimeFingerprint != 0u);
        break;

    case Phase::DrainOnly:
    case Phase::ShutdownDrain:
        REQUIRE(acSnapshot.CapabilityRetained == 1u);
        REQUIRE(acSnapshot.BoundGeneration != 0u);
        REQUIRE(acSnapshot.RuntimeFingerprint != 0u);
        REQUIRE(acSnapshot.RequestPhase != RequestPhase::None);
        REQUIRE(acSnapshot.ActiveAttemptNonce != 0u);
        break;

    case Phase::ShutdownComplete:
        REQUIRE(acSnapshot.CapabilityRetained == 0u);
        REQUIRE(acSnapshot.BoundGeneration == 0u);
        REQUIRE(acSnapshot.RuntimeFingerprint == 0u);
        REQUIRE(acSnapshot.RequestPhase == RequestPhase::None);
        break;

    case Phase::PoisonedUnsafeToUnload:
        REQUIRE(acSnapshot.CapabilityRetained == 1u);
        break;
    }

    if (acSnapshot.PendingEffect != EffectKind::None)
    {
        REQUIRE(acSnapshot.CapabilityRetained == 1u);
        REQUIRE(acSnapshot.PendingEffectSequence != 0u);
    }
    else
    {
        REQUIRE(acSnapshot.PendingEffectSequence == 0u);
    }
}

void RequireReleaseSafe(
    const Result& acResult,
    const Snapshot& acSnapshot)
{
    if (acResult.ReleaseCapability != 0u)
    {
        REQUIRE(acSnapshot.CapabilityRetained == 0u);
        REQUIRE(acSnapshot.RequestPhase == RequestPhase::None);
        REQUIRE(acSnapshot.ActiveAttemptNonce == 0u);
        REQUIRE(acSnapshot.PendingEffect == EffectKind::None);
        REQUIRE((acSnapshot.Phase == Phase::Unbound ||
            acSnapshot.Phase == Phase::ShutdownComplete));
    }
}

void BindOwner(
    Owner& aOwner,
    uint64_t aGeneration = 1u,
    uint64_t aFingerprint = kFingerprint)
{
    const auto result =
        aOwner.Plan(BindCommand(aGeneration, aFingerprint));
    REQUIRE(result.Code == ResultCode::Bound);
    REQUIRE(result.HasEffect == 0u);
    RequireSnapshotInvariant(aOwner.Snapshot());
}

uint64_t ReserveOwner(
    Owner& aOwner,
    uint64_t aNonce,
    const Identity& acIdentity = MakeIdentity())
{
    const auto plan = aOwner.Plan(ReserveCommand(acIdentity));
    RequireEffect(plan, EffectKind::Reserve);
    REQUIRE(PartyQuestNativeLoadBridgePolicy::IsValidReserveRequest(
        plan.Effect.ReserveRequest));
    REQUIRE(IdentityEqual(
        plan.Effect.ReserveRequest.Identity,
        acIdentity));

    const auto applied =
        aOwner.ApplyForeignOutcome(
            ReserveSuccess(aNonce, acIdentity));
    REQUIRE(applied.Code == ResultCode::Reserved);
    REQUIRE(applied.HasEffect == 0u);
    REQUIRE(aOwner.Snapshot().ActiveAttemptNonce == aNonce);
    RequireSnapshotInvariant(aOwner.Snapshot());
    return aNonce;
}

Completion CacheCompletion(
    Owner& aOwner,
    uint64_t aNonce,
    uint64_t aSequence,
    const Identity& acIdentity = MakeIdentity(),
    uint8_t aResult = 1u)
{
    const auto plan =
        aOwner.Plan(NonceCommand(CommandKind::Poll, aNonce));
    RequireEffect(plan, EffectKind::Poll, aNonce);

    const auto applied = aOwner.ApplyForeignOutcome(
        CompletionSuccess(
            aNonce,
            aSequence,
            acIdentity,
            aResult));
    REQUIRE(applied.Code == ResultCode::CompletionAvailable);
    REQUIRE(applied.HasCompletion == 1u);
    REQUIRE(applied.Completion.Result == aResult);
    RequireSnapshotInvariant(aOwner.Snapshot());
    return applied.Completion;
}

void RetireOwner(Owner& aOwner, uint64_t aNonce)
{
    const auto plan =
        aOwner.Plan(NonceCommand(CommandKind::Retire, aNonce));
    RequireEffect(plan, EffectKind::Retire, aNonce);

    const auto outcome =
        ReturnedOutcome(EffectKind::Retire, Status::Retired);
    const auto applied = aOwner.ApplyForeignOutcome(outcome);
    REQUIRE(applied.Code == ResultCode::Retired);
    RequireSnapshotInvariant(aOwner.Snapshot());
    RequireReleaseSafe(applied, aOwner.Snapshot());
}

void ReachPhase(Owner& aOwner, Phase aPhase)
{
    switch (aPhase)
    {
    case Phase::Unbound:
        break;

    case Phase::Bound:
        BindOwner(aOwner);
        break;

    case Phase::DrainOnly:
    {
        BindOwner(aOwner);
        ReserveOwner(aOwner, 1u);
        const auto transition = aOwner.Plan(GenerationCommand(2u));
        REQUIRE(transition.Code == ResultCode::DrainPending);
        REQUIRE(aOwner.Snapshot().Phase == Phase::DrainOnly);
        break;
    }

    case Phase::ShutdownDrain:
    {
        BindOwner(aOwner);
        ReserveOwner(aOwner, 1u);
        const auto shutdown = aOwner.Plan(ShutdownCommand());
        RequireEffect(shutdown, EffectKind::Cancel, 1u);
        const auto pending = aOwner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Cancel, Status::InvalidState));
        REQUIRE(pending.Code == ResultCode::Pending);
        REQUIRE(aOwner.Snapshot().Phase == Phase::ShutdownDrain);
        break;
    }

    case Phase::ShutdownComplete:
    {
        const auto shutdown = aOwner.Plan(ShutdownCommand());
        REQUIRE(shutdown.Code == ResultCode::ShutdownComplete);
        break;
    }

    case Phase::PoisonedUnsafeToUnload:
    {
        BindOwner(aOwner);
        const auto reserve = aOwner.Plan(ReserveCommand());
        RequireEffect(reserve, EffectKind::Reserve);
        const auto poisoned =
            aOwner.ApplyForeignOutcome(
                UnknownOutcome(EffectKind::Reserve));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        break;
    }
    }

    REQUIRE(aOwner.Snapshot().Phase == aPhase);
    RequireSnapshotInvariant(aOwner.Snapshot());
}

Command CanonicalCommandFor(
    const Snapshot& acSnapshot,
    CommandKind aKind) noexcept
{
    switch (aKind)
    {
    case CommandKind::Bind:
    {
        const uint64_t generation =
            acSnapshot.CurrentGeneration != 0u ?
                acSnapshot.CurrentGeneration : 1u;
        const uint64_t fingerprint =
            acSnapshot.RuntimeFingerprint != 0u ?
                acSnapshot.RuntimeFingerprint : kFingerprint;
        return BindCommand(generation, fingerprint);
    }

    case CommandKind::Reserve:
        return ReserveCommand();

    case CommandKind::Cancel:
    case CommandKind::Poll:
    case CommandKind::Retire:
    {
        const uint64_t nonce =
            acSnapshot.ActiveAttemptNonce != 0u ?
                acSnapshot.ActiveAttemptNonce :
                (acSnapshot.LastAttemptNonce != 0u ?
                    acSnapshot.LastAttemptNonce : 1u);
        return NonceCommand(aKind, nonce);
    }

    case CommandKind::GenerationTransition:
        return GenerationCommand(
            acSnapshot.CurrentGeneration == 0u ?
                1u : acSnapshot.CurrentGeneration + 1u);

    case CommandKind::Shutdown:
        return ShutdownCommand();

    case CommandKind::Invalid:
        break;
    }

    return {};
}

ResultCode ExpectedMatrixCode(
    Phase aPhase,
    CommandKind aKind) noexcept
{
    if (aPhase == Phase::PoisonedUnsafeToUnload)
        return ResultCode::PoisonedUnsafeToUnload;

    switch (aPhase)
    {
    case Phase::Unbound:
        switch (aKind)
        {
        case CommandKind::Bind: return ResultCode::Bound;
        case CommandKind::Reserve: return ResultCode::NotBound;
        case CommandKind::Cancel:
        case CommandKind::Poll:
        case CommandKind::Retire: return ResultCode::NonceMismatch;
        case CommandKind::GenerationTransition: return ResultCode::Unbound;
        case CommandKind::Shutdown: return ResultCode::ShutdownComplete;
        case CommandKind::Invalid: return ResultCode::InvalidCommand;
        }
        break;

    case Phase::Bound:
        switch (aKind)
        {
        case CommandKind::Bind: return ResultCode::AlreadyBound;
        case CommandKind::Reserve: return ResultCode::EffectRequired;
        case CommandKind::Cancel:
        case CommandKind::Poll:
        case CommandKind::Retire: return ResultCode::NonceMismatch;
        case CommandKind::GenerationTransition: return ResultCode::Unbound;
        case CommandKind::Shutdown: return ResultCode::ShutdownComplete;
        case CommandKind::Invalid: return ResultCode::InvalidCommand;
        }
        break;

    case Phase::DrainOnly:
    case Phase::ShutdownDrain:
        switch (aKind)
        {
        case CommandKind::Bind:
        case CommandKind::Reserve:
            return ResultCode::AdmissionClosed;
        case CommandKind::Cancel:
        case CommandKind::Poll:
            return ResultCode::EffectRequired;
        case CommandKind::Retire:
            return ResultCode::InvalidState;
        case CommandKind::GenerationTransition:
            return ResultCode::DrainPending;
        case CommandKind::Shutdown:
            return ResultCode::EffectRequired;
        case CommandKind::Invalid:
            return ResultCode::InvalidCommand;
        }
        break;

    case Phase::ShutdownComplete:
        switch (aKind)
        {
        case CommandKind::Bind:
        case CommandKind::Reserve:
        case CommandKind::GenerationTransition:
            return ResultCode::AdmissionClosed;
        case CommandKind::Cancel:
        case CommandKind::Poll:
        case CommandKind::Retire:
            return ResultCode::NonceMismatch;
        case CommandKind::Shutdown:
            return ResultCode::ShutdownComplete;
        case CommandKind::Invalid:
            return ResultCode::InvalidCommand;
        }
        break;

    case Phase::PoisonedUnsafeToUnload:
        return ResultCode::PoisonedUnsafeToUnload;
    }

    return ResultCode::InvalidCommand;
}

void ReachPendingEffect(Owner& aOwner, EffectKind aKind)
{
    BindOwner(aOwner);
    if (aKind == EffectKind::Reserve)
    {
        const auto plan = aOwner.Plan(ReserveCommand());
        RequireEffect(plan, EffectKind::Reserve);
        return;
    }

    ReserveOwner(aOwner, 1u);

    if (aKind == EffectKind::Cancel)
    {
        const auto plan =
            aOwner.Plan(NonceCommand(CommandKind::Cancel, 1u));
        RequireEffect(plan, EffectKind::Cancel, 1u);
        return;
    }

    if (aKind == EffectKind::Poll)
    {
        const auto plan =
            aOwner.Plan(NonceCommand(CommandKind::Poll, 1u));
        RequireEffect(plan, EffectKind::Poll, 1u);
        return;
    }

    if (aKind == EffectKind::Retire)
    {
        CacheCompletion(aOwner, 1u, 1u);
        const auto plan =
            aOwner.Plan(NonceCommand(CommandKind::Retire, 1u));
        RequireEffect(plan, EffectKind::Retire, 1u);
    }
}

bool IsAllowedStatus(EffectKind aKind, Status aStatus) noexcept
{
    switch (aKind)
    {
    case EffectKind::Reserve:
        return aStatus == Status::Reserved;
    case EffectKind::Cancel:
        return aStatus == Status::Cancelled ||
            aStatus == Status::InvalidState;
    case EffectKind::Poll:
        return aStatus == Status::Pending ||
            aStatus == Status::CompletionAvailable;
    case EffectKind::Retire:
        return aStatus == Status::Retired;
    case EffectKind::None:
        return false;
    }
    return false;
}

Outcome ValidOutcomeFor(
    EffectKind aKind,
    Status aStatus,
    const Snapshot& acSnapshot) noexcept
{
    auto outcome = ReturnedOutcome(aKind, aStatus);

    if (aKind == EffectKind::Reserve &&
        aStatus == Status::Reserved)
    {
        outcome.HasReservation = 1u;
        outcome.Reservation =
            MakeReservation(1u, MakeIdentity());
    }
    else if (aKind == EffectKind::Poll &&
             aStatus == Status::CompletionAvailable)
    {
        outcome.HasCompletion = 1u;
        outcome.Completion = MakeCompletion(
            acSnapshot.ActiveAttemptNonce,
            1u,
            acSnapshot.ActiveIdentity,
            1u);
    }

    return outcome;
}
}

TEST_CASE("Native load bridge owner initial state is empty and consistent",
          "[quest.party-state][native-load-owner]")
{
    Owner owner;
    const auto snapshot = owner.Snapshot();

    REQUIRE(snapshot.Phase == Phase::Unbound);
    REQUIRE(snapshot.RequestPhase == RequestPhase::None);
    REQUIRE(snapshot.PendingEffect == EffectKind::None);
    REQUIRE(snapshot.CurrentGeneration == 0u);
    REQUIRE(snapshot.LastAttemptNonce == 0u);
    REQUIRE(snapshot.LastCompletionSequence == 0u);
    RequireSnapshotInvariant(snapshot);

    REQUIRE(owner.ClassifyNonce(0u) == NonceClass::Mismatch);
    REQUIRE(owner.ClassifyNonce(1u) == NonceClass::Mismatch);
}

TEST_CASE("Native load bridge owner Bind is authenticated exact and local",
          "[quest.party-state][native-load-owner][bind]")
{
    SECTION("authenticated")
    {
        Owner owner;
        const auto result = owner.Plan(BindCommand(7u, kFingerprint));
        REQUIRE(result.Code == ResultCode::Bound);
        REQUIRE(result.HasEffect == 0u);

        const auto snapshot = owner.Snapshot();
        REQUIRE(snapshot.Phase == Phase::Bound);
        REQUIRE(snapshot.CurrentGeneration == 7u);
        REQUIRE(snapshot.BoundGeneration == 7u);
        REQUIRE(snapshot.RuntimeFingerprint == kFingerprint);
        REQUIRE(snapshot.CapabilityRetained == 1u);
        RequireSnapshotInvariant(snapshot);
    }

    SECTION("rejected")
    {
        Owner owner;
        const auto before = owner.Snapshot();
        const auto result = owner.Plan(RejectedBindCommand());
        REQUIRE(result.Code == ResultCode::BindRejected);
        const auto after = owner.Snapshot();
        REQUIRE(std::memcmp(
            &before, &after, sizeof(before)) == 0);
    }

    SECTION("zero generation or fingerprint is malformed")
    {
        Owner owner;
        REQUIRE(owner.Plan(BindCommand(0u, kFingerprint)).Code ==
            ResultCode::InvalidCommand);
        REQUIRE(owner.Plan(BindCommand(1u, 0u)).Code ==
            ResultCode::InvalidCommand);
        RequireSnapshotInvariant(owner.Snapshot());
    }

    SECTION("same exact bind is idempotent")
    {
        Owner owner;
        BindOwner(owner, 3u, kFingerprint);
        const auto result =
            owner.Plan(BindCommand(3u, kFingerprint));
        REQUIRE(result.Code == ResultCode::AlreadyBound);
        RequireSnapshotInvariant(owner.Snapshot());
    }

    SECTION("stale generation rejected")
    {
        Owner owner;
        const auto transition = owner.Plan(GenerationCommand(5u));
        REQUIRE(transition.Code == ResultCode::Unbound);
        REQUIRE(owner.Plan(BindCommand(4u, kFingerprint)).Code ==
            ResultCode::StaleGeneration);
        REQUIRE(owner.Plan(BindCommand(6u, kFingerprint)).Code ==
            ResultCode::StaleGeneration);
        REQUIRE(owner.Plan(BindCommand(5u, kFingerprint)).Code ==
            ResultCode::Bound);
    }
}

TEST_CASE("Native load bridge owner validates reserved and non-applicable command fields",
          "[quest.party-state][native-load-owner][command-shape]")
{
    Owner owner;
    BindOwner(owner);
    const auto before = owner.Snapshot();

    SECTION("command reserved")
    {
        auto command = ReserveCommand();
        command.Reserved[2] = 1u;
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("reserve generation")
    {
        auto command = ReserveCommand();
        command.RuntimeGeneration = 1u;
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("reserve nonce")
    {
        auto command = ReserveCommand();
        command.AttemptNonce = 1u;
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("request identity is non-applicable")
    {
        auto command = NonceCommand(CommandKind::Poll, 1u);
        command.Identity = MakeIdentity();
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("generation fingerprint is non-applicable")
    {
        auto command = GenerationCommand(2u);
        command.RuntimeFingerprint = kFingerprint;
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("shutdown nonce is non-applicable")
    {
        auto command = ShutdownCommand();
        command.AttemptNonce = 1u;
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("unknown command")
    {
        Command command{};
        command.Kind = static_cast<CommandKind>(0xFFu);
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    SECTION("unknown bind disposition")
    {
        auto command = BindCommand();
        command.BindDisposition =
            static_cast<BindDisposition>(0xFFu);
        REQUIRE(owner.Plan(command).Code == ResultCode::InvalidCommand);
    }

    const auto after = owner.Snapshot();
    REQUIRE(std::memcmp(
        &before, &after, sizeof(before)) == 0);
}

TEST_CASE("Native load bridge owner validates Reserve identity before effect",
          "[quest.party-state][native-load-owner][reserve]")
{
    Owner owner;
    BindOwner(owner);

    auto empty = MakeIdentity();
    empty.Length = 0u;
    REQUIRE(owner.Plan(ReserveCommand(empty)).Code ==
        ResultCode::InvalidIdentity);

    auto tooLong = MakeIdentity();
    tooLong.Length =
        PartyQuestNativeLoadIdentity::kCapacity + 1u;
    REQUIRE(owner.Plan(ReserveCommand(tooLong)).Code ==
        ResultCode::InvalidIdentity);

    auto reserved = MakeIdentity();
    reserved.Reserved0 = 1u;
    REQUIRE(owner.Plan(ReserveCommand(reserved)).Code ==
        ResultCode::InvalidIdentity);

    REQUIRE(owner.Snapshot().PendingEffect == EffectKind::None);
    RequireSnapshotInvariant(owner.Snapshot());
}

TEST_CASE("Native load bridge owner Reserve publishes exactly one canonical effect",
          "[quest.party-state][native-load-owner][reserve]")
{
    Owner owner;
    BindOwner(owner);

    auto identity = MakeIdentity(5u);
    identity.Bytes[100] = 0xEEu;

    const auto plan = owner.Plan(ReserveCommand(identity));
    RequireEffect(plan, EffectKind::Reserve);
    REQUIRE(plan.Effect.AttemptNonce == 0u);
    REQUIRE(PartyQuestNativeLoadBridgePolicy::IsValidReserveRequest(
        plan.Effect.ReserveRequest));
    REQUIRE(IdentityEqual(
        plan.Effect.ReserveRequest.Identity,
        identity));
    REQUIRE(plan.Effect.ReserveRequest.Identity.Bytes[100] == 0u);
    REQUIRE(owner.Snapshot().PendingEffect == EffectKind::Reserve);

    for (const auto kind : {
             CommandKind::Bind,
             CommandKind::Reserve,
             CommandKind::Cancel,
             CommandKind::Poll,
             CommandKind::Retire,
             CommandKind::GenerationTransition,
             CommandKind::Shutdown})
    {
        const auto command =
            CanonicalCommandFor(owner.Snapshot(), kind);
        REQUIRE(owner.Plan(command).Code ==
            ResultCode::OperationInFlight);
    }
}

TEST_CASE("Native load bridge owner Reserve accepts only exact valid monotonic reservation",
          "[quest.party-state][native-load-owner][reserve]")
{
    Owner owner;
    BindOwner(owner);

    auto identity = MakeIdentity(7u);
    const auto plan = owner.Plan(ReserveCommand(identity));
    RequireEffect(plan, EffectKind::Reserve);

    auto returnedIdentity = identity;
    returnedIdentity.Bytes[200] = 0xA5u;
    const auto applied = owner.ApplyForeignOutcome(
        ReserveSuccess(9u, returnedIdentity));
    REQUIRE(applied.Code == ResultCode::Reserved);

    const auto snapshot = owner.Snapshot();
    REQUIRE(snapshot.RequestPhase == RequestPhase::Active);
    REQUIRE(snapshot.ActiveAttemptNonce == 9u);
    REQUIRE(snapshot.LastAttemptNonce == 9u);
    REQUIRE(snapshot.ActiveIdentity.Bytes[200] == 0u);
    REQUIRE(owner.ClassifyNonce(9u) == NonceClass::ExactActive);
    REQUIRE(owner.ClassifyNonce(8u) == NonceClass::Stale);
    REQUIRE(owner.ClassifyNonce(10u) == NonceClass::Mismatch);
    RequireSnapshotInvariant(snapshot);
}

TEST_CASE("Native load bridge owner local nonce classification prevents stale ABA effects",
          "[quest.party-state][native-load-owner][aba]")
{
    Owner owner;
    BindOwner(owner);

    ReserveOwner(owner, 10u);
    const auto cancelPlan =
        owner.Plan(NonceCommand(CommandKind::Cancel, 10u));
    RequireEffect(cancelPlan, EffectKind::Cancel, 10u);
    const auto cancelled = owner.ApplyForeignOutcome(
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
    REQUIRE(cancelled.Code == ResultCode::Cancelled);

    REQUIRE(owner.ClassifyNonce(10u) == NonceClass::ExactRetired);
    REQUIRE(owner.Plan(
        NonceCommand(CommandKind::Cancel, 10u)).Code ==
        ResultCode::Duplicate);

    ReserveOwner(owner, 11u);
    REQUIRE(owner.ClassifyNonce(10u) == NonceClass::Stale);
    REQUIRE(owner.ClassifyNonce(11u) == NonceClass::ExactActive);
    REQUIRE(owner.ClassifyNonce(12u) == NonceClass::Mismatch);

    for (const auto kind : {
             CommandKind::Cancel,
             CommandKind::Poll,
             CommandKind::Retire})
    {
        const auto stale =
            owner.Plan(NonceCommand(kind, 10u));
        REQUIRE(stale.Code == ResultCode::StaleNonce);
        REQUIRE(stale.HasEffect == 0u);

        const auto mismatch =
            owner.Plan(NonceCommand(kind, 12u));
        REQUIRE(mismatch.Code == ResultCode::NonceMismatch);
        REQUIRE(mismatch.HasEffect == 0u);
    }

    REQUIRE(owner.Snapshot().PendingEffect == EffectKind::None);
    REQUIRE(owner.Snapshot().ActiveAttemptNonce == 11u);
    RequireSnapshotInvariant(owner.Snapshot());
}

TEST_CASE("Native load bridge owner cached Poll is local and byte-idempotent",
          "[quest.party-state][native-load-owner][poll]")
{
    Owner owner;
    BindOwner(owner);
    const auto identity = MakeIdentity(8u);
    ReserveOwner(owner, 3u, identity);

    const auto cached =
        CacheCompletion(owner, 3u, 17u, identity, 0u);
    REQUIRE(cached.Result == 0u);

    const auto first =
        owner.Plan(NonceCommand(CommandKind::Poll, 3u));
    const auto second =
        owner.Plan(NonceCommand(CommandKind::Poll, 3u));
    REQUIRE(first.Code == ResultCode::CompletionAvailable);
    REQUIRE(second.Code == ResultCode::CompletionAvailable);
    REQUIRE(first.HasEffect == 0u);
    REQUIRE(second.HasEffect == 0u);
    REQUIRE(first.HasCompletion == 1u);
    REQUIRE(second.HasCompletion == 1u);
    REQUIRE(std::memcmp(
        &first.Completion,
        &second.Completion,
        sizeof(Completion)) == 0);
    REQUIRE(owner.Snapshot().PendingEffect == EffectKind::None);
    RequireSnapshotInvariant(owner.Snapshot());
}

TEST_CASE("Native load bridge owner false completion is valid evidence",
          "[quest.party-state][native-load-owner][poll]")
{
    Owner owner;
    BindOwner(owner);
    const auto identity = MakeIdentity();
    ReserveOwner(owner, 1u, identity);

    const auto completion =
        CacheCompletion(owner, 1u, 1u, identity, 0u);
    REQUIRE(completion.Result == 0u);
    REQUIRE(owner.Snapshot().RequestPhase ==
        RequestPhase::CompletionCached);
    RequireSnapshotInvariant(owner.Snapshot());
}

TEST_CASE("Native load bridge owner ignores identity tail outside Length",
          "[quest.party-state][native-load-owner][identity]")
{
    Owner owner;
    BindOwner(owner);

    auto identity = MakeIdentity(4u);
    identity.Bytes[100] = 0x11u;
    const auto reservePlan = owner.Plan(ReserveCommand(identity));
    RequireEffect(reservePlan, EffectKind::Reserve);

    auto reservationIdentity = identity;
    reservationIdentity.Bytes[100] = 0x22u;
    REQUIRE(owner.ApplyForeignOutcome(
        ReserveSuccess(1u, reservationIdentity)).Code ==
        ResultCode::Reserved);

    const auto pollPlan =
        owner.Plan(NonceCommand(CommandKind::Poll, 1u));
    RequireEffect(pollPlan, EffectKind::Poll, 1u);

    auto completionIdentity = identity;
    completionIdentity.Bytes[100] = 0x33u;
    const auto completion = owner.ApplyForeignOutcome(
        CompletionSuccess(1u, 1u, completionIdentity, 1u));
    REQUIRE(completion.Code == ResultCode::CompletionAvailable);
    REQUIRE(completion.Completion.Identity.Bytes[100] == 0u);
    REQUIRE(owner.Snapshot().ActiveIdentity.Bytes[100] == 0u);
    RequireSnapshotInvariant(owner.Snapshot());
}

TEST_CASE("Native load bridge owner generation transition retains active drain authority",
          "[quest.party-state][native-load-owner][generation]")
{
    SECTION("cancel before native claim")
    {
        Owner owner;
        BindOwner(owner, 5u);
        ReserveOwner(owner, 1u);

        const auto transition = owner.Plan(GenerationCommand(6u));
        REQUIRE(transition.Code == ResultCode::DrainPending);
        REQUIRE(transition.ReleaseCapability == 0u);

        auto snapshot = owner.Snapshot();
        REQUIRE(snapshot.Phase == Phase::DrainOnly);
        REQUIRE(snapshot.CurrentGeneration == 6u);
        REQUIRE(snapshot.BoundGeneration == 5u);
        REQUIRE(snapshot.CapabilityRetained == 1u);
        REQUIRE(owner.Plan(ReserveCommand()).Code ==
            ResultCode::AdmissionClosed);

        const auto cancel =
            owner.Plan(NonceCommand(CommandKind::Cancel, 1u));
        RequireEffect(cancel, EffectKind::Cancel, 1u);
        const auto cancelled = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
        REQUIRE(cancelled.Code == ResultCode::Cancelled);
        REQUIRE(cancelled.ReleaseCapability == 1u);

        snapshot = owner.Snapshot();
        REQUIRE(snapshot.Phase == Phase::Unbound);
        REQUIRE(snapshot.CurrentGeneration == 6u);
        RequireSnapshotInvariant(snapshot);
        RequireReleaseSafe(cancelled, snapshot);
    }

    SECTION("native already claimed")
    {
        Owner owner;
        BindOwner(owner, 9u);
        const auto identity = MakeIdentity();
        ReserveOwner(owner, 1u, identity);

        REQUIRE(owner.Plan(GenerationCommand(10u)).Code ==
            ResultCode::DrainPending);

        const auto cancel =
            owner.Plan(NonceCommand(CommandKind::Cancel, 1u));
        RequireEffect(cancel, EffectKind::Cancel, 1u);
        const auto pending = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Cancel, Status::InvalidState));
        REQUIRE(pending.Code == ResultCode::Pending);
        REQUIRE(pending.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().Phase == Phase::DrainOnly);

        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 1u));
        RequireEffect(poll, EffectKind::Poll, 1u);
        REQUIRE(owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Poll, Status::Pending)).Code ==
            ResultCode::Pending);

        CacheCompletion(owner, 1u, 1u, identity);
        const auto retire =
            owner.Plan(NonceCommand(CommandKind::Retire, 1u));
        RequireEffect(retire, EffectKind::Retire, 1u);
        const auto retired = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Retire, Status::Retired));
        REQUIRE(retired.Code == ResultCode::Retired);
        REQUIRE(retired.ReleaseCapability == 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::Unbound);
        RequireSnapshotInvariant(owner.Snapshot());
    }
}

TEST_CASE("Native load bridge owner generation transition without request releases binding",
          "[quest.party-state][native-load-owner][generation]")
{
    Owner owner;
    BindOwner(owner, 2u);

    const auto transition = owner.Plan(GenerationCommand(3u));
    REQUIRE(transition.Code == ResultCode::Unbound);
    REQUIRE(transition.ReleaseCapability == 1u);

    const auto snapshot = owner.Snapshot();
    REQUIRE(snapshot.Phase == Phase::Unbound);
    REQUIRE(snapshot.CurrentGeneration == 3u);
    REQUIRE(snapshot.BoundGeneration == 0u);
    REQUIRE(snapshot.RuntimeFingerprint == 0u);
    RequireSnapshotInvariant(snapshot);
    RequireReleaseSafe(transition, snapshot);

    REQUIRE(owner.Plan(BindCommand(2u, kFingerprint)).Code ==
        ResultCode::StaleGeneration);
    REQUIRE(owner.Plan(BindCommand(4u, kFingerprint)).Code ==
        ResultCode::StaleGeneration);
    REQUIRE(owner.Plan(BindCommand(3u, kFingerprint)).Code ==
        ResultCode::Bound);
}

TEST_CASE("Native load bridge owner generation must advance monotonically",
          "[quest.party-state][native-load-owner][generation]")
{
    Owner owner;
    REQUIRE(owner.Plan(GenerationCommand(4u)).Code ==
        ResultCode::Unbound);

    const auto before = owner.Snapshot();
    REQUIRE(owner.Plan(GenerationCommand(4u)).Code ==
        ResultCode::InvalidCommand);
    REQUIRE(owner.Plan(GenerationCommand(3u)).Code ==
        ResultCode::InvalidCommand);
    const auto after = owner.Snapshot();
    REQUIRE(std::memcmp(
        &before, &after, sizeof(before)) == 0);
}

TEST_CASE("Native load bridge owner Shutdown drains before releasing capability",
          "[quest.party-state][native-load-owner][shutdown]")
{
    SECTION("no active request")
    {
        Owner owner;
        BindOwner(owner);
        const auto shutdown = owner.Plan(ShutdownCommand());
        REQUIRE(shutdown.Code == ResultCode::ShutdownComplete);
        REQUIRE(shutdown.ReleaseCapability == 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownComplete);
        RequireSnapshotInvariant(owner.Snapshot());
        RequireReleaseSafe(shutdown, owner.Snapshot());
    }

    SECTION("cancel before claim")
    {
        Owner owner;
        BindOwner(owner);
        ReserveOwner(owner, 1u);

        const auto shutdown = owner.Plan(ShutdownCommand());
        RequireEffect(shutdown, EffectKind::Cancel, 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownDrain);

        const auto cancelled = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Cancel, Status::Cancelled));
        REQUIRE(cancelled.Code == ResultCode::Cancelled);
        REQUIRE(cancelled.ReleaseCapability == 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownComplete);
        RequireSnapshotInvariant(owner.Snapshot());
        RequireReleaseSafe(cancelled, owner.Snapshot());
    }

    SECTION("already claimed then completion")
    {
        Owner owner;
        BindOwner(owner);
        const auto identity = MakeIdentity();
        ReserveOwner(owner, 1u, identity);

        const auto shutdown = owner.Plan(ShutdownCommand());
        RequireEffect(shutdown, EffectKind::Cancel, 1u);

        const auto pending = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Cancel, Status::InvalidState));
        REQUIRE(pending.Code == ResultCode::Pending);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownDrain);
        REQUIRE(owner.Snapshot().CapabilityRetained == 1u);

        CacheCompletion(owner, 1u, 1u, identity);

        const auto shutdownAgain = owner.Plan(ShutdownCommand());
        RequireEffect(shutdownAgain, EffectKind::Retire, 1u);
        const auto retired = owner.ApplyForeignOutcome(
            ReturnedOutcome(EffectKind::Retire, Status::Retired));
        REQUIRE(retired.Code == ResultCode::Retired);
        REQUIRE(retired.ReleaseCapability == 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownComplete);
        RequireReleaseSafe(retired, owner.Snapshot());
    }

    SECTION("cached completion skips cancel")
    {
        Owner owner;
        BindOwner(owner);
        const auto identity = MakeIdentity();
        ReserveOwner(owner, 1u, identity);
        CacheCompletion(owner, 1u, 1u, identity);

        const auto shutdown = owner.Plan(ShutdownCommand());
        RequireEffect(shutdown, EffectKind::Retire, 1u);
        REQUIRE(owner.Snapshot().Phase == Phase::ShutdownDrain);
    }
}

TEST_CASE("Native load bridge owner all phase by command guards are deterministic",
          "[quest.party-state][native-load-owner][matrix]")
{
    constexpr std::array<Phase, 6> phases{
        Phase::Unbound,
        Phase::Bound,
        Phase::DrainOnly,
        Phase::ShutdownDrain,
        Phase::ShutdownComplete,
        Phase::PoisonedUnsafeToUnload};

    constexpr std::array<CommandKind, 7> commands{
        CommandKind::Bind,
        CommandKind::Reserve,
        CommandKind::Cancel,
        CommandKind::Poll,
        CommandKind::Retire,
        CommandKind::GenerationTransition,
        CommandKind::Shutdown};

    for (const auto phase : phases)
    {
        for (const auto kind : commands)
        {
            Owner owner;
            ReachPhase(owner, phase);
            const auto before = owner.Snapshot();
            const auto command =
                CanonicalCommandFor(before, kind);
            const auto result = owner.Plan(command);

            REQUIRE(result.Code ==
                ExpectedMatrixCode(phase, kind));
            RequireSnapshotInvariant(owner.Snapshot());
            RequireReleaseSafe(result, owner.Snapshot());

            if (phase == Phase::PoisonedUnsafeToUnload)
            {
                REQUIRE(result.ReleaseCapability == 0u);
                REQUIRE(result.HasEffect == 0u);
                REQUIRE(owner.Snapshot().Phase ==
                    Phase::PoisonedUnsafeToUnload);
            }
        }
    }
}

TEST_CASE("Native load bridge owner maps every known status per pending effect",
          "[quest.party-state][native-load-owner][status-matrix]")
{
    constexpr std::array<EffectKind, 4> effects{
        EffectKind::Reserve,
        EffectKind::Cancel,
        EffectKind::Poll,
        EffectKind::Retire};

    for (const auto effect : effects)
    {
        for (const auto status : kKnownStatuses)
        {
            Owner owner;
            ReachPendingEffect(owner, effect);
            const auto before = owner.Snapshot();
            auto outcome =
                ValidOutcomeFor(effect, status, before);

            const auto result =
                owner.ApplyForeignOutcome(outcome);

            if (!IsAllowedStatus(effect, status))
            {
                REQUIRE(result.Code ==
                    ResultCode::PoisonedUnsafeToUnload);
                REQUIRE(result.ReleaseCapability == 0u);
                REQUIRE(owner.Snapshot().Phase ==
                    Phase::PoisonedUnsafeToUnload);
                RequireSnapshotInvariant(owner.Snapshot());
                continue;
            }

            REQUIRE(result.Code !=
                ResultCode::PoisonedUnsafeToUnload);

            if (effect == EffectKind::Reserve)
                REQUIRE(result.Code == ResultCode::Reserved);
            else if (effect == EffectKind::Cancel &&
                     status == Status::Cancelled)
                REQUIRE(result.Code == ResultCode::Cancelled);
            else if (effect == EffectKind::Cancel)
                REQUIRE(result.Code == ResultCode::Pending);
            else if (effect == EffectKind::Poll &&
                     status == Status::Pending)
                REQUIRE(result.Code == ResultCode::Pending);
            else if (effect == EffectKind::Poll)
                REQUIRE(result.Code ==
                    ResultCode::CompletionAvailable);
            else if (effect == EffectKind::Retire)
                REQUIRE(result.Code == ResultCode::Retired);

            RequireSnapshotInvariant(owner.Snapshot());
        }
    }
}

TEST_CASE("Native load bridge owner unknown status poisons every effect",
          "[quest.party-state][native-load-owner][status-matrix]")
{
    for (const auto effect : {
             EffectKind::Reserve,
             EffectKind::Cancel,
             EffectKind::Poll,
             EffectKind::Retire})
    {
        Owner owner;
        ReachPendingEffect(owner, effect);

        Outcome outcome{};
        outcome.EffectKind = effect;
        outcome.Disposition = ForeignDisposition::Returned;
        outcome.RawStatus =
            std::numeric_limits<uint32_t>::max();

        const auto result =
            owner.ApplyForeignOutcome(outcome);
        REQUIRE(result.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(result.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().Phase ==
            Phase::PoisonedUnsafeToUnload);
        RequireSnapshotInvariant(owner.Snapshot());
    }
}

TEST_CASE("Native load bridge owner unknown foreign disposition poisons every effect",
          "[quest.party-state][native-load-owner][foreign-unknown]")
{
    for (const auto effect : {
             EffectKind::Reserve,
             EffectKind::Cancel,
             EffectKind::Poll,
             EffectKind::Retire})
    {
        Owner owner;
        ReachPendingEffect(owner, effect);

        const auto result =
            owner.ApplyForeignOutcome(UnknownOutcome(effect));
        REQUIRE(result.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(result.ReleaseCapability == 0u);
        REQUIRE(owner.Snapshot().Phase ==
            Phase::PoisonedUnsafeToUnload);
        RequireSnapshotInvariant(owner.Snapshot());
    }
}

TEST_CASE("Native load bridge owner effect mismatch and outcome reserved fields poison",
          "[quest.party-state][native-load-owner][foreign-shape]")
{
    SECTION("effect mismatch")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome =
            ReserveSuccess(1u, MakeIdentity());
        outcome.EffectKind = EffectKind::Poll;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("reserved0")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Cancel);
        auto outcome =
            ReturnedOutcome(EffectKind::Cancel, Status::Cancelled);
        outcome.Reserved0[1] = 1u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("reserved1")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome =
            ReturnedOutcome(EffectKind::Poll, Status::Pending);
        outcome.Reserved1 = 1u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("unknown disposition enum")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Retire);
        auto outcome =
            ReturnedOutcome(EffectKind::Retire, Status::Retired);
        outcome.Disposition =
            static_cast<ForeignDisposition>(0xFFu);
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }
}

TEST_CASE("Native load bridge owner Reserve payload corruption poisons",
          "[quest.party-state][native-load-owner][payload-corruption]")
{
    SECTION("missing reservation flag")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome = ReserveSuccess(1u, MakeIdentity());
        outcome.HasReservation = 0u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("contradictory completion")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome = ReserveSuccess(1u, MakeIdentity());
        outcome.HasCompletion = 1u;
        outcome.Completion =
            MakeCompletion(1u, 1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("bad reservation ABI")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome = ReserveSuccess(1u, MakeIdentity());
        ++outcome.Reservation.AbiVersion;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("bad reservation size")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome = ReserveSuccess(1u, MakeIdentity());
        --outcome.Reservation.StructSize;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("zero nonce")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto outcome = ReserveSuccess(1u, MakeIdentity());
        outcome.Reservation.AttemptNonce = 0u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("identity length mismatch")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Reserve);
        auto mismatch = MakeIdentity(5u);
        auto outcome = ReserveSuccess(1u, mismatch);
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("identity first middle last mismatch")
    {
        constexpr std::array<size_t, 3> positions{0u, 2u, 3u};
        for (const auto position : positions)
        {
            Owner owner;
            ReachPendingEffect(owner, EffectKind::Reserve);
            auto mismatch = MakeIdentity();
            ++mismatch.Bytes[position];
            auto outcome = ReserveSuccess(1u, mismatch);
            REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
                ResultCode::PoisonedUnsafeToUnload);
        }
    }
}

TEST_CASE("Native load bridge owner Poll payload corruption poisons",
          "[quest.party-state][native-load-owner][payload-corruption]")
{
    SECTION("Pending carries completion")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome =
            ReturnedOutcome(EffectKind::Poll, Status::Pending);
        outcome.HasCompletion = 1u;
        outcome.Completion =
            MakeCompletion(1u, 1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("CompletionAvailable missing flag")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        outcome.HasCompletion = 0u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("completion ABI and size")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        ++outcome.Completion.AbiVersion;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);

        Owner owner2;
        ReachPendingEffect(owner2, EffectKind::Poll);
        outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        --outcome.Completion.StructSize;
        REQUIRE(owner2.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("nonce mismatch")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            2u, 1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("zero sequence")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        outcome.Completion.EventSequence = 0u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("invalid result byte")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity(), 2u);
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("reserved completion byte")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        outcome.Completion.Reserved[4] = 1u;
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("identity mismatch")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto mismatch = MakeIdentity();
        ++mismatch.Bytes[2];
        auto outcome = CompletionSuccess(
            1u, 1u, mismatch);
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("contradictory reservation")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Poll);
        auto outcome = CompletionSuccess(
            1u, 1u, MakeIdentity());
        outcome.HasReservation = 1u;
        outcome.Reservation =
            MakeReservation(1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }
}

TEST_CASE("Native load bridge owner Cancel and Retire reject contradictory payloads",
          "[quest.party-state][native-load-owner][payload-corruption]")
{
    SECTION("cancel")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Cancel);
        auto outcome =
            ReturnedOutcome(EffectKind::Cancel, Status::Cancelled);
        outcome.HasCompletion = 1u;
        outcome.Completion =
            MakeCompletion(1u, 1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("retire")
    {
        Owner owner;
        ReachPendingEffect(owner, EffectKind::Retire);
        auto outcome =
            ReturnedOutcome(EffectKind::Retire, Status::Retired);
        outcome.HasReservation = 1u;
        outcome.Reservation =
            MakeReservation(1u, MakeIdentity());
        REQUIRE(owner.ApplyForeignOutcome(outcome).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }
}

TEST_CASE("Native load bridge owner attempt nonce is strictly monotonic across retire and rebind",
          "[quest.party-state][native-load-owner][monotonic]")
{
    Owner owner;
    BindOwner(owner, 1u);

    ReserveOwner(owner, 5u);
    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 5u));
    RequireEffect(cancel, EffectKind::Cancel, 5u);
    REQUIRE(owner.ApplyForeignOutcome(
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled)).Code ==
        ResultCode::Cancelled);

    REQUIRE(owner.Plan(GenerationCommand(2u)).Code ==
        ResultCode::Unbound);
    REQUIRE(owner.Plan(BindCommand(2u, kFingerprint)).Code ==
        ResultCode::Bound);

    const auto reservePlan = owner.Plan(ReserveCommand());
    RequireEffect(reservePlan, EffectKind::Reserve);
    REQUIRE(owner.ApplyForeignOutcome(
        ReserveSuccess(6u, MakeIdentity())).Code ==
        ResultCode::Reserved);

    const auto cancel2 =
        owner.Plan(NonceCommand(CommandKind::Cancel, 6u));
    RequireEffect(cancel2, EffectKind::Cancel, 6u);
    REQUIRE(owner.ApplyForeignOutcome(
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled)).Code ==
        ResultCode::Cancelled);

    SECTION("equal")
    {
        const auto badPlan = owner.Plan(ReserveCommand());
        RequireEffect(badPlan, EffectKind::Reserve);
        const auto poisoned = owner.ApplyForeignOutcome(
            ReserveSuccess(6u, MakeIdentity()));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(owner.Snapshot().LastAttemptNonce == 6u);
    }

    SECTION("lower")
    {
        const auto badPlan = owner.Plan(ReserveCommand());
        RequireEffect(badPlan, EffectKind::Reserve);
        const auto poisoned = owner.ApplyForeignOutcome(
            ReserveSuccess(5u, MakeIdentity()));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(owner.Snapshot().LastAttemptNonce == 6u);
    }
}

TEST_CASE("Native load bridge owner completion sequence is strictly monotonic across retire and rebind",
          "[quest.party-state][native-load-owner][monotonic]")
{
    Owner owner;
    BindOwner(owner, 1u);

    ReserveOwner(owner, 1u);
    CacheCompletion(owner, 1u, 9u);
    RetireOwner(owner, 1u);

    REQUIRE(owner.Plan(GenerationCommand(2u)).Code ==
        ResultCode::Unbound);
    REQUIRE(owner.Plan(BindCommand(2u, kFingerprint)).Code ==
        ResultCode::Bound);

    ReserveOwner(owner, 2u);

    SECTION("equal")
    {
        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 2u));
        RequireEffect(poll, EffectKind::Poll, 2u);
        const auto poisoned = owner.ApplyForeignOutcome(
            CompletionSuccess(2u, 9u, MakeIdentity()));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(owner.Snapshot().LastCompletionSequence == 9u);
    }

    SECTION("lower")
    {
        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 2u));
        RequireEffect(poll, EffectKind::Poll, 2u);
        const auto poisoned = owner.ApplyForeignOutcome(
            CompletionSuccess(2u, 8u, MakeIdentity()));
        REQUIRE(poisoned.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(owner.Snapshot().LastCompletionSequence == 9u);
    }
}

TEST_CASE("Native load bridge owner accepts UINT64_MAX only as final monotonic value",
          "[quest.party-state][native-load-owner][max]")
{
    SECTION("attempt nonce")
    {
        Owner owner;
        BindOwner(owner);
        ReserveOwner(
            owner,
            std::numeric_limits<uint64_t>::max());

        const auto cancel =
            owner.Plan(NonceCommand(
                CommandKind::Cancel,
                std::numeric_limits<uint64_t>::max()));
        RequireEffect(
            cancel,
            EffectKind::Cancel,
            std::numeric_limits<uint64_t>::max());
        REQUIRE(owner.ApplyForeignOutcome(
            ReturnedOutcome(
                EffectKind::Cancel,
                Status::Cancelled)).Code ==
            ResultCode::Cancelled);

        const auto next = owner.Plan(ReserveCommand());
        RequireEffect(next, EffectKind::Reserve);
        REQUIRE(owner.ApplyForeignOutcome(
            ReserveSuccess(
                std::numeric_limits<uint64_t>::max(),
                MakeIdentity())).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }

    SECTION("completion sequence")
    {
        Owner owner;
        BindOwner(owner);
        ReserveOwner(owner, 1u);
        CacheCompletion(
            owner,
            1u,
            std::numeric_limits<uint64_t>::max());
        RetireOwner(owner, 1u);

        ReserveOwner(owner, 2u);
        const auto poll =
            owner.Plan(NonceCommand(CommandKind::Poll, 2u));
        RequireEffect(poll, EffectKind::Poll, 2u);
        REQUIRE(owner.ApplyForeignOutcome(
            CompletionSuccess(
                2u,
                std::numeric_limits<uint64_t>::max(),
                MakeIdentity())).Code ==
            ResultCode::PoisonedUnsafeToUnload);
    }
}

TEST_CASE("Native load bridge owner poison is absorbing and never releasable",
          "[quest.party-state][native-load-owner][poison]")
{
    Owner owner;
    BindOwner(owner);
    ReserveOwner(owner, 1u);

    const auto poll =
        owner.Plan(NonceCommand(CommandKind::Poll, 1u));
    RequireEffect(poll, EffectKind::Poll, 1u);

    const auto poisoned =
        owner.ApplyForeignOutcome(
            UnknownOutcome(EffectKind::Poll));
    REQUIRE(poisoned.Code ==
        ResultCode::PoisonedUnsafeToUnload);
    REQUIRE(poisoned.ReleaseCapability == 0u);

    const auto poisonedSnapshot = owner.Snapshot();
    REQUIRE(poisonedSnapshot.Phase ==
        Phase::PoisonedUnsafeToUnload);
    REQUIRE(poisonedSnapshot.CapabilityRetained == 1u);
    REQUIRE(poisonedSnapshot.ActiveAttemptNonce == 1u);
    REQUIRE(poisonedSnapshot.PendingEffect == EffectKind::None);
    RequireSnapshotInvariant(poisonedSnapshot);

    for (const auto kind : {
             CommandKind::Bind,
             CommandKind::Reserve,
             CommandKind::Cancel,
             CommandKind::Poll,
             CommandKind::Retire,
             CommandKind::GenerationTransition,
             CommandKind::Shutdown})
    {
        const auto result = owner.Plan(
            CanonicalCommandFor(poisonedSnapshot, kind));
        REQUIRE(result.Code ==
            ResultCode::PoisonedUnsafeToUnload);
        REQUIRE(result.ReleaseCapability == 0u);
        REQUIRE(result.HasEffect == 0u);
    }

    Outcome arbitrary{};
    arbitrary.EffectKind = EffectKind::Reserve;
    arbitrary.Disposition = ForeignDisposition::Returned;
    arbitrary.RawStatus = static_cast<uint32_t>(Status::Reserved);
    const auto apply = owner.ApplyForeignOutcome(arbitrary);
    REQUIRE(apply.Code ==
        ResultCode::PoisonedUnsafeToUnload);
    REQUIRE(apply.ReleaseCapability == 0u);

    const auto poisonedAfter = owner.Snapshot();
    REQUIRE(std::memcmp(
        &poisonedSnapshot,
        &poisonedAfter,
        sizeof(poisonedSnapshot)) == 0);
}

TEST_CASE("Native load bridge owner Apply without a pending effect is inert",
          "[quest.party-state][native-load-owner]")
{
    Owner owner;
    const auto before = owner.Snapshot();
    const auto result = owner.ApplyForeignOutcome(
        ReturnedOutcome(EffectKind::Reserve, Status::Reserved));
    REQUIRE(result.Code == ResultCode::InvalidState);
    const auto after = owner.Snapshot();
    REQUIRE(std::memcmp(
        &before, &after, sizeof(before)) == 0);
}

TEST_CASE("Native load bridge owner deterministic model trace preserves ownership invariants",
          "[quest.party-state][native-load-owner][property]")
{
    Owner owner;
    BindOwner(owner, 1u);

    uint64_t generation = 1u;
    for (uint64_t index = 1u; index <= 32u; ++index)
    {
        const auto identity = MakeIdentity(
            static_cast<uint16_t>(1u + (index % 20u)),
            static_cast<uint8_t>(0x20u + index));

        ReserveOwner(owner, index, identity);
        REQUIRE(owner.Snapshot().ActiveAttemptNonce == index);
        RequireSnapshotInvariant(owner.Snapshot());

        const auto firstPoll =
            owner.Plan(NonceCommand(CommandKind::Poll, index));
        RequireEffect(firstPoll, EffectKind::Poll, index);
        REQUIRE(owner.ApplyForeignOutcome(
            ReturnedOutcome(
                EffectKind::Poll,
                Status::Pending)).Code ==
            ResultCode::Pending);
        RequireSnapshotInvariant(owner.Snapshot());

        CacheCompletion(
            owner,
            index,
            index,
            identity,
            static_cast<uint8_t>(index & 1u));

        const auto cached =
            owner.Plan(NonceCommand(CommandKind::Poll, index));
        REQUIRE(cached.Code == ResultCode::CompletionAvailable);
        REQUIRE(cached.HasEffect == 0u);
        REQUIRE(cached.HasCompletion == 1u);
        RequireSnapshotInvariant(owner.Snapshot());

        RetireOwner(owner, index);
        REQUIRE(owner.Snapshot().ActiveAttemptNonce == 0u);
        REQUIRE(owner.Snapshot().LastAttemptNonce == index);
        REQUIRE(owner.Snapshot().LastCompletionSequence == index);

        if (index == 16u)
        {
            ++generation;
            const auto transition =
                owner.Plan(GenerationCommand(generation));
            REQUIRE(transition.Code == ResultCode::Unbound);
            REQUIRE(transition.ReleaseCapability == 1u);
            RequireReleaseSafe(transition, owner.Snapshot());

            REQUIRE(owner.Plan(
                BindCommand(generation, kFingerprint)).Code ==
                ResultCode::Bound);
            RequireSnapshotInvariant(owner.Snapshot());
        }
    }

    const auto finalSnapshot = owner.Snapshot();
    REQUIRE(finalSnapshot.LastAttemptNonce == 32u);
    REQUIRE(finalSnapshot.LastCompletionSequence == 32u);
    REQUIRE(finalSnapshot.RequestPhase == RequestPhase::None);
    RequireSnapshotInvariant(finalSnapshot);
}

TEST_CASE("Native load bridge owner effect sequence binds each published Plan instance",
          "[quest.party-state][native-load-owner][effect-sequence]")
{
    Owner owner;
    BindOwner(owner, 1u);

    const auto firstReserve = owner.Plan(ReserveCommand(MakeIdentity(4u, 0x31u)));
    RequireEffect(firstReserve, EffectKind::Reserve);
    const uint64_t firstSequence = firstReserve.Effect.Sequence;
    REQUIRE(owner.Snapshot().PendingEffectSequence == firstSequence);

    REQUIRE(owner.ApplyForeignOutcome(
        ReserveSuccess(1u, MakeIdentity(4u, 0x31u))).Code ==
        ResultCode::Reserved);
    REQUIRE(owner.Snapshot().PendingEffectSequence == 0u);

    const auto cancel =
        owner.Plan(NonceCommand(CommandKind::Cancel, 1u));
    RequireEffect(cancel, EffectKind::Cancel, 1u);
    REQUIRE(cancel.Effect.Sequence > firstSequence);
    REQUIRE(owner.Snapshot().PendingEffectSequence ==
        cancel.Effect.Sequence);

    REQUIRE(owner.ApplyForeignOutcome(
        ReturnedOutcome(EffectKind::Cancel, Status::Cancelled)).Code ==
        ResultCode::Cancelled);
    REQUIRE(owner.Snapshot().PendingEffectSequence == 0u);

    const auto secondReserve =
        owner.Plan(ReserveCommand(MakeIdentity(4u, 0x41u)));
    RequireEffect(secondReserve, EffectKind::Reserve);
    REQUIRE(secondReserve.Effect.Sequence > cancel.Effect.Sequence);
    REQUIRE(secondReserve.Effect.Sequence != firstSequence);
    REQUIRE(owner.Snapshot().PendingEffectSequence ==
        secondReserve.Effect.Sequence);
}

TEST_CASE("Native load bridge owner fixed POD layouts and noexcept surface are stable",
          "[quest.party-state][native-load-owner][abi]")
{
    STATIC_REQUIRE(sizeof(Phase) == 1u);
    STATIC_REQUIRE(sizeof(RequestPhase) == 1u);
    STATIC_REQUIRE(sizeof(CommandKind) == 1u);
    STATIC_REQUIRE(sizeof(BindDisposition) == 1u);
    STATIC_REQUIRE(sizeof(EffectKind) == 1u);
    STATIC_REQUIRE(sizeof(ForeignDisposition) == 1u);
    STATIC_REQUIRE(sizeof(NonceClass) == 1u);
    STATIC_REQUIRE(sizeof(ResultCode) == 1u);

    STATIC_REQUIRE(static_cast<uint8_t>(Phase::Unbound) == 0u);
    STATIC_REQUIRE(static_cast<uint8_t>(Phase::Bound) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(Phase::DrainOnly) == 2u);
    STATIC_REQUIRE(static_cast<uint8_t>(Phase::ShutdownDrain) == 3u);
    STATIC_REQUIRE(static_cast<uint8_t>(Phase::ShutdownComplete) == 4u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        Phase::PoisonedUnsafeToUnload) == 5u);

    STATIC_REQUIRE(static_cast<uint8_t>(RequestPhase::None) == 0u);
    STATIC_REQUIRE(static_cast<uint8_t>(RequestPhase::Active) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        RequestPhase::CompletionCached) == 2u);

    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Invalid) == 0u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Bind) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Reserve) == 2u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Cancel) == 3u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Poll) == 4u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Retire) == 5u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        CommandKind::GenerationTransition) == 6u);
    STATIC_REQUIRE(static_cast<uint8_t>(CommandKind::Shutdown) == 7u);

    STATIC_REQUIRE(static_cast<uint8_t>(EffectKind::None) == 0u);
    STATIC_REQUIRE(static_cast<uint8_t>(EffectKind::Reserve) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(EffectKind::Cancel) == 2u);
    STATIC_REQUIRE(static_cast<uint8_t>(EffectKind::Poll) == 3u);
    STATIC_REQUIRE(static_cast<uint8_t>(EffectKind::Retire) == 4u);

    STATIC_REQUIRE(static_cast<uint8_t>(
        ForeignDisposition::Returned) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ForeignDisposition::Unknown) == 2u);

    STATIC_REQUIRE(static_cast<uint8_t>(
        BindDisposition::Rejected) == 0u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        BindDisposition::Authenticated) == 1u);

    STATIC_REQUIRE(static_cast<uint8_t>(
        NonceClass::ExactActive) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        NonceClass::ExactRetired) == 2u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        NonceClass::Stale) == 3u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        NonceClass::Mismatch) == 4u);

    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::EffectRequired) == 1u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::Bound) == 2u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::CompletionAvailable) == 9u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::DrainPending) == 20u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::ShutdownComplete) == 21u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::InvalidCommand) == 22u);
    STATIC_REQUIRE(static_cast<uint8_t>(
        ResultCode::PoisonedUnsafeToUnload) == 23u);

    STATIC_REQUIRE(sizeof(Command) == 296u);
    STATIC_REQUIRE(alignof(Command) == 8u);
    STATIC_REQUIRE(offsetof(Command, Kind) == 0u);
    STATIC_REQUIRE(offsetof(Command, BindDisposition) == 1u);
    STATIC_REQUIRE(offsetof(Command, RuntimeGeneration) == 8u);
    STATIC_REQUIRE(offsetof(Command, RuntimeFingerprint) == 16u);
    STATIC_REQUIRE(offsetof(Command, AttemptNonce) == 24u);
    STATIC_REQUIRE(offsetof(Command, Identity) == 32u);

    STATIC_REQUIRE(sizeof(Effect) == 296u);
    STATIC_REQUIRE(alignof(Effect) == 8u);
    STATIC_REQUIRE(offsetof(Effect, Kind) == 0u);
    STATIC_REQUIRE(offsetof(Effect, Sequence) == 8u);
    STATIC_REQUIRE(offsetof(Effect, AttemptNonce) == 16u);
    STATIC_REQUIRE(offsetof(Effect, ReserveRequest) == 24u);

    STATIC_REQUIRE(sizeof(Outcome) == 592u);
    STATIC_REQUIRE(alignof(Outcome) == 8u);
    STATIC_REQUIRE(offsetof(Outcome, EffectKind) == 0u);
    STATIC_REQUIRE(offsetof(Outcome, Disposition) == 1u);
    STATIC_REQUIRE(offsetof(Outcome, RawStatus) == 8u);
    STATIC_REQUIRE(offsetof(Outcome, Reservation) == 16u);
    STATIC_REQUIRE(offsetof(Outcome, Completion) == 296u);

    STATIC_REQUIRE(sizeof(Result) == 600u);
    STATIC_REQUIRE(alignof(Result) == 8u);
    STATIC_REQUIRE(offsetof(Result, Code) == 0u);
    STATIC_REQUIRE(offsetof(Result, Effect) == 8u);
    STATIC_REQUIRE(offsetof(Result, Completion) == 304u);

    STATIC_REQUIRE(sizeof(Snapshot) == 624u);
    STATIC_REQUIRE(alignof(Snapshot) == 8u);
    STATIC_REQUIRE(offsetof(Snapshot, Phase) == 0u);
    STATIC_REQUIRE(offsetof(Snapshot, PendingEffectSequence) == 8u);
    STATIC_REQUIRE(offsetof(Snapshot, CurrentGeneration) == 16u);
    STATIC_REQUIRE(offsetof(Snapshot, ActiveIdentity) == 64u);
    STATIC_REQUIRE(offsetof(Snapshot, CachedCompletion) == 328u);

    STATIC_REQUIRE(std::is_standard_layout_v<Command>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Command>);
    STATIC_REQUIRE(std::is_standard_layout_v<Effect>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Effect>);
    STATIC_REQUIRE(std::is_standard_layout_v<Outcome>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Outcome>);
    STATIC_REQUIRE(std::is_standard_layout_v<Result>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Result>);
    STATIC_REQUIRE(std::is_standard_layout_v<Snapshot>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Snapshot>);

    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Owner>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Owner>);

    STATIC_REQUIRE(noexcept(
        std::declval<Owner&>().Plan(
            std::declval<const Command&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<Owner&>().ApplyForeignOutcome(
            std::declval<const Outcome&>())));
    STATIC_REQUIRE(noexcept(
        std::declval<const Owner&>().Snapshot()));
    STATIC_REQUIRE(noexcept(
        std::declval<const Owner&>().ClassifyNonce(uint64_t{})));
}
