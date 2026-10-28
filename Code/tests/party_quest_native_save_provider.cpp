#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderOwner.h>
#include <Games/Skyrim/PartyQuestSkyrimNativeSaveProviderResolver.h>
#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>

#include <catch2/catch.hpp>

#include <limits>
#include <type_traits>
#include <utility>

namespace
{
PartyQuestNativeSaveProviderDescriptor Descriptor()
{
    PartyQuestNativeSaveProviderDescriptor descriptor;
    descriptor.AbiVersion = kPartyQuestNativeSaveProviderDescriptorAbi;
    descriptor.StructSize = sizeof(PartyQuestNativeSaveProviderDescriptor);
    descriptor.EventAbiVersion = kPartyQuestNativeSaveEventAbi;
    descriptor.ImplementationVersion =
        kPartyQuestNativeSaveProviderImplementationVersion;
    descriptor.Capabilities =
        kPartyQuestRequiredNativeSaveProviderCapabilities;
    descriptor.RuntimeMajor = 1;
    descriptor.RuntimeMinor = 6;
    descriptor.RuntimePatch = 1170;
    descriptor.ProviderFingerprint =
        kPartyQuestNativeSaveProviderFingerprint;
    return descriptor;
}

PartyQuestNativeSaveProviderDescriptor ResearchDescriptor()
{
    auto descriptor = Descriptor();
    descriptor.ProviderFingerprint =
        kPartyQuestTask07ResearchProviderFingerprint;
    descriptor.Reserved0 = kPartyQuestTask07ResearchProviderMarker;
    return descriptor;
}

PartyQuestAsyncSaveRequestIdentity TrackedIdentity(uint64_t aNonce = 1u)
{
    PartyQuestAsyncSaveRequestIdentity identity;
    identity.CampaignId = {1u, 2u};
    identity.PlayerProfileId = {3u, 4u};
    identity.RuntimeGeneration = 5u;
    identity.TransactionId = 6u;
    identity.TargetWorldRevision = 7u;
    identity.CaptureEpochId = 8u;
    identity.AttemptNonce = aNonce;
    identity.SaveName = aNonce == 1u ?
        "STR_PreRepair_T0000000000000006_R0000000000000007_A0000000000000001" :
        "STR_PreRepair_T0000000000000006_R0000000000000007_A0000000000000002";
    return identity;
}
}

static_assert(!std::is_copy_constructible_v<PartyQuestNativeSaveProviderToken>);
static_assert(!std::is_copy_assignable_v<PartyQuestNativeSaveProviderToken>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestSkyrimNativeSaveProviderPollCapability>);
static_assert(!std::is_copy_assignable_v<
    PartyQuestSkyrimNativeSaveProviderPollCapability>);
static_assert(std::is_nothrow_move_constructible_v<
    PartyQuestSkyrimNativeSaveProviderPollCapability>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestSkyrimNativeSaveProviderOwner>);
static_assert(!std::is_move_constructible_v<
    PartyQuestSkyrimNativeSaveProviderOwner>);
using TNativeBeginAndInvoke =
    PartyQuestSkyrimNativeSaveProviderBeginInvokeResult (
        PartyQuestSkyrimNativeSaveProviderOwner::*)(
            const PartyQuestAsyncSaveRequestIdentity&,
            PartyQuestSkyrimNativeSaveInvoker,
            void*) noexcept;
static_assert(std::is_same_v<
    decltype(&PartyQuestSkyrimNativeSaveProviderOwner::BeginAndInvoke),
    TNativeBeginAndInvoke>);
using TNativeTrackedBeginAndInvoke =
    PartyQuestSkyrimNativeSaveTrackedBeginResult (
        PartyQuestSkyrimNativeSaveProviderOwner::*)(
            const PartyQuestAsyncSaveRequestIdentity&,
            uint64_t,
            bool,
            bool,
            PartyQuestSkyrimNativeSaveInvoker,
            void*) noexcept;
static_assert(std::is_same_v<
    decltype(&PartyQuestSkyrimNativeSaveProviderOwner::BeginTrackedAndInvoke),
    TNativeTrackedBeginAndInvoke>);
