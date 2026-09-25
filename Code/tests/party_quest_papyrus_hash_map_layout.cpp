#include <Structs/Skyrim/PartyQuestPapyrusHashMapLayout.h>

#include <catch2/catch.hpp>

TEST_CASE("Papyrus hash map accepts capacity as the initial free search cursor", "[quest.party-state.quiescence][papyrus-layout]")
{
    REQUIRE(IsPlausiblePartyQuestPapyrusHashMapHeader(2048, 2048, 2048));
    REQUIRE(IsPlausiblePartyQuestPapyrusHashMapHeader(2048, 2047, 2048));
}

TEST_CASE("Papyrus hash map rejects out-of-range header state", "[quest.party-state.quiescence][papyrus-layout]")
{
    REQUIRE_FALSE(IsPlausiblePartyQuestPapyrusHashMapHeader(2048, 2049, 2048));
    REQUIRE_FALSE(IsPlausiblePartyQuestPapyrusHashMapHeader(2048, 2048, 2049));
    REQUIRE_FALSE(IsPlausiblePartyQuestPapyrusHashMapHeader(2047, 0, 0));
    REQUIRE_FALSE(IsPlausiblePartyQuestPapyrusHashMapHeader(0, 0, 1));
}
