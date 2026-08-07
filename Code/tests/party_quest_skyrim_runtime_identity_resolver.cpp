#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>

#include <catch2/catch.hpp>

TEST_CASE("Skyrim runtime identity requires exact trusted startup and VersionDb agreement", "[quest.party-state.quiescence][runtime-profile][runtime-identity]")
{
    const auto exact =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeIdentityForTesting(
            1, 6, 1170, 0,
            true,
            1, 6, 1170, 0,
            true);
    REQUIRE(exact.IsVerified());
    REQUIRE(exact.GetRuntimeVersion().Major == 1);
    REQUIRE(exact.GetRuntimeVersion().Minor == 6);
    REQUIRE(exact.GetRuntimeVersion().Patch == 1170);
    REQUIRE(exact.GetRuntimeVersion().Build == 0);

    const auto executableNotMapped =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeIdentityForTesting(
            1, 6, 1170, 0,
            false,
            1, 6, 1170, 0,
            true);
    REQUIRE_FALSE(executableNotMapped.IsVerified());

    const auto versionDbNotLoaded =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeIdentityForTesting(
            1, 6, 1170, 0,
            true,
            1, 6, 1170, 0,
            false);
    REQUIRE_FALSE(versionDbNotLoaded.IsVerified());

    const auto mismatchedVersion =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeIdentityForTesting(
            1, 6, 1170, 0,
            true,
            1, 6, 1179, 0,
            true);
    REQUIRE_FALSE(mismatchedVersion.IsVerified());

    const auto missingIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeIdentityForTesting(
            0, 0, 0, 0,
            true,
            0, 0, 0, 0,
            true);
    REQUIRE_FALSE(missingIdentity.IsVerified());
}

TEST_CASE("Skyrim runtime version parsing is strict and non-partial", "[quest.party-state.quiescence][runtime-profile][runtime-identity]")
{
    PartyQuestSkyrimRuntimeVersion parsed{9, 9, 9, 9};
    REQUIRE(PartyQuestSkyrimRuntimeVersion::TryParse("1.6.1170.0", parsed));
    REQUIRE(parsed.Major == 1);
    REQUIRE(parsed.Minor == 6);
    REQUIRE(parsed.Patch == 1170);
    REQUIRE(parsed.Build == 0);

    const PartyQuestSkyrimRuntimeVersion sentinel{7, 7, 7, 7};

    parsed = sentinel;
    REQUIRE_FALSE(PartyQuestSkyrimRuntimeVersion::TryParse("1.6.1170", parsed));
    REQUIRE(parsed.Matches(sentinel));

    parsed = sentinel;
    REQUIRE_FALSE(PartyQuestSkyrimRuntimeVersion::TryParse("1.6.1170.0-extra", parsed));
    REQUIRE(parsed.Matches(sentinel));

    parsed = sentinel;
    REQUIRE_FALSE(PartyQuestSkyrimRuntimeVersion::TryParse("1.6.4294967296.0", parsed));
    REQUIRE(parsed.Matches(sentinel));

    parsed = sentinel;
    REQUIRE_FALSE(PartyQuestSkyrimRuntimeVersion::TryParse("0.0.0.0", parsed));
    REQUIRE(parsed.Matches(sentinel));
}
