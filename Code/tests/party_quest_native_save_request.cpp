#include <Structs/Skyrim/PartyQuestNativeSaveRequest.h>

#include <catch2/catch.hpp>

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using NativeSaveRequestEncoderFunction =
    PartyQuestNativeSaveRequestEncodeResult (*)(
        const PartyQuestAsyncSaveRequestIdentity&) noexcept;

static_assert(std::is_same_v<
    decltype(&PartyQuestNativeSaveRequestEncoder::Encode),
    NativeSaveRequestEncoderFunction>);

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

TEST_CASE("Native isolated save request ABI layout and padding are exact")
{
    REQUIRE(sizeof(PartyQuestNativeSaveRequestIdentity) == 208u);
    REQUIRE(sizeof(PartyQuestNativeIsolatedSaveRequest) == 480u);
    REQUIRE(alignof(PartyQuestNativeIsolatedSaveRequest) == 8u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, RuntimeGeneration) == 0u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, TransactionId) == 8u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, TargetWorldRevision) == 16u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, CaptureEpochId) == 24u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, AttemptNonce) == 32u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, CampaignId) == 40u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, PlayerProfileId) == 73u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, SaveName) == 106u);
    REQUIRE(offsetof(PartyQuestNativeSaveRequestIdentity, Reserved) == 202u);
    REQUIRE(offsetof(PartyQuestNativeIsolatedSaveRequest, AbiVersion) == 0u);
    REQUIRE(offsetof(PartyQuestNativeIsolatedSaveRequest, StructSize) == 4u);
    REQUIRE(offsetof(PartyQuestNativeIsolatedSaveRequest, Identity) == 8u);
    REQUIRE(offsetof(PartyQuestNativeIsolatedSaveRequest, RelativeSavePath) == 216u);

    const auto encoded =
        PartyQuestNativeSaveRequestEncoder::Encode(MakeIdentity());
    REQUIRE(encoded.Status == PartyQuestNativeSaveRequestEncodeStatus::Encoded);
    REQUIRE(encoded.Request);

    const auto& request = *encoded.Request;
    for (const auto value : request.Identity.Reserved)
        REQUIRE(value == 0u);

    const auto campaignLength = std::strlen(request.Identity.CampaignId.data());
    REQUIRE(campaignLength == 32u);
    for (size_t index = campaignLength + 1u;
         index < request.Identity.CampaignId.size();
         ++index)
    {
        REQUIRE(request.Identity.CampaignId[index] == '\0');
    }

    const auto profileLength =
        std::strlen(request.Identity.PlayerProfileId.data());
    REQUIRE(profileLength == 32u);
    for (size_t index = profileLength + 1u;
         index < request.Identity.PlayerProfileId.size();
         ++index)
    {
        REQUIRE(request.Identity.PlayerProfileId[index] == '\0');
    }

    const auto saveNameLength =
        std::strlen(request.Identity.SaveName.data());
    for (size_t index = saveNameLength + 1u;
         index < request.Identity.SaveName.size();
         ++index)
    {
        REQUIRE(request.Identity.SaveName[index] == '\0');
    }

    const auto pathLength = std::strlen(request.RelativeSavePath.data());
    for (size_t index = pathLength + 1u;
         index < request.RelativeSavePath.size();
         ++index)
    {
        REQUIRE(request.RelativeSavePath[index] == '\0');
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(&request);
    for (size_t index = 476u; index < sizeof(request); ++index)
        REQUIRE(bytes[index] == 0u);
}

TEST_CASE("Native isolated save request encoder formats edge identities canonically")
{
    auto identity = MakeIdentity();
    identity.CampaignId = {
        0x0000000000000001ull,
        std::numeric_limits<uint64_t>::max()};
    identity.PlayerProfileId = {
        0x000000000000000Aull,
        0x0000000000000001ull};
    identity.TransactionId = std::numeric_limits<uint64_t>::max();
    identity.TargetWorldRevision = 1u;
    identity.AttemptNonce = std::numeric_limits<uint64_t>::max();

    char saveName[96]{};
    const int written = std::snprintf(
        saveName,
        sizeof(saveName),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX",
        static_cast<unsigned long long>(identity.TransactionId),
        static_cast<unsigned long long>(identity.TargetWorldRevision),
        static_cast<unsigned long long>(identity.AttemptNonce));
    REQUIRE(written > 0);
    REQUIRE(static_cast<size_t>(written) < sizeof(saveName));
    identity.SaveName = saveName;
    REQUIRE(identity.IsValid());

    const auto encoded = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    REQUIRE(encoded.Status == PartyQuestNativeSaveRequestEncodeStatus::Encoded);
    REQUIRE(encoded.Request);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.CampaignId.data(),
                "0000000000000001FFFFFFFFFFFFFFFF") == 0);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.PlayerProfileId.data(),
                "000000000000000A0000000000000001") == 0);
    REQUIRE(std::strcmp(
                encoded.Request->Identity.SaveName.data(),
                "STR_PreRepair_TFFFFFFFFFFFFFFFF_R0000000000000001_AFFFFFFFFFFFFFFFF") == 0);
    REQUIRE(std::strcmp(
                encoded.Request->RelativeSavePath.data(),
                "CoopCampaigns\\Campaign_0000000000000001FFFFFFFFFFFFFFFF\\"
                "Player_000000000000000A0000000000000001\\saves\\") == 0);
}

