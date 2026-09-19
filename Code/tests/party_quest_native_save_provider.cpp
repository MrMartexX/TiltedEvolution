#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>

#include <catch2/catch.hpp>

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
}

static_assert(!std::is_copy_constructible_v<PartyQuestNativeSaveProviderToken>);
static_assert(!std::is_copy_assignable_v<PartyQuestNativeSaveProviderToken>);

TEST_CASE("Native save provider descriptor is exact and fail closed",
    "[quest.party-state][native-save-provider]")
{
    const auto valid = Descriptor();
    REQUIRE(PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(valid));

    SECTION("partial callback capability set")
    {
        auto invalid = valid;
        invalid.Capabilities &= ~static_cast<uint64_t>(
            PartyQuestNativeSaveProviderCapabilityBit::RequestRetirement);
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
    }
    SECTION("unknown capability")
    {
        auto invalid = valid;
        invalid.Capabilities |= 1ull << 63u;
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
    SECTION("unproven runtime")
    {
        auto invalid = valid;
        invalid.RuntimePatch = 1171;
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
    SECTION("nonzero reserved field")
    {
        auto invalid = valid;
        invalid.Reserved1 = 1;
        REQUIRE_FALSE(
            PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(invalid));
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
