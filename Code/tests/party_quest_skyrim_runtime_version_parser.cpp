#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <catch2/catch.hpp>

TEST_CASE("Skyrim runtime version parser accepts only four decimal components", "[quest.party-state.quiescence][runtime-profile][runtime-version]")
{
    PartyQuestSkyrimRuntimeVersion parsed{};
    REQUIRE(PartyQuestSkyrimRuntimeVersion::TryParse("1.6.1170.0", parsed));
    REQUIRE(parsed.Major == 1);
    REQUIRE(parsed.Minor == 6);
    REQUIRE(parsed.Patch == 1170);
    REQUIRE(parsed.Build == 0);

    const PartyQuestSkyrimRuntimeVersion sentinel{7, 7, 7, 7};

    for (const auto malformed : {
             "",
             "1",
             "1.6",
             "1.6.1170",
             "1.6.1170.0.1",
             "1.6.1170.",
             ".1.6.1170",
             "1..1170.0",
             " 1.6.1170.0",
             "1.6.1170.0 ",
             "+1.6.1170.0",
             "1.-6.1170.0",
             "1.6.a1170.0",
             "0.6.1170.0",
             "1.6.4294967296.0"})
    {
        parsed = sentinel;
        REQUIRE_FALSE(PartyQuestSkyrimRuntimeVersion::TryParse(malformed, parsed));
        REQUIRE(parsed.Matches(sentinel));
    }
}

TEST_CASE("Skyrim runtime version parser normalizes numeric identity without granting authority", "[quest.party-state.quiescence][runtime-profile][runtime-version]")
{
    PartyQuestSkyrimRuntimeVersion parsed{};
    REQUIRE(PartyQuestSkyrimRuntimeVersion::TryParse("01.006.001170.000", parsed));
    REQUIRE(parsed.Matches({1, 6, 1170, 0}));

    // Parsed public data is deliberately insufficient to construct the
    // process-local runtime-identity authorization.
    PartyQuestSkyrimRuntimeIdentityAuthorization authorization;
    REQUIRE_FALSE(authorization.IsVerified());
}