TEST_CASE("Native isolated save request encoder is byte deterministic")
{
    const auto identity = MakeIdentity();
    const auto first = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    const auto second = PartyQuestNativeSaveRequestEncoder::Encode(identity);
    REQUIRE(first.Status == PartyQuestNativeSaveRequestEncodeStatus::Encoded);
    REQUIRE(second.Status == PartyQuestNativeSaveRequestEncodeStatus::Encoded);
    REQUIRE(first.Request);
    REQUIRE(second.Request);
    REQUIRE(std::memcmp(
                &*first.Request,
                &*second.Request,
                sizeof(PartyQuestNativeIsolatedSaveRequest)) == 0);
}

TEST_CASE("Native isolated save request encoder rejects every zero identity field")
{
    const auto expectInvalid =
        [](const PartyQuestAsyncSaveRequestIdentity& acIdentity)
    {
        const auto encoded =
            PartyQuestNativeSaveRequestEncoder::Encode(acIdentity);
        REQUIRE(encoded.Status ==
                PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
        REQUIRE_FALSE(encoded.Request);
    };

    SECTION("transaction")
    {
        auto identity = MakeIdentity();
        identity.TransactionId = 0u;
        identity.SaveName =
            "STR_PreRepair_T0000000000000000_R000000000000001E_A0000000000000032";
        expectInvalid(identity);
    }
    SECTION("target revision")
    {
        auto identity = MakeIdentity();
        identity.TargetWorldRevision = 0u;
        identity.SaveName =
            "STR_PreRepair_T0000000000000014_R0000000000000000_A0000000000000032";
        expectInvalid(identity);
    }
    SECTION("capture epoch")
    {
        auto identity = MakeIdentity();
        identity.CaptureEpochId = 0u;
        expectInvalid(identity);
    }
    SECTION("attempt nonce")
    {
        auto identity = MakeIdentity();
        identity.AttemptNonce = 0u;
        identity.SaveName =
            "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000000";
        expectInvalid(identity);
    }
}

TEST_CASE("Native isolated save request encoder rejects invalid stable identities")
{
    SECTION("campaign id")
    {
        auto identity = MakeIdentity();
        identity.CampaignId = {};
        const auto encoded =
            PartyQuestNativeSaveRequestEncoder::Encode(identity);
        REQUIRE(encoded.Status ==
                PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
        REQUIRE_FALSE(encoded.Request);
    }

    SECTION("player profile id")
    {
        auto identity = MakeIdentity();
        identity.PlayerProfileId = {};
        const auto encoded =
            PartyQuestNativeSaveRequestEncoder::Encode(identity);
        REQUIRE(encoded.Status ==
                PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
        REQUIRE_FALSE(encoded.Request);
    }
}

TEST_CASE("Native isolated save request encoder rejects noncanonical save names")
{
    const auto expectInvalidSaveName = [](std::string aSaveName)
    {
        auto identity = MakeIdentity();
        identity.SaveName = std::move(aSaveName);
        const auto encoded =
            PartyQuestNativeSaveRequestEncoder::Encode(identity);
        REQUIRE(encoded.Status ==
                PartyQuestNativeSaveRequestEncodeStatus::InvalidIdentity);
        REQUIRE_FALSE(encoded.Request);
    };

    SECTION("oversized")
    {
        expectInvalidSaveName(std::string(96u, 'A'));
    }

    SECTION("embedded nul")
    {
        auto name = MakeIdentity().SaveName;
        REQUIRE(name.size() > 10u);
        name[10] = '\0';
        expectInvalidSaveName(std::move(name));
    }
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
