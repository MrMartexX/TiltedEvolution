#include <catch2/catch.hpp>

#include <Structs/Skyrim/PartyQuestModMappingPublication.h>

TEST_CASE("PartyQuest mod mapping publication remains fail closed until commit")
{
    PartyQuestModMappingPublication publication;

    REQUIRE_FALSE(publication.IsReady());

    publication.BeginRebuild();
    REQUIRE_FALSE(publication.IsReady());

    // A failed/abandoned candidate rebuild performs no Commit().
    REQUIRE_FALSE(publication.IsReady());

    publication.Commit();
    REQUIRE(publication.IsReady());

    // The next rebuild revokes the previously published mapping immediately.
    publication.BeginRebuild();
    REQUIRE_FALSE(publication.IsReady());
}
