#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

#include <catch2/catch.hpp>

TEST_CASE("Papyrus quiescence requires consecutive empty samples with stable event generation", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(1001));

    REQUIRE(tracker.Observe(1001, 0, 10) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.GetStableSamples() == 1);
    REQUIRE_FALSE(tracker.IsQuiescent());

    REQUIRE(tracker.Observe(1001, 0, 10) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    REQUIRE(tracker.GetStableSamples() == 2);
    REQUIRE(tracker.IsQuiescent());
}

TEST_CASE("Queued Papyrus work resets quiescence stability", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(2001));

    REQUIRE(tracker.Observe(2001, 0, 20) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(2001, 3, 20) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.GetStableSamples() == 0);

    REQUIRE(tracker.Observe(2001, 0, 20) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(2001, 0, 20) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
}

TEST_CASE("New quest events reset stable empty-queue observation", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(3001));

    REQUIRE(tracker.Observe(3001, 0, 30) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(3001, 0, 31) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.GetStableSamples() == 1);
    REQUIRE(tracker.Observe(3001, 0, 31) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
}

TEST_CASE("Quiescence can be invalidated if new work appears after a stable observation", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(4001));
    REQUIRE(tracker.Observe(4001, 0, 40) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(4001, 0, 40) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    REQUIRE(tracker.IsQuiescent());

    REQUIRE(tracker.Observe(4001, 1, 41) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE_FALSE(tracker.IsQuiescent());
    REQUIRE(tracker.GetStableSamples() == 0);
}

TEST_CASE("Papyrus quiescence tracker is transaction-scoped", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE_FALSE(tracker.Begin(0));
    REQUIRE(tracker.Begin(5001));
    REQUIRE_FALSE(tracker.Begin(5002));

    REQUIRE(tracker.Observe(5002, 0, 1) == PartyQuestPapyrusQuiescenceStatus::InvalidTransaction);
    REQUIRE_FALSE(tracker.Reset(5002));
    REQUIRE(tracker.Reset(5001));
    REQUIRE(tracker.GetTransactionId() == 0);
    REQUIRE_FALSE(tracker.IsQuiescent());
    REQUIRE(tracker.Begin(5002));
}
