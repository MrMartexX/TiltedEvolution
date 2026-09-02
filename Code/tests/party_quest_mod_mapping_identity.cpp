#include <catch2/catch.hpp>

#include <Structs/Skyrim/PartyQuestModMappingIdentity.h>

TEST_CASE("PartyQuest standard mod mapping distinguishes empty slots from server id zero")
{
    PartyQuestStandardModMapping mapping;
    uint32_t serverId = 99;

    REQUIRE_FALSE(mapping.TryGet(1, serverId));
    REQUIRE(serverId == 99);

    REQUIRE(mapping.TryInsert(1, 0));
    REQUIRE(mapping.TryGet(1, serverId));
    REQUIRE(serverId == 0);
}

TEST_CASE("PartyQuest mod mapping candidate rejects duplicate server ids")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(7, false, true, 1, false) ==
            PartyQuestModMappingIdentityResult::Accepted);
    REQUIRE(candidate.Observe(7, true, true, 2, true) ==
            PartyQuestModMappingIdentityResult::DuplicateServerId);
}

TEST_CASE("PartyQuest mod mapping candidate rejects duplicate standard local slots")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(1, false, true, 4, false) ==
            PartyQuestModMappingIdentityResult::Accepted);
    REQUIRE(candidate.Observe(2, false, true, 4, false) ==
            PartyQuestModMappingIdentityResult::DuplicateStandardLocalSlot);
}

TEST_CASE("PartyQuest mod mapping candidate rejects duplicate lite local slots")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(1, true, true, 0x321, true) ==
            PartyQuestModMappingIdentityResult::Accepted);
    REQUIRE(candidate.Observe(2, true, true, 0x321, true) ==
            PartyQuestModMappingIdentityResult::DuplicateLiteLocalSlot);
}

TEST_CASE("PartyQuest mod mapping candidate rejects server and local type mismatch")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(1, false, true, 3, true) ==
            PartyQuestModMappingIdentityResult::ServerLocalTypeMismatch);
    REQUIRE(candidate.Observe(2, true, true, 4, false) ==
            PartyQuestModMappingIdentityResult::ServerLocalTypeMismatch);
}

TEST_CASE("PartyQuest mod mapping candidate accepts non-conflicting standard and lite identities")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(0, false, true, 2, false) ==
            PartyQuestModMappingIdentityResult::Accepted);
    REQUIRE(candidate.Observe(8, true, true, 0x456, true) ==
            PartyQuestModMappingIdentityResult::Accepted);

    uint32_t serverId = 99;
    REQUIRE(candidate.GetStandardMapping().TryGet(2, serverId));
    REQUIRE(serverId == 0);
    REQUIRE(candidate.IsLiteSlotOccupied(0x456));
}

TEST_CASE("PartyQuest missing local mods preserve partial candidate mapping")
{
    PartyQuestModMappingCandidateIdentity candidate;

    REQUIRE(candidate.Observe(3, false, false) ==
            PartyQuestModMappingIdentityResult::MissingLocalModSkipped);
    REQUIRE(candidate.Observe(3, false, true, 5, false) ==
            PartyQuestModMappingIdentityResult::Accepted);

    uint32_t serverId = 0;
    REQUIRE(candidate.GetStandardMapping().TryGet(5, serverId));
    REQUIRE(serverId == 3);
}
