#include <Structs/Skyrim/PartyQuestSkyrimSavePath.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kSavePathCampaign{
    0x0011223344556677ull,
    0x8899AABBCCDDEEFFull};
const PartyQuestPlayerProfileId kSavePathPlayer{
    0xFFEEDDCCBBAA9988ull,
    0x7766554433221100ull};
} // namespace

TEST_CASE("Skyrim co-op save path is deterministic and matches replica identity", "[quest.party-state.skyrim-save-path]")
{
    const std::string path = PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
        kSavePathCampaign,
        kSavePathPlayer);

    REQUIRE(path ==
        "CoopCampaigns\\"
        "Campaign_00112233445566778899AABBCCDDEEFF\\"
        "Player_FFEEDDCCBBAA99887766554433221100\\"
        "saves\\");
    REQUIRE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(path));
    REQUIRE(PartyQuestSkyrimSavePathPolicy::MatchesRelativeSavePath(
        path,
        kSavePathCampaign,
        kSavePathPlayer));
}

TEST_CASE("Invalid campaign or player cannot produce a Skyrim save path", "[quest.party-state.skyrim-save-path]")
{
    REQUIRE(PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
        {},
        kSavePathPlayer).empty());
    REQUIRE(PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
        kSavePathCampaign,
        {}).empty());
}

TEST_CASE("Skyrim co-op save path rejects traversal rooted and noncanonical forms", "[quest.party-state.skyrim-save-path]")
{
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(""));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "C:\\CoopCampaigns\\Campaign_00112233445566778899AABBCCDDEEFF\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "\\\\server\\share\\CoopCampaigns\\Campaign_00112233445566778899AABBCCDDEEFF\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns\\..\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns/Campaign_00112233445566778899AABBCCDDEEFF/Player_FFEEDDCCBBAA99887766554433221100/saves/"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns\\Campaign_00112233445566778899AABBCCDDEEFF\\Player_FFEEDDCCBBAA99887766554433221100\\saves"));
}

TEST_CASE("Skyrim co-op save path accepts only fixed-width uppercase identity components", "[quest.party-state.skyrim-save-path]")
{
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns\\Campaign_00112233445566778899aabbccddeeff\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns\\Campaign_00112233\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "CoopCampaigns\\Campaign_00112233445566778899AABBCCDDEEFF\\Player_FFEEDDCCBBAA9988776655443322110G\\saves\\"));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
        "OtherRoot\\Campaign_00112233445566778899AABBCCDDEEFF\\Player_FFEEDDCCBBAA99887766554433221100\\saves\\"));
}

TEST_CASE("Skyrim co-op save path does not match a different campaign or player", "[quest.party-state.skyrim-save-path]")
{
    const std::string path = PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
        kSavePathCampaign,
        kSavePathPlayer);

    const PartyQuestCampaignId otherCampaign{
        kSavePathCampaign.High,
        kSavePathCampaign.Low + 1};
    const PartyQuestPlayerProfileId otherPlayer{
        kSavePathPlayer.High,
        kSavePathPlayer.Low + 1};

    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::MatchesRelativeSavePath(
        path,
        otherCampaign,
        kSavePathPlayer));
    REQUIRE_FALSE(PartyQuestSkyrimSavePathPolicy::MatchesRelativeSavePath(
        path,
        kSavePathCampaign,
        otherPlayer));
}
