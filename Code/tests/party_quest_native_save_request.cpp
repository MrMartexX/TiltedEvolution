#include <Structs/Skyrim/PartyQuestNativeSaveRequest.h>

#include <catch2/catch.hpp>

#include <cstdio>
#include <cstring>

namespace
{
PartyQuestAsyncSaveRequestIdentity MakeIdentity()
{
    PartyQuestAsyncSaveRequestIdentity identity;
    identity.CampaignId = {0x1111222233334444ull, 0x5555666677778888ull};
    identity.PlayerProfileId = {0x9999AAAABBBBCCCCull, 0xDDDDEEEEFFFF0001ull};
    identity.RuntimeGeneration = 10u;
    identity.TransactionId = 20u;
    identity.TargetWorldRevision = 30u;
    identity.CaptureEpochId = 40u;
    identity.AttemptNonce = 50u;
    char saveName[96]{};
    const int written = std::snprintf(
        saveName,
        sizeof(saveName),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX",
        static_cast<unsigned long long>(identity.TransactionId),
        static_cast<unsigned long long>(identity.TargetWorldRevision),
        static_cast<unsigned long long>(identity.AttemptNonce));
    REQUIRE(written > 0);
    identity.SaveName = saveName;
    REQUIRE(identity.IsValid());
    return identity;
}
}

TEST_CASE("Native isolated save request encoder emits exact deterministic ABI")
{
    const auto identity = MakeIdentity();
    const auto encoded = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    REQUIRE(encoded.Status == PartyQuestNativeSaveRequestEncodeStatus::Encoded);
    REQUIRE(encoded.Request);
    REQUIRE(encoded.Request->AbiVersion == 2u);
    REQUIRE(encoded.Request->StructSize == 480u);
    REQUIRE(encoded.Request->Identity.RuntimeGeneration == 10u);
    REQUIRE(encoded.Request->Identity.TransactionId == 20u);
    REQUIRE(encoded.Request->Identity.TargetWorldRevision == 30u);
    REQUIRE(encoded.Request->Identity.CaptureEpochId == 40u);
    REQUIRE(encoded.Request->Identity.AttemptNonce == 50u);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.CampaignId.data(),
                "11112222333344445555666677778888") == 0);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.PlayerProfileId.data(),
                "9999AAAABBBBCCCCDDDDEEEEFFFF0001") == 0);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.SaveName.data(),
                identity.SaveName.c_str()) == 0);
    REQUIRE(std::strcmp(
                encoded.Request->RelativeSavePath.data(),
                "CoopCampaigns\\Campaign_11112222333344445555666677778888\\"
                "Player_9999AAAABBBBCCCCDDDDEEEEFFFF0001\\saves\\") == 0);
}

TEST_CASE("Native isolated save request encoder rejects invalid identity fields")
{
    auto identity = MakeIdentity();
    identity.RuntimeGeneration = 0u;
    auto invalid = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    REQUIRE(invalid.Status ==
            PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
    REQUIRE_FALSE(invalid.Request);

    identity = MakeIdentity();
    identity.SaveName.push_back('X');
    invalid = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    REQUIRE(invalid.Status ==
            PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
    REQUIRE_FALSE(invalid.Request);
}

TEST_CASE("Native isolated save request encoder zeroes every reserved byte")
{
    const auto encoded =
        PartyQuestNativeSaveRequestEncoder::Encode(MakeIdentity());
    REQUIRE(encoded.Request);
    for (const auto value : encoded.Request->Identity.Reserved)
        REQUIRE(value == 0u);

    const auto* bytes = reinterpret_cast<const uint8_t*>(&*encoded.Request);
    REQUIRE(bytes[479] == 0u);
}
