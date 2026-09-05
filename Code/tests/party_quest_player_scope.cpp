#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <catch2/catch.hpp>

TEST_CASE("Player-scoped runtime recovery identity matches the isolated replica path", "[quest.party-state.player-scope]")
{
    const PartyQuestCampaignId campaign{0x1111, 0x2222};
    const PartyQuestPlayerProfileId playerA{0xAAAA, 0xBBBB};
    const PartyQuestPlayerProfileId playerB{0xCCCC, 0xDDDD};

    PartyQuestRuntimeApplyCoordinator coordinator;
    const auto recovery = coordinator.ExportRecoveryState(campaign, playerA);
    REQUIRE(recovery.CampaignId == campaign);
    REQUIRE(recovery.PlayerProfileId == playerA);

    PartyQuestRuntimeApplyCoordinator wrongPlayer;
    REQUIRE(wrongPlayer.RestoreRecoveryState(recovery, campaign, playerB) ==
        PartyQuestRuntimeRecoveryDisposition::PlayerProfileMismatch);

    PartyQuestRuntimeApplyCoordinator correctPlayer;
    REQUIRE(correctPlayer.RestoreRecoveryState(recovery, campaign, playerA) ==
        PartyQuestRuntimeRecoveryDisposition::Clean);

    const auto pathA = PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, playerA);
    const auto pathB = PartyQuestCoopSaveLayout::Build("CoopCampaigns", campaign, playerB);
    REQUIRE(pathA.has_value());
    REQUIRE(pathB.has_value());
    REQUIRE(pathA->RuntimeApplySidecar != pathB->RuntimeApplySidecar);
}
