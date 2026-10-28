#include <Structs/ActionEventsAdmission.h>
#include <Game/Animation/ActionReplayCache.h>
#include <catch2/catch.hpp>

TEST_CASE("Rejected server actions cannot reappear through spawn replay", "[actions.admission]")
{
    ActionReplayCache cache;
    TiltedPhoques::Vector<ActionEvent> incoming(3);
    incoming[0].EventName = "intake-accepted-one";
    incoming[1].EventName = "intake-canceled";
    incoming[1].Type = 1;
    incoming[2].EventName = "intake-accepted-two";
    size_t decisions = 0;
    const auto relay = AcceptAndCacheActionEvents(incoming, [&](const ActionEvent& aAction) {
        ++decisions;
        return aAction.Type != 1;
    }, cache);
    REQUIRE(decisions == 3);
    REQUIRE(relay.size() == 2);
    REQUIRE(cache.GetActions() == relay);
    const auto replay = cache.FormRefinedReplayChain();
    REQUIRE(replay.Actions == relay);
    for (const auto& action : replay.Actions)
        REQUIRE(action.Type != 1);
}
