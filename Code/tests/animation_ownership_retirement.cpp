#include <Systems/AnimationOwnershipReplay.h>
#include <Systems/AnimationReplayReset.h>
#include <Structs/ActionEvent.h>
#include <TiltedCore/Stl.hpp>
#include <catch2/catch.hpp>

TEST_CASE("New ownership retires future playback and its pending graph reset", "[actions.ownership]")
{
    ActionEvent old;
    old.Tick = 999999;
    old.EventName = "old-owner-pending";
    TiltedPhoques::List<ActionEvent> queue{old};
    uint32_t replayCount = 1;
    bool pending = true;

    REQUIRE(RetireAnimationReplayForOwnershipChange(7, 8, queue, replayCount, pending));
    REQUIRE(queue.empty());
    REQUIRE(replayCount == 0);
    REQUIRE_FALSE(pending);
    REQUIRE(TryApplyAnimationReplayReset(pending,
        [] { FAIL("Old reset must not query the graph after ownership changed"); return false; },
        [] { FAIL("Old owner must not reset the new owner's graph"); return true; }));

    ActionEvent fresh;
    fresh.Tick = 42;
    fresh.EventName = "new-owner-action";
    queue.push_back(fresh);
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.front() == fresh);
}

TEST_CASE("Rejected ownership epochs preserve every queued playback field", "[actions.ownership]")
{
    ActionEvent old;
    old.Tick = 123;
    old.EventName = "still-current-owner";
    const TiltedPhoques::List<ActionEvent> original{old};
    for (const uint32_t incoming : {0u, 6u, 7u})
    {
        auto queue = original;
        uint32_t replayCount = 1;
        bool pending = true;
        REQUIRE_FALSE(RetireAnimationReplayForOwnershipChange(7, incoming, queue, replayCount, pending));
        REQUIRE(queue == original);
        REQUIRE(replayCount == 1);
        REQUIRE(pending);
    }
}

TEST_CASE("Ownership change retires a reset-only replay", "[actions.ownership]")
{
    TiltedPhoques::List<ActionEvent> queue;
    uint32_t replayCount = 0;
    bool pending = true;
    REQUIRE(RetireAnimationReplayForOwnershipChange(1, 2, queue, replayCount, pending));
    REQUIRE(queue.empty());
    REQUIRE(replayCount == 0);
    REQUIRE_FALSE(pending);
}

TEST_CASE("Playback retirement handles an unknown epoch and consecutive handoffs", "[actions.ownership]")
{
    ActionEvent old;
    old.EventName = "old-action";
    TiltedPhoques::List<ActionEvent> queue{old};
    uint32_t replayCount = 1;
    bool pending = true;
    REQUIRE(RetireAnimationReplayForOwnershipChange(0, 1, queue, replayCount, pending));
    REQUIRE(queue.empty());
    REQUIRE_FALSE(pending);
    REQUIRE(replayCount == 0);
    REQUIRE(RetireAnimationReplayForOwnershipChange(1, 2, queue, replayCount, pending));
    REQUIRE(queue.empty());
    REQUIRE_FALSE(pending);
    REQUIRE(replayCount == 0);
}
