#include <Structs/Skyrim/PartyQuestSkyrimEngineSaveIsolationPolicy.h>

#include <catch2/catch.hpp>

TEST_CASE("Skyrim engine PreRepair save isolation fails closed until asynchronous path capture is proven", "[quest.party-state.skyrim-save-isolation]")
{
    REQUIRE_FALSE(
        PartyQuestSkyrimEngineSaveIsolationPolicy::AllowsProductionCapture());
}
