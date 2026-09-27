#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>
#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>
#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kServerCampaign{
    0xA1B2C3D4E5F60718ull,
    0x81706F5E4D3C2B1Aull};
const PartyQuestPlayerProfileId kServerProfile{
    0x1020304050607080ull,
    0x8070605040302010ull};

struct ServerPersistenceSandbox
{
    std::filesystem::path Root;

    ServerPersistenceSandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_server_durable_namespace_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~ServerPersistenceSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};
} // namespace

TEST_CASE(
    "server strong archives require explicit durable namespace promotion",
    "[quest.party-state.server][durability][namespace]")
{
    ServerPersistenceSandbox sandbox;
    const auto stateDirectory = sandbox.Root / "server" / "state";
    const auto statePath = stateDirectory / "party_quest_campaign.bin";
    const auto campaignPath = stateDirectory / "party_quest_campaign.bin.campaign-id";
    const auto profilePath = stateDirectory / "player-profile.bin";
    const PartyQuestState state;

    // Strong archive writers deliberately have no directory-creation authority.
    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                statePath,
                kServerCampaign,
                state) == PartyQuestPersistenceStatus::IoError);
    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(
                campaignPath,
                kServerCampaign) == PartyQuestCampaignPersistenceStatus::IoError);
    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                profilePath,
                kServerProfile) == PartyQuestPlayerProfilePersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(stateDirectory));

    // Namespace authority is separate and must cross its own stable-storage barrier.
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(stateDirectory) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(std::filesystem::is_directory(stateDirectory));

    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                statePath,
                kServerCampaign,
                state) == PartyQuestPersistenceStatus::Success);
    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(
                campaignPath,
                kServerCampaign) == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                profilePath,
                kServerProfile) == PartyQuestPlayerProfilePersistenceStatus::Success);

    const auto loadedState = PartyQuestStatePersistence::Load(statePath);
    REQUIRE(loadedState.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(loadedState.CampaignId == kServerCampaign);
    REQUIRE(loadedState.State.has_value());
    REQUIRE(loadedState.State->GetWorldRevision() == 0);

    const auto loadedCampaign = PartyQuestCampaignPersistence::Load(campaignPath);
    REQUIRE(loadedCampaign.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(loadedCampaign.CampaignId == kServerCampaign);
    REQUIRE(loadedCampaign.CanonicalArchiveRequired);

    const auto loadedProfile = PartyQuestPlayerProfilePersistence::Load(profilePath);
    REQUIRE(loadedProfile.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(loadedProfile.ProfileId == kServerProfile);

    // A concrete archive path can now be strong without widening global mutation policy.
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