static_assert(!std::is_copy_constructible_v<
    PartyQuestSkyrimNativeSaveTrackedPollResult>);
static_assert(std::is_move_constructible_v<
    PartyQuestSkyrimNativeSaveTrackedPollResult>);
static_assert(std::is_same_v<
    decltype(PartyQuestSkyrimNativeSaveProviderBeginInvokeResult::
        NativeReservationAccepted),
    bool>);
using TNativeShutdown = PartyQuestSkyrimNativeSaveProviderOwnerStatus (
    PartyQuestSkyrimNativeSaveProviderOwner::*)() noexcept;
static_assert(std::is_same_v<
    decltype(&PartyQuestSkyrimNativeSaveProviderOwner::Shutdown),
    TNativeShutdown>);

TEST_CASE("Native save command capability defaults fail closed")
{
    PartyQuestSkyrimNativeSaveProviderPollCapability capability;
    REQUIRE_FALSE(capability.IsValid());
}

TEST_CASE("Accepted native reservation remains drain-owned after engine rejection",
    "[quest.party-state][native-save-provider][tracked-save]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = TrackedIdentity();

    REQUIRE(gate.BeginCoordinated(
        contract, identity, 100u, false, false).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);
    PartyQuestSkyrimNativeSaveProviderBeginInvokeResult native;
    native.Status = PartyQuestSkyrimNativeSaveProviderCommandStatus::Accepted;
    native.EngineInvocationAttempted = true;
    native.EngineInvocationSucceeded = false;
    native.NativeReservationAccepted = true;

    const auto admitted = lifecycle.ObserveReservation(identity,
        native.NativeReservationAccepted ?
            PartyQuestAsyncSaveReservationOutcome::Accepted :
            PartyQuestAsyncSaveReservationOutcome::Rejected);
    REQUIRE(admitted.DrainRequired);
    REQUIRE_FALSE(admitted.SafeToInvalidate);
    REQUIRE(lifecycle.CloseAdmission().CancelRequired);
    REQUIRE(lifecycle.ObserveCancelRequested(identity).DrainRequired);
    REQUIRE(lifecycle.ObserveCompletion(identity,
        PartyQuestAsyncSavePhysicalOutcome::Failed).DrainRequired);
    const auto retired = lifecycle.ObserveRetirement(identity);
    REQUIRE(retired.SafeToInvalidate);
    REQUIRE_FALSE(retired.ConsumptionAuthorized);
}

TEST_CASE("Tracked native save stale retirement cannot release the active request",
    "[quest.party-state][native-save-provider][tracked-save]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto first = TrackedIdentity();
    const auto second = TrackedIdentity(2u);
    REQUIRE(lifecycle.ObserveReservation(first,
        PartyQuestAsyncSaveReservationOutcome::Accepted).DrainRequired);
    REQUIRE(lifecycle.ObserveRetirement(second).Status ==
            PartyQuestAsyncSaveLifecycleStatus::Stale);
    REQUIRE_FALSE(lifecycle.ObserveRetirement(second).SafeToInvalidate);
    REQUIRE(lifecycle.ObserveCompletion(first,
        PartyQuestAsyncSavePhysicalOutcome::Succeeded).DrainRequired);
    REQUIRE(lifecycle.ObserveRetirement(first).ConsumptionAuthorized);
    REQUIRE(lifecycle.ObserveReservation(second,
        PartyQuestAsyncSaveReservationOutcome::Accepted).DrainRequired);
}

