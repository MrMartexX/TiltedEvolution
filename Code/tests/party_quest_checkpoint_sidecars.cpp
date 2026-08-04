#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestCheckpointSidecarRequirement BuildRequirement(
    PartyQuestCheckpointSidecarRequirementMode aMode =
        PartyQuestCheckpointSidecarRequirementMode::Required)
{
    PartyQuestCheckpointSidecarRequirement requirement;
    requirement.CapabilityId = 0x5349444543415201ull;
    requirement.SchemaVersion = 3;
    requirement.ProviderFingerprint = 0x1111222233334444ull;
    requirement.RestoreAdapterFingerprint = 0x5555666677778888ull;
    requirement.Mode = aMode;
    return requirement;
}

PartyQuestCheckpointSidecarFacts BuildFacts()
{
    const auto requirement = BuildRequirement();

    PartyQuestCheckpointSidecarFacts facts;
    facts.CapabilityId = requirement.CapabilityId;
    facts.SchemaVersion = requirement.SchemaVersion;
    facts.ProviderFingerprint = requirement.ProviderFingerprint;
    facts.RestoreAdapterFingerprint = requirement.RestoreAdapterFingerprint;
    facts.CaptureAvailable = true;
    facts.RestoreAvailable = true;
    return facts;
}
} // namespace

TEST_CASE("Checkpoint sidecar authorization requires exact capture and restore capability", "[quest.party-state.sidecars]")
{
    const auto requirement = BuildRequirement();
    const auto facts = BuildFacts();

    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        requirement,
        &facts);
    REQUIRE(decision.Status == PartyQuestCheckpointSidecarStatus::Authorized);
    REQUIRE(decision.IsAuthorized());
    REQUIRE(decision.IsSatisfied());
    REQUIRE(decision.Authorization.GetCapabilityId() == requirement.CapabilityId);
    REQUIRE(decision.Authorization.GetSchemaVersion() == requirement.SchemaVersion);
    REQUIRE(decision.Authorization.GetProviderFingerprint() == requirement.ProviderFingerprint);
    REQUIRE(decision.Authorization.GetRestoreAdapterFingerprint() ==
        requirement.RestoreAdapterFingerprint);
}

TEST_CASE("Missing required sidecar capability blocks checkpoint coverage", "[quest.party-state.sidecars]")
{
    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        BuildRequirement(PartyQuestCheckpointSidecarRequirementMode::Required),
        nullptr);

    REQUIRE(decision.Status == PartyQuestCheckpointSidecarStatus::RequiredUnavailable);
    REQUIRE_FALSE(decision.IsAuthorized());
    REQUIRE_FALSE(decision.IsSatisfied());
}

TEST_CASE("Missing optional sidecar capability does not produce authorization", "[quest.party-state.sidecars]")
{
    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        BuildRequirement(PartyQuestCheckpointSidecarRequirementMode::Optional),
        nullptr);

    REQUIRE(decision.Status == PartyQuestCheckpointSidecarStatus::OptionalUnavailable);
    REQUIRE_FALSE(decision.IsAuthorized());
    REQUIRE(decision.IsSatisfied());
}

TEST_CASE("Sidecar capture without restore support is fail closed", "[quest.party-state.sidecars]")
{
    const auto requirement = BuildRequirement();
    auto facts = BuildFacts();
    facts.RestoreAvailable = false;

    const auto decision = PartyQuestCheckpointSidecarPolicy::Evaluate(
        requirement,
        &facts);
    REQUIRE(decision.Status == PartyQuestCheckpointSidecarStatus::RestoreUnavailable);
    REQUIRE_FALSE(decision.IsAuthorized());
    REQUIRE_FALSE(decision.IsSatisfied());
}

TEST_CASE("Sidecar capability evidence must match exact schema and fingerprints", "[quest.party-state.sidecars]")
{
    const auto requirement = BuildRequirement();

    SECTION("schema")
    {
        auto facts = BuildFacts();
        ++facts.SchemaVersion;
        REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(requirement, &facts).Status ==
            PartyQuestCheckpointSidecarStatus::SchemaVersionMismatch);
    }

    SECTION("provider")
    {
        auto facts = BuildFacts();
        ++facts.ProviderFingerprint;
        REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(requirement, &facts).Status ==
            PartyQuestCheckpointSidecarStatus::ProviderMismatch);
    }

    SECTION("restore adapter")
    {
        auto facts = BuildFacts();
        ++facts.RestoreAdapterFingerprint;
        REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(requirement, &facts).Status ==
            PartyQuestCheckpointSidecarStatus::RestoreAdapterMismatch);
    }

    SECTION("capability")
    {
        auto facts = BuildFacts();
        ++facts.CapabilityId;
        REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(requirement, &facts).Status ==
            PartyQuestCheckpointSidecarStatus::CapabilityMismatch);
    }
}

TEST_CASE("Zero or incomplete sidecar evidence cannot authorize a checkpoint", "[quest.party-state.sidecars]")
{
    auto invalidRequirement = BuildRequirement();
    invalidRequirement.RestoreAdapterFingerprint = 0;
    REQUIRE_FALSE(PartyQuestCheckpointSidecarPolicy::IsValidRequirement(invalidRequirement));
    REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(
                invalidRequirement,
                nullptr).Status ==
        PartyQuestCheckpointSidecarStatus::InvalidRequirement);

    const auto requirement = BuildRequirement();
    auto invalidFacts = BuildFacts();
    invalidFacts.ProviderFingerprint = 0;
    REQUIRE_FALSE(PartyQuestCheckpointSidecarPolicy::IsValidFacts(invalidFacts));
    REQUIRE(PartyQuestCheckpointSidecarPolicy::Evaluate(
                requirement,
                &invalidFacts).Status ==
        PartyQuestCheckpointSidecarStatus::InvalidFacts);
}

TEST_CASE("Checkpoint sidecar manifest rejects duplicate capability requirements", "[quest.party-state.sidecars]")
{
    PartyQuestCheckpointSidecarManifest manifest;
    const auto requirement = BuildRequirement();

    REQUIRE(manifest.AddRequirement(requirement));
    REQUIRE_FALSE(manifest.AddRequirement(requirement));
    REQUIRE(manifest.GetRequirementCount() == 1);
    REQUIRE(manifest.FindRequirement(requirement.CapabilityId) != nullptr);
    REQUIRE(*manifest.FindRequirement(requirement.CapabilityId) == requirement);
    REQUIRE(manifest.FindRequirement(0xDEADBEEFull) == nullptr);
}
