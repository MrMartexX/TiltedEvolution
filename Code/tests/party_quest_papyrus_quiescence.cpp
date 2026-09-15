#include <Structs/Skyrim/PartyQuestPapyrusQuiescence.h>

#include <catch2/catch.hpp>

#include <utility>

TEST_CASE("Papyrus quiescence requires consecutive empty samples with stable event generation", "[quest.party-state.quiescence]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(1001));
    REQUIRE_FALSE(tracker.Authorize().has_value());

    REQUIRE(tracker.Observe(1001, 0, 10) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.GetStableSamples() == 1);
    REQUIRE_FALSE(tracker.IsQuiescent());
    REQUIRE_FALSE(tracker.Authorize().has_value());

    REQUIRE(tracker.Observe(1001, 0, 10) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    REQUIRE(tracker.GetStableSamples() == 2);
    REQUIRE(tracker.IsQuiescent());

    auto authorization = tracker.Authorize();
    REQUIRE(authorization.has_value());
    REQUIRE(authorization->IsVerified());
    REQUIRE(authorization->GetTransactionId() == 1001);
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

TEST_CASE("Quiescence authorization becomes stale after any later valid observation", "[quest.party-state.quiescence][authorization]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(4001));
    REQUIRE(tracker.Observe(4001, 0, 40) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(4001, 0, 40) == PartyQuestPapyrusQuiescenceStatus::Quiescent);

    auto stale = tracker.Authorize();
    REQUIRE(stale.has_value());

    // Even an unchanged later observation advances the observation revision.
    REQUIRE(tracker.Observe(4001, 0, 40) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto current = tracker.Authorize();
    REQUIRE(current.has_value());

    REQUIRE_FALSE(tracker.Consume(std::move(*stale)));
    REQUIRE_FALSE(stale->IsVerified());
    REQUIRE(tracker.IsQuiescent());
    REQUIRE(tracker.Consume(std::move(*current)));
    REQUIRE_FALSE(current->IsVerified());
    REQUIRE(tracker.GetTransactionId() == 0);
    REQUIRE_FALSE(tracker.IsQuiescent());
}

TEST_CASE("New work after authorization invalidates proof even after restabilization", "[quest.party-state.quiescence][authorization]")
{
    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(4101));
    REQUIRE(tracker.Observe(4101, 0, 50) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(4101, 0, 50) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto stale = tracker.Authorize();
    REQUIRE(stale.has_value());

    REQUIRE(tracker.Observe(4101, 2, 51) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(4101, 0, 51) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(4101, 0, 51) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto current = tracker.Authorize();
    REQUIRE(current.has_value());

    REQUIRE_FALSE(tracker.Consume(std::move(*stale)));
    REQUIRE(tracker.Consume(std::move(*current)));
}

TEST_CASE("Quiescence authorization is tracker-session scoped and one shot", "[quest.party-state.quiescence][authorization]")
{
    PartyQuestPapyrusQuiescenceTracker first;
    REQUIRE(first.Begin(4201));
    REQUIRE(first.Observe(4201, 0, 60) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(first.Observe(4201, 0, 60) == PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto authorization = first.Authorize();
    REQUIRE(authorization.has_value());

    PartyQuestPapyrusQuiescenceTracker second;
    REQUIRE(second.Begin(4201));
    REQUIRE(second.Observe(4201, 0, 60) == PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(second.Observe(4201, 0, 60) == PartyQuestPapyrusQuiescenceStatus::Quiescent);

    REQUIRE_FALSE(second.Consume(std::move(*authorization)));
    REQUIRE_FALSE(authorization->IsVerified());

    auto secondAuthorization = second.Authorize();
    REQUIRE(secondAuthorization.has_value());
    REQUIRE(second.Consume(std::move(*secondAuthorization)));
    REQUIRE_FALSE(second.Consume(std::move(*secondAuthorization)));
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