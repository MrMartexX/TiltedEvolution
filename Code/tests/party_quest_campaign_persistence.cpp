#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
void RemoveCampaignIdentityFiles(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);

    auto backup = acPath;
    backup += ".bak";
    std::filesystem::remove(backup, ec);

    auto temporary = acPath;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
}
} // namespace

TEST_CASE("Campaign identity metadata round-trips deterministically", "[quest.party-state.campaign-id]")
{
    const PartyQuestCampaignId campaignId{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull};

    const auto first = PartyQuestCampaignPersistence::Encode(campaignId);
    const auto second = PartyQuestCampaignPersistence::Encode(campaignId);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);

    const auto decoded = PartyQuestCampaignPersistence::Decode(first);
    REQUIRE(decoded.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(decoded.CampaignId.has_value());
    REQUIRE(*decoded.CampaignId == campaignId);
    REQUIRE_FALSE(decoded.UsedBackup);
}

TEST_CASE("Campaign identity metadata rejects invalid and corrupted archives", "[quest.party-state.campaign-id]")
{
    REQUIRE(PartyQuestCampaignPersistence::Encode({}).empty());

    const PartyQuestCampaignId campaignId{11, 22};
    const auto encoded = PartyQuestCampaignPersistence::Encode(campaignId);
    REQUIRE(encoded.size() > 20);

    auto corrupted = encoded;
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestCampaignPersistence::Decode(corrupted).Status ==
            PartyQuestCampaignPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestCampaignPersistence::Decode(truncated).Status ==
            PartyQuestCampaignPersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestCampaignPersistence::Decode(unsupported).Status ==
            PartyQuestCampaignPersistenceStatus::UnsupportedVersion);
}

TEST_CASE("Campaign identity is generated valid and survives backup recovery", "[quest.party-state.campaign-id]")
{
    const PartyQuestCampaignId generated = PartyQuestCampaignPersistence::GenerateCampaignId();
    REQUIRE(generated.IsValid());

    const auto uniqueSuffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_campaign_" + std::to_string(uniqueSuffix) + ".id");
    RemoveCampaignIdentityFiles(path);

    const PartyQuestCampaignId original{100, 200};
    REQUIRE(PartyQuestCampaignPersistence::SaveAtomically(path, original) ==
            PartyQuestCampaignPersistenceStatus::Success);

    auto firstLoad = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(firstLoad.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(firstLoad.CampaignId == original);
    REQUIRE_FALSE(firstLoad.UsedBackup);

    const PartyQuestCampaignId replacement{300, 400};
    REQUIRE(PartyQuestCampaignPersistence::SaveAtomically(path, replacement) ==
            PartyQuestCampaignPersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    auto recovered = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(recovered.CampaignId == original);
    REQUIRE(recovered.UsedBackup);

    RemoveCampaignIdentityFiles(path);
}
