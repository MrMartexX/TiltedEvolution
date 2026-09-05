#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestRuntimeCompatibilityRequirement BuildRequirement(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 4;
    requirement.ResolvedRecordFingerprint = 0x1111222233334444ull;
    requirement.WinningOverrideFingerprint = 0x2222333344445555ull;
    requirement.ScriptFingerprint = 0x3333444455556666ull;
    requirement.NativeAdapterFingerprint = 0x4444555566667777ull;
    requirement.AdapterMutationComponents = PartyQuestVerificationComponent::QuestSnapshot;
    return requirement;
}

PartyQuestRuntimeCompatibilityFacts BuildMatchingFacts(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = acRequirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = acRequirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = acRequirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = acRequirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = acRequirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = acRequirement.AdapterMutationComponents;
    return facts;
}
} // namespace

TEST_CASE("Runtime adapter compatibility is fail-closed for unknown quests", "[quest.party-state.runtime-compatibility]")
{
    PartyQuestRuntimeCompatibilityManifest manifest;
    const GameId knownQuest(21, 0x1000);
    const GameId unknownQuest(21, 0x2000);
    const auto requirement = BuildRequirement(knownQuest);
    REQUIRE(manifest.AddRequirement(requirement));

    const auto decision = manifest.Evaluate(unknownQuest, BuildMatchingFacts(requirement));
    REQUIRE(decision.Status == PartyQuestRuntimeCompatibilityStatus::UnknownQuest);
    REQUIRE_FALSE(decision.IsAuthorized());
    REQUIRE_FALSE(decision.SafetyProfile.HasVerifiedNativeAdapter());
}

TEST_CASE("Runtime adapter manifest rejects incomplete and duplicate requirements", "[quest.party-state.runtime-compatibility]")
{
    PartyQuestRuntimeCompatibilityManifest manifest;

    auto invalid = BuildRequirement(GameId(22, 0x1000));
    invalid.ScriptFingerprint = 0;
    REQUIRE_FALSE(PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(invalid));
    REQUIRE_FALSE(manifest.AddRequirement(invalid));

    auto missingCoverage = BuildRequirement(GameId(22, 0x1001));
    missingCoverage.AdapterMutationComponents = PartyQuestVerificationComponent::None;
    REQUIRE_FALSE(PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(missingCoverage));
    REQUIRE_FALSE(manifest.AddRequirement(missingCoverage));

    auto unsupportedCoverage = BuildRequirement(GameId(22, 0x1002));
    unsupportedCoverage.AdapterMutationComponents =
        PartyQuestVerificationComponent::InventoryEffects;
    REQUIRE_FALSE(PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(unsupportedCoverage));
    REQUIRE_FALSE(manifest.AddRequirement(unsupportedCoverage));

    const auto valid = BuildRequirement(GameId(22, 0x2000));
    REQUIRE(PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(valid));
    REQUIRE(manifest.AddRequirement(valid));
    REQUIRE_FALSE(manifest.AddRequirement(valid));
    REQUIRE(manifest.GetRequirementCount() == 1);
    REQUIRE(manifest.FindRequirement(valid.QuestId) != nullptr);
    REQUIRE(*manifest.FindRequirement(valid.QuestId) == valid);
}

TEST_CASE("Exact runtime compatibility fingerprints authorize a native adapter token", "[quest.party-state.runtime-compatibility]")
{
    const auto requirement = BuildRequirement(GameId(23, 0x1000));
    const auto facts = BuildMatchingFacts(requirement);

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.Status == PartyQuestRuntimeCompatibilityStatus::Authorized);
    REQUIRE(decision.IsAuthorized());
    REQUIRE(decision.SafetyProfile.HasVerifiedNativeAdapter());
    REQUIRE(decision.SafetyProfile.GetAdapterMutationComponents() ==
        PartyQuestVerificationComponent::QuestSnapshot);
}

TEST_CASE("Missing runtime compatibility evidence never authorizes mutation", "[quest.party-state.runtime-compatibility]")
{
    const auto requirement = BuildRequirement(GameId(24, 0x1000));
    PartyQuestRuntimeCompatibilityFacts facts;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.Status == PartyQuestRuntimeCompatibilityStatus::InvalidClientFacts);
    REQUIRE_FALSE(decision.IsAuthorized());
    REQUIRE_FALSE(decision.SafetyProfile.HasVerifiedNativeAdapter());
}

TEST_CASE("Runtime compatibility reports the first exact mismatch deterministically", "[quest.party-state.runtime-compatibility]")
{
    const auto requirement = BuildRequirement(GameId(25, 0x1000));

    SECTION("profile version")
    {
        auto facts = BuildMatchingFacts(requirement);
        ++facts.ProfileVersion;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::ProfileVersionMismatch);
    }

    SECTION("resolved record")
    {
        auto facts = BuildMatchingFacts(requirement);
        facts.ResolvedRecordFingerprint ^= 1ull;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::ResolvedRecordMismatch);
    }

    SECTION("winning override")
    {
        auto facts = BuildMatchingFacts(requirement);
        facts.WinningOverrideFingerprint ^= 1ull;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::WinningOverrideMismatch);
    }

    SECTION("scripts")
    {
        auto facts = BuildMatchingFacts(requirement);
        facts.ScriptFingerprint ^= 1ull;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::ScriptMismatch);
    }

    SECTION("native adapter")
    {
        auto facts = BuildMatchingFacts(requirement);
        facts.NativeAdapterFingerprint ^= 1ull;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::NativeAdapterMismatch);
    }

    SECTION("adapter mutation coverage")
    {
        auto facts = BuildMatchingFacts(requirement);
        facts.AdapterMutationComponents = PartyQuestVerificationComponent::WorldEffects;
        REQUIRE(PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts).Status ==
            PartyQuestRuntimeCompatibilityStatus::AdapterMutationCoverageMismatch);
    }
}

TEST_CASE("Manifest authorization is quest-scoped even when fingerprints are identical", "[quest.party-state.runtime-compatibility]")
{
    PartyQuestRuntimeCompatibilityManifest manifest;
    const GameId firstQuest(26, 0x1000);
    const GameId secondQuest(26, 0x2000);

    const auto firstRequirement = BuildRequirement(firstQuest);
    REQUIRE(manifest.AddRequirement(firstRequirement));

    const auto sameBinaryFacts = BuildMatchingFacts(firstRequirement);
    REQUIRE(manifest.Evaluate(firstQuest, sameBinaryFacts).IsAuthorized());

    const auto secondDecision = manifest.Evaluate(secondQuest, sameBinaryFacts);
    REQUIRE(secondDecision.Status == PartyQuestRuntimeCompatibilityStatus::UnknownQuest);
    REQUIRE_FALSE(secondDecision.IsAuthorized());
}
