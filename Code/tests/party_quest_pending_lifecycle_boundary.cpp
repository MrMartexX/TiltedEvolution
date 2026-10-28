#include <Structs/Skyrim/PartyQuestPendingLifecycleBoundary.h>

#include <catch2/catch.hpp>

TEST_CASE("Pending lifecycle boundary is empty by default",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    const auto disposition = pending.Normalize();

    REQUIRE(pending.Empty());
    REQUIRE(disposition.Empty());
    REQUIRE_FALSE(disposition.ResetBootstrap);
    REQUIRE(disposition.Release == PartyQuestPendingSessionRelease::None);
}

TEST_CASE("Pending lifecycle boundary merges duplicates idempotently",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::Connected);
    const auto once = pending.Normalize();
    pending.Add(PartyQuestPendingLifecycleReason::Connected);
    const auto twice = pending.Normalize();

    REQUIRE(twice.Reasons == once.Reasons);
    REQUIRE(twice.Revision == once.Revision);
    REQUIRE(twice.ApplyConnectedBoundary);
    REQUIRE_FALSE(twice.ApplyPartyJoinedBoundary);
    REQUIRE(twice.ResetBootstrap);
}

TEST_CASE("Disconnect explicitly supersedes party leave",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::PartyLeft);
    pending.Add(PartyQuestPendingLifecycleReason::Disconnect);
    const auto disposition = pending.Normalize();

    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::PartyLeft));
    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::Disconnect));
    REQUIRE(disposition.Release == PartyQuestPendingSessionRelease::Disconnect);
}

TEST_CASE("Shutdown is terminal and suppresses bootstrap actions",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::Connected);
    pending.Add(PartyQuestPendingLifecycleReason::PartyJoined);
    pending.Add(PartyQuestPendingLifecycleReason::Shutdown);
    const auto disposition = pending.Normalize();

    REQUIRE(disposition.Shutdown);
    REQUIRE(disposition.Release == PartyQuestPendingSessionRelease::Shutdown);
    REQUIRE_FALSE(disposition.ApplyConnectedBoundary);
    REQUIRE_FALSE(disposition.ApplyPartyJoinedBoundary);
    REQUIRE(disposition.ResetBootstrap);
}

TEST_CASE("Campaign switch remains visible when disconnect wins release",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::CampaignSwitch);
    pending.Add(PartyQuestPendingLifecycleReason::Disconnect);
    const auto disposition = pending.Normalize();

    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::CampaignSwitch));
    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::Disconnect));
    REQUIRE(disposition.Release == PartyQuestPendingSessionRelease::Disconnect);
}

TEST_CASE("LoadGame remains a distinct blocking reason",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::LoadGame);
    pending.Add(PartyQuestPendingLifecycleReason::CampaignSwitch);
    pending.Add(PartyQuestPendingLifecycleReason::Connected);
    const auto disposition = pending.Normalize();

    REQUIRE(disposition.BlockForLoadGame);
    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::LoadGame));
    REQUIRE(disposition.Has(PartyQuestPendingLifecycleReason::CampaignSwitch));
    REQUIRE(disposition.Release ==
        PartyQuestPendingSessionRelease::CampaignSwitch);
    REQUIRE_FALSE(disposition.ApplyConnectedBoundary);
}

TEST_CASE("Later weak boundaries cannot weaken a pending release",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::Disconnect);
    pending.Add(PartyQuestPendingLifecycleReason::Connected);
    pending.Add(PartyQuestPendingLifecycleReason::PartyJoined);
    const auto disposition = pending.Normalize();

    REQUIRE(disposition.Release == PartyQuestPendingSessionRelease::Disconnect);
    REQUIRE_FALSE(disposition.ApplyConnectedBoundary);
    REQUIRE_FALSE(disposition.ApplyPartyJoinedBoundary);
}

TEST_CASE("Pending lifecycle reasons clear only after successful application",
          "[quest.party-state.pending-lifecycle]")
{
    PartyQuestPendingLifecycleBoundary pending;
    pending.Add(PartyQuestPendingLifecycleReason::PartyJoined);
    const auto attempted = pending.Normalize();

    // A failed attempt leaves the accumulator untouched.
    REQUIRE(pending.Normalize().ApplyPartyJoinedBoundary);
    REQUIRE(pending.Normalize().ApplyPartyJoinedBoundary);

    pending.Add(PartyQuestPendingLifecycleReason::LoadGame);
    REQUIRE_FALSE(pending.ClearAfterSuccessfulApplication(attempted));
    REQUIRE(pending.Has(PartyQuestPendingLifecycleReason::PartyJoined));
    REQUIRE(pending.Has(PartyQuestPendingLifecycleReason::LoadGame));

    const auto complete = pending.Normalize();
    REQUIRE(pending.ClearAfterSuccessfulApplication(complete));
    REQUIRE(pending.Empty());
    REQUIRE(pending.Normalize().Empty());
}
