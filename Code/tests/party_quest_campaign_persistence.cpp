#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
constexpr size_t kFormatVersionOffset = 8;
constexpr size_t kPayloadSizeOffset = 10;
constexpr size_t kPayloadOffset = 18;
constexpr size_t kLegacyPayloadSize = 16;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t ComputeChecksum(const uint8_t* apData, size_t aSize)
{
    uint64_t checksum = kFnvOffsetBasis;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= kFnvPrime;
    }
    return checksum;
}

template <class T>
void WriteIntegerAt(std::vector<uint8_t>& aBytes, size_t aOffset, T aValue)
{
    for (size_t i = 0; i < sizeof(T); ++i)
        aBytes[aOffset + i] = static_cast<uint8_t>((aValue >> (i * 8)) & 0xFF);
}

std::vector<uint8_t> BuildLegacyV1Archive(const PartyQuestCampaignId& acCampaignId)
{
    auto archive = PartyQuestCampaignPersistence::Encode(acCampaignId);
    archive.erase(archive.begin() + kPayloadOffset + kLegacyPayloadSize);
    WriteIntegerAt<uint16_t>(archive, kFormatVersionOffset, 1);
    WriteIntegerAt<uint64_t>(archive, kPayloadSizeOffset, kLegacyPayloadSize);
    WriteIntegerAt<uint64_t>(
        archive,
        kPayloadOffset + kLegacyPayloadSize,
        ComputeChecksum(archive.data() + kPayloadOffset, kLegacyPayloadSize));
    return archive;
}

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

    auto backupTemporary = backup;
    backupTemporary += ".tmp";
    std::filesystem::remove(backupTemporary, ec);
}

void WriteArchive(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(
        reinterpret_cast<const char*>(acBytes.data()),
        static_cast<std::streamsize>(acBytes.size()));
    REQUIRE(file.good());
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
    REQUIRE(decoded.CanonicalArchiveRequired);
    REQUIRE_FALSE(decoded.UsedBackup);
}

TEST_CASE("Legacy campaign identity metadata remains readable but does not require an archive", "[quest.party-state.campaign-id]")
{
    const PartyQuestCampaignId campaignId{0x1122334455667788ull, 0x8877665544332211ull};
    const auto legacy = BuildLegacyV1Archive(campaignId);

    const auto decoded = PartyQuestCampaignPersistence::Decode(legacy);
    REQUIRE(decoded.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(decoded.CampaignId == campaignId);
    REQUIRE_FALSE(decoded.CanonicalArchiveRequired);

    const auto migrated = PartyQuestCampaignPersistence::Decode(
        PartyQuestCampaignPersistence::Encode(*decoded.CampaignId));
    REQUIRE(migrated.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(migrated.CampaignId == campaignId);
    REQUIRE(migrated.CanonicalArchiveRequired);
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

    auto invalidMarker = encoded;
    invalidMarker[kPayloadOffset + kLegacyPayloadSize] = 0;
    WriteIntegerAt<uint64_t>(
        invalidMarker,
        kPayloadOffset + kLegacyPayloadSize + sizeof(uint8_t),
        ComputeChecksum(
            invalidMarker.data() + kPayloadOffset,
            kLegacyPayloadSize + sizeof(uint8_t)));
    REQUIRE(PartyQuestCampaignPersistence::Decode(invalidMarker).Status ==
            PartyQuestCampaignPersistenceStatus::InvalidData);
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
    REQUIRE(firstLoad.CanonicalArchiveRequired);
    REQUIRE_FALSE(firstLoad.UsedBackup);
    REQUIRE_FALSE(firstLoad.BackupRefreshRequired);

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
    REQUIRE(recovered.CampaignId == replacement);
    REQUIRE(recovered.CanonicalArchiveRequired);
    REQUIRE(recovered.UsedBackup);

    RemoveCampaignIdentityFiles(path);
}

TEST_CASE("Archive-required backup prevents rollback to legacy metadata", "[quest.party-state.campaign-id]")
{
    const auto uniqueSuffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_campaign_marker_" + std::to_string(uniqueSuffix) + ".id");
    RemoveCampaignIdentityFiles(path);

    const PartyQuestCampaignId campaignId{500, 600};
    auto backupPath = path;
    backupPath += ".bak";

    WriteArchive(path, PartyQuestCampaignPersistence::Encode(campaignId));
    const auto singleCopyLoad = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(singleCopyLoad.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(singleCopyLoad.CampaignId == campaignId);
    REQUIRE(singleCopyLoad.CanonicalArchiveRequired);
    REQUIRE(singleCopyLoad.BackupRefreshRequired);

    WriteArchive(path, BuildLegacyV1Archive(campaignId));
    WriteArchive(backupPath, PartyQuestCampaignPersistence::Encode(campaignId));

    const auto protectedLoad = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(protectedLoad.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(protectedLoad.CampaignId == campaignId);
    REQUIRE(protectedLoad.CanonicalArchiveRequired);
    REQUIRE(protectedLoad.UsedBackup);

    WriteArchive(path, BuildLegacyV1Archive({700, 800}));
    const auto conflictingLoad = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(conflictingLoad.Status == PartyQuestCampaignPersistenceStatus::InvalidData);
    REQUIRE_FALSE(conflictingLoad.CampaignId.has_value());

    RemoveCampaignIdentityFiles(path);
}
