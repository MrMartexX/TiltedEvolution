#include <Systems/AnimationReplayReset.h>
#include <Game/Animation/ActionReplayCache.h>
#include <catch2/catch.hpp>

TEST_CASE("Exit-only replay resets the graph without a queued action", "[actions.replay-reset]")
{
    ActionReplayCache cache;
    ActionEvent exit;
    exit.EventName = "idleReplayRegressionExit";
    cache.AppendAll({exit});
    const auto replay = cache.FormRefinedReplayChain();
    REQUIRE(replay.ResetAnimationGraph);
    REQUIRE(replay.Actions.empty());

    bool pending = replay.ResetAnimationGraph;
    unsigned resets = 0;
    REQUIRE(TryApplyAnimationReplayReset(pending, [] { return true; }, [&] { ++resets; return true; }));
    REQUIRE_FALSE(pending);
    REQUIRE(resets == 1);
    REQUIRE(replay.Actions.empty());

    REQUIRE(TryApplyAnimationReplayReset(pending, [] { FAIL("Consumed reset must not query the graph"); return false; }, [&] { ++resets; return true; }));
    REQUIRE(resets == 1);
}

TEST_CASE("Replay reset waits for graph readiness without losing the request", "[actions.replay-reset]")
{
    bool pending = true;
    unsigned resets = 0;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        REQUIRE_FALSE(TryApplyAnimationReplayReset(pending, [] { return false; }, [&] { ++resets; return true; }));
        REQUIRE(pending);
        REQUIRE(resets == 0);
    }
    REQUIRE(TryApplyAnimationReplayReset(pending, [] { return true; }, [&] { ++resets; return true; }));
    REQUIRE_FALSE(pending);
    REQUIRE(resets == 1);
}

TEST_CASE("Replay graph reset does not consume subsequent actions", "[actions.replay-reset]")
{
    ActionReplayCache cache;
    ActionEvent exit;
    exit.EventName = "idleReplayRegressionExit";
    ActionEvent next;
    next.EventName = "replayRegressionNext";
    next.Tick = 1234;
    cache.AppendAll({exit, next});
    const auto replay = cache.FormRefinedReplayChain();
    REQUIRE(replay.ResetAnimationGraph);
    REQUIRE(replay.Actions.size() == 1);
    REQUIRE(replay.Actions.front() == next);
    bool pending = replay.ResetAnimationGraph;
    REQUIRE(TryApplyAnimationReplayReset(pending, [] { return true; }, [&] {
        REQUIRE(pending);
        REQUIRE(replay.Actions.size() == 1);
        REQUIRE(replay.Actions.front() == next);
        return true;
    }));
    REQUIRE_FALSE(pending);
    REQUIRE(replay.Actions.front() == next);
}

TEST_CASE("Ordinary playback does not query or reset the graph through replay reset", "[actions.replay-reset]")
{
    bool pending = false;
    REQUIRE(TryApplyAnimationReplayReset(pending,
        [] { FAIL("No reset was requested"); return false; },
        [] { FAIL("Ordinary playback must not reset the graph"); return false; }));
    REQUIRE_FALSE(pending);
}

TEST_CASE("Failed replay graph reset retains the request for retry", "[actions.replay-reset]")
{
    bool pending = true;
    REQUIRE_FALSE(TryApplyAnimationReplayReset(pending, [] { return true; }, [] { return false; }));
    REQUIRE(pending);
    REQUIRE(TryApplyAnimationReplayReset(pending, [] { return true; }, [] { return true; }));
    REQUIRE_FALSE(pending);
}