TEST_CASE("Native save provider descriptor is exact and fail closed",
    "[quest.party-state][native-save-provider]")
{
    const auto valid = Descriptor();
    REQUIRE(PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(valid));

    SECTION("descriptor ABI mismatch")
    {
        auto invalid = valid;
        ++invalid.AbiVersion;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("descriptor size mismatch")
    {
        auto invalid = valid;
        --invalid.StructSize;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("different event ABI")
    {
        auto invalid = valid;
        ++invalid.EventAbiVersion;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("different implementation version")
    {
        auto invalid = valid;
        ++invalid.ImplementationVersion;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("partial callback capability set")
    {
        auto invalid = valid;
        invalid.Capabilities &= ~static_cast<uint64_t>(
            PartyQuestNativeSaveProviderCapabilityBit::RequestRetirement);
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("required capabilities plus unknown bit")
    {
        auto invalid = valid;
        invalid.Capabilities |= 1ull << 63u;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("runtime major mismatch")
    {
        auto invalid = valid;
        ++invalid.RuntimeMajor;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("runtime minor mismatch")
    {
        auto invalid = valid;
        ++invalid.RuntimeMinor;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("runtime patch mismatch")
    {
        auto invalid = valid;
        ++invalid.RuntimePatch;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("runtime build mismatch")
    {
        auto invalid = valid;
        ++invalid.RuntimeBuild;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("different provider identity")
    {
        auto invalid = valid;
        ++invalid.ProviderFingerprint;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("nonzero reserved0")
    {
        auto invalid = valid;
        invalid.Reserved0 = 1;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("nonzero reserved1")
    {
        auto invalid = valid;
        invalid.Reserved1 = 1;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
}

TEST_CASE("Research native save descriptor is disjoint from production",
    "[quest.party-state][native-save-provider][research]")
{
    const auto production = Descriptor();
    const auto research = ResearchDescriptor();

    REQUIRE(PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(
        production));
    REQUIRE(PartyQuestNativeSaveProviderPolicy::IsApprovedBuildDescriptor(
        production));
    REQUIRE_FALSE(
        PartyQuestNativeSaveProviderPolicy::IsApprovedTask07ResearchDescriptor(
            production));
    REQUIRE_FALSE(PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(
        research));
    REQUIRE_FALSE(PartyQuestNativeSaveProviderPolicy::IsApprovedBuildDescriptor(
        research));
    REQUIRE(
        PartyQuestNativeSaveProviderPolicy::IsApprovedTask07ResearchDescriptor(
            research));

    SECTION("research marker is exact")
    {
        auto invalid = research;
        ++invalid.Reserved0;
        REQUIRE_FALSE(PartyQuestNativeSaveProviderPolicy::
                IsApprovedTask07ResearchDescriptor(invalid));
    }
    SECTION("research fingerprint is exact")
    {
        auto invalid = research;
        ++invalid.ProviderFingerprint;
        REQUIRE_FALSE(PartyQuestNativeSaveProviderPolicy::
                IsApprovedTask07ResearchDescriptor(invalid));
    }
    SECTION("reserved tail remains zero")
    {
        auto invalid = research;
        invalid.Reserved1 = 1u;
        REQUIRE_FALSE(PartyQuestNativeSaveProviderPolicy::
                IsApprovedTask07ResearchDescriptor(invalid));
    }
}

TEST_CASE("Native save provider registration is generation and instance bound",
    "[quest.party-state][native-save-provider]")
{
    PartyQuestNativeSaveProviderRegistration registry;
    auto registered = registry.RegisterAuthenticated(Descriptor(), 10);
    REQUIRE(registered.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Registered);
    REQUIRE(registered.Token);
    REQUIRE(registry.Validate(*registered.Token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);
    REQUIRE(registry.Validate(*registered.Token, 11) ==
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidGeneration);

    const auto duplicate = registry.RegisterAuthenticated(Descriptor(), 10);
    REQUIRE(duplicate.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Busy);
    REQUIRE_FALSE(duplicate.Token);
    REQUIRE(registry.Validate(*registered.Token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);

    auto token = std::move(*registered.Token);
    REQUIRE_FALSE(registered.Token->IsValid());
    REQUIRE(token.IsValid());
    REQUIRE(registry.Validate(*registered.Token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Stale);
    REQUIRE(registry.Invalidate(token) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);
    REQUIRE(registry.Validate(token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);

    auto replacement = registry.RegisterAuthenticated(Descriptor(), 10);
    REQUIRE(replacement.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Registered);
    REQUIRE(replacement.Token);
    REQUIRE(registry.Validate(token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Stale);
    REQUIRE(registry.Validate(*replacement.Token, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);
}

TEST_CASE("Native save provider rejected admission cannot mutate active authority",
    "[quest.party-state][native-save-provider][admission]")
{
    PartyQuestNativeSaveProviderRegistration registry;
    auto registered = registry.RegisterAuthenticated(Descriptor(), 42);
    REQUIRE(registered.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Registered);
    REQUIRE(registered.Token);

    auto invalidDescriptor = Descriptor();
    invalidDescriptor.ProviderFingerprint ^= 1ull;
    const auto rejectedDescriptor =
        registry.RegisterAuthenticated(invalidDescriptor, 42);
    REQUIRE(rejectedDescriptor.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor);
    REQUIRE_FALSE(rejectedDescriptor.Token);
    REQUIRE(registry.Validate(*registered.Token, 42) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);

    const auto rejectedGeneration =
        registry.RegisterAuthenticated(Descriptor(), 0);
    REQUIRE(rejectedGeneration.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidGeneration);
    REQUIRE_FALSE(rejectedGeneration.Token);
    REQUIRE(registry.Validate(*registered.Token, 42) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);

    const auto busy = registry.RegisterAuthenticated(Descriptor(), 42);
    REQUIRE(busy.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Busy);
    REQUIRE_FALSE(busy.Token);
    REQUIRE(registry.Validate(*registered.Token, 42) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);
}

TEST_CASE("Native save provider accepts maximum nonzero runtime generation",
    "[quest.party-state][native-save-provider][generation]")
{
    constexpr uint64_t generation = std::numeric_limits<uint64_t>::max();

    PartyQuestNativeSaveProviderRegistration registry;
    auto registered = registry.RegisterAuthenticated(Descriptor(), generation);
    REQUIRE(registered.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Registered);
    REQUIRE(registered.Token);
    REQUIRE(registered.Token->GetRuntimeGeneration() == generation);
    REQUIRE(registry.Validate(*registered.Token, generation) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Current);
}

TEST_CASE("Native save provider registration rejects ABA tokens across repeated replacement",
    "[quest.party-state][native-save-provider][aba]")
{
    PartyQuestNativeSaveProviderRegistration registry;
    constexpr uint64_t generation = 77;

    auto first = registry.RegisterAuthenticated(Descriptor(), generation);
    REQUIRE(first.Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::Registered);
    REQUIRE(first.Token);
    auto stale = std::move(*first.Token);

    for (size_t cycle = 0; cycle < 4; ++cycle)
    {
        REQUIRE(registry.Invalidate(stale) ==
                PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);
        REQUIRE(registry.Validate(stale, generation) ==
                PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);

        auto replacement =
            registry.RegisterAuthenticated(Descriptor(), generation);
        REQUIRE(replacement.Status ==
                PartyQuestNativeSaveProviderRegistrationStatus::Registered);
        REQUIRE(replacement.Token);
        REQUIRE(registry.Validate(stale, generation) ==
                PartyQuestNativeSaveProviderRegistrationStatus::Stale);
        REQUIRE(registry.Validate(*replacement.Token, generation) ==
                PartyQuestNativeSaveProviderRegistrationStatus::Current);

        stale = std::move(*replacement.Token);
        REQUIRE_FALSE(replacement.Token->IsValid());
        REQUIRE(stale.IsValid());
    }
}

TEST_CASE("Native save provider registration rejects invalid admission",
    "[quest.party-state][native-save-provider]")
{
    PartyQuestNativeSaveProviderRegistration registry;
    auto invalid = Descriptor();
    invalid.Capabilities = 0;
    REQUIRE(registry.RegisterAuthenticated(invalid, 10).Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor);
    REQUIRE(registry.RegisterAuthenticated(Descriptor(), 0).Status ==
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidGeneration);

    PartyQuestNativeSaveProviderToken empty;
    REQUIRE(registry.Validate(empty, 10) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);
    REQUIRE(registry.Invalidate(empty) ==
            PartyQuestNativeSaveProviderRegistrationStatus::Invalidated);
}
