#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <catch2/catch.hpp>

#include <string>
#include <utility>

TEST_CASE("Co-op save layout isolates campaign and player replicas", "[quest.party-state.save-layout]")
{
    const PartyQuestCampaignId campaign{0x0123456789ABCDEFull, 0x1111222233334444ull};
    const PartyQuestPlayerProfileId player{0xAAAABBBBCCCCDDDDull, 0x5555666677778888ull};

    const auto paths = PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, player);
    REQUIRE(paths.has_value());

    REQUIRE(paths->Root == std::filesystem::path("CoopCampaigns"));
    REQUIRE(paths->CampaignDirectory.filename() ==
        "Campaign_0123456789ABCDEF1111222233334444");
    REQUIRE(paths->PlayerDirectory.filename() ==
        "Player_AAAABBBBCCCCDDDD5555666677778888");
    REQUIRE(paths->PlayerDirectory.parent_path() == paths->CampaignDirectory);
    REQUIRE(paths->CheckpointsDirectory == paths->PlayerDirectory / "checkpoints");
    REQUIRE(paths->SavesDirectory == paths->PlayerDirectory / "saves");
    REQUIRE(paths->SidecarsDirectory == paths->PlayerDirectory / "sidecars");
    REQUIRE(paths->MetadataDirectory == paths->PlayerDirectory / "metadata");
    REQUIRE(paths->RuntimeApplySidecar ==
        paths->SidecarsDirectory / "party_quest_runtime_apply.bin");
}

TEST_CASE("Co-op save layout identifiers are deterministic fixed-width path-safe hex", "[quest.party-state.save-layout]")
{
    const PartyQuestCampaignId campaign{1, 2};
    const PartyQuestPlayerProfileId player{3, 4};

    const std::string campaignText = PartyQuestCoopSaveLayout::FormatCampaignId(campaign);
    const std::string playerText = PartyQuestCoopSaveLayout::FormatPlayerProfileId(player);

    REQUIRE(campaignText == "00000000000000010000000000000002");
    REQUIRE(playerText == "00000000000000030000000000000004");
    REQUIRE(campaignText.size() == 32);
    REQUIRE(playerText.size() == 32);
    REQUIRE(campaignText.find('/') == std::string::npos);
    REQUIRE(campaignText.find('\\') == std::string::npos);
    REQUIRE(playerText.find('/') == std::string::npos);
    REQUIRE(playerText.find('\\') == std::string::npos);
}

TEST_CASE("Co-op save layout fails closed for missing identity or root", "[quest.party-state.save-layout]")
{
    const PartyQuestCampaignId campaign{1, 2};
    const PartyQuestPlayerProfileId player{3, 4};

    REQUIRE_FALSE(PartyQuestCoopSaveLayout::Build({}, campaign, player).has_value());
    REQUIRE_FALSE(PartyQuestCoopSaveLayout::Build("CoopCampaigns", {}, player).has_value());
    REQUIRE_FALSE(PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, {}).has_value());
    REQUIRE(PartyQuestCoopSaveLayout::FormatCampaignId({}).empty());
    REQUIRE(PartyQuestCoopSaveLayout::FormatPlayerProfileId({}).empty());
}

TEST_CASE("Co-op checkpoint names are explicit and stay inside the player checkpoint tree", "[quest.party-state.save-layout]")
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        "CoopCampaigns",
        PartyQuestCampaignId{0x10, 0x20},
        PartyQuestPlayerProfileId{0x30, 0x40});
    REQUIRE(paths.has_value());

    const std::pair<PartyQuestCheckpointKind, const char*> cases[] = {
        {PartyQuestCheckpointKind::PreJoin, "PreJoin"},
        {PartyQuestCheckpointKind::PreMigration, "PreMigration"},
        {PartyQuestCheckpointKind::PreRepair, "PreRepair"},
        {PartyQuestCheckpointKind::SessionStart, "SessionStart"},
        {PartyQuestCheckpointKind::LastKnownGood, "LastKnownGood"}
    };

    for (const auto& [kind, name] : cases)
    {
        REQUIRE(std::string(PartyQuestCoopSaveLayout::GetCheckpointName(kind)) == name);
        const auto checkpoint = PartyQuestCoopSaveLayout::GetCheckpointDirectory(*paths, kind);
        REQUIRE(checkpoint.parent_path() == paths->CheckpointsDirectory);
        REQUIRE(checkpoint.filename() == name);
    }
}

TEST_CASE("Checkpoint revisions have immutable deterministic subdirectories", "[quest.party-state.save-layout]")
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        "CoopCampaigns",
        PartyQuestCampaignId{0x10, 0x20},
        PartyQuestPlayerProfileId{0x30, 0x40});
    REQUIRE(paths.has_value());

    REQUIRE(PartyQuestCoopSaveLayout::FormatWorldRevision(0) == "Revision_0000000000000000");
    REQUIRE(PartyQuestCoopSaveLayout::FormatWorldRevision(410) == "Revision_000000000000019A");

    const auto first = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        410);
    const auto second = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        411);

    REQUIRE(first != second);
    REQUIRE(first.parent_path() ==
        PartyQuestCoopSaveLayout::GetCheckpointDirectory(*paths, PartyQuestCheckpointKind::PreRepair));
    REQUIRE(first.filename() == "Revision_000000000000019A");
    REQUIRE(second.filename() == "Revision_000000000000019B");
}

TEST_CASE("Different player profiles never share runtime sidecar paths", "[quest.party-state.save-layout]")
{
    const PartyQuestCampaignId campaign{0xAA, 0xBB};
    const PartyQuestPlayerProfileId playerA{0x01, 0x02};
    const PartyQuestPlayerProfileId playerB{0x03, 0x04};

    const auto a = PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, playerA);
    const auto b = PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, playerB);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->PlayerDirectory != b->PlayerDirectory);
    REQUIRE(a->RuntimeApplySidecar != b->RuntimeApplySidecar);
}

TEST_CASE("Different campaigns never share player replica roots", "[quest.party-state.save-layout]")
{
    const PartyQuestPlayerProfileId player{0x01, 0x02};

    const auto a = PartyQuestCoopSaveLayout::Build(
        "CoopCampaigns", PartyQuestCampaignId{0x11, 0x12}, player);
    const auto b = PartyQuestCoopSaveLayout::Build(
        "CoopCampaigns", PartyQuestCampaignId{0x21, 0x22}, player);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->CampaignDirectory != b->CampaignDirectory);
    REQUIRE(a->PlayerDirectory != b->PlayerDirectory);
}
