#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

const std::filesystem::path& GetTPTestsExecutablePath() noexcept;

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

bool SetCampaignPersistenceEnvironment(const char* apName, const std::string& acValue)
{
#ifdef _WIN32
    return _putenv_s(apName, acValue.c_str()) == 0;
#else
    return setenv(apName, acValue.c_str(), 1) == 0;
#endif
}

void ClearCampaignPersistenceEnvironment(const char* apName)
{
#ifdef _WIN32
    _putenv_s(apName, "");
#else
    unsetenv(apName);
#endif
}

struct CampaignPersistenceCrashBoundary
{
    PartyQuestCampaignPersistenceBoundary Boundary;
};

PartyQuestCampaignPersistenceDirective CrashCampaignPersistenceAtBoundary(
    PartyQuestCampaignPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto& crash = *static_cast<const CampaignPersistenceCrashBoundary*>(apContext);
    if (crash.Boundary == aBoundary)
        std::_Exit(92);
    return PartyQuestCampaignPersistenceDirective::Continue;
}

PartyQuestCampaignPersistenceBoundary ParseCampaignPersistenceBoundary(
    const std::string& acBoundary)
{
    if (acBoundary == "TemporaryVerified")
        return PartyQuestCampaignPersistenceBoundary::TemporaryVerified;
    if (acBoundary == "PrimaryMovedToBackup")
        return PartyQuestCampaignPersistenceBoundary::PrimaryMovedToBackup;
    if (acBoundary == "PrimaryPublished")
        return PartyQuestCampaignPersistenceBoundary::PrimaryPublished;
    if (acBoundary == "BackupTemporaryVerified")
        return PartyQuestCampaignPersistenceBoundary::BackupTemporaryVerified;
    return PartyQuestCampaignPersistenceBoundary::BackupPublished;
}

void RunCampaignPersistenceCrashProcess(
    const std::filesystem::path& acRoot,
    const char* apBoundary)
{
    REQUIRE(SetCampaignPersistenceEnvironment(
        "TP_CAMPAIGN_PERSISTENCE_CRASH_ROOT", acRoot.string()));
    REQUIRE(SetCampaignPersistenceEnvironment(
        "TP_CAMPAIGN_PERSISTENCE_CRASH_BOUNDARY", apBoundary));

    const auto& executable = GetTPTestsExecutablePath();
    REQUIRE_FALSE(executable.empty());
    const std::string command =
        "\"" + executable.string() +
        "\" \"Campaign metadata atomic publication crash helper\" --reporter compact";
    const int exitCode = std::system(command.c_str());

    ClearCampaignPersistenceEnvironment("TP_CAMPAIGN_PERSISTENCE_CRASH_BOUNDARY");
    ClearCampaignPersistenceEnvironment("TP_CAMPAIGN_PERSISTENCE_CRASH_ROOT");
    REQUIRE(exitCode != 0);

    const PartyQuestCampaignId campaignId{900, 1000};
    const auto path = acRoot / "campaign.id";
    const auto loaded = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(loaded.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(loaded.CampaignId == campaignId);

    const std::string boundary = apBoundary;
    const bool beforePrimaryMove = boundary == "TemporaryVerified";
    REQUIRE(loaded.CanonicalArchiveRequired != beforePrimaryMove);
    REQUIRE(loaded.UsedTemporary == (boundary == "PrimaryMovedToBackup"));
    REQUIRE(loaded.BackupRefreshRequired ==
        (boundary == "PrimaryMovedToBackup" ||
         boundary == "PrimaryPublished" ||
         boundary == "BackupTemporaryVerified"));

    REQUIRE(PartyQuestCampaignPersistence::SaveAtomically(path, campaignId) ==
            PartyQuestCampaignPersistenceStatus::Success);
    const auto converged = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(converged.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(converged.CampaignId == campaignId);
    REQUIRE(converged.CanonicalArchiveRequired);
    REQUIRE_FALSE(converged.UsedBackup);
    REQUIRE_FALSE(converged.UsedTemporary);
    REQUIRE_FALSE(converged.BackupRefreshRequired);
}
} // namespace

TEST_CASE("Campaign metadata atomic publication crash helper", "[.][quest.party-state.campaign-id][fault-helper]")
{
    const char* rootValue = std::getenv("TP_CAMPAIGN_PERSISTENCE_CRASH_ROOT");
    const char* boundaryValue = std::getenv("TP_CAMPAIGN_PERSISTENCE_CRASH_BOUNDARY");
    REQUIRE(rootValue != nullptr);
    REQUIRE(boundaryValue != nullptr);

    const std::string boundary = boundaryValue;
    REQUIRE((boundary == "TemporaryVerified" ||
        boundary == "PrimaryMovedToBackup" ||
        boundary == "PrimaryPublished" ||
        boundary == "BackupTemporaryVerified" ||
        boundary == "BackupPublished"));

    const PartyQuestCampaignId campaignId{900, 1000};
    const auto path = std::filesystem::path(rootValue) / "campaign.id";
    auto backupPath = path;
    backupPath += ".bak";
    const auto legacy = BuildLegacyV1Archive(campaignId);
    WriteArchive(path, legacy);
    WriteArchive(backupPath, legacy);

    CampaignPersistenceCrashBoundary crash{
        ParseCampaignPersistenceBoundary(boundary)};
    const auto status = PartyQuestCampaignPersistence::SaveAtomically(
        path,
        campaignId,
        {CrashCampaignPersistenceAtBoundary, &crash});
    FAIL("Crash boundary returned with status " << static_cast<int>(status));
}

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

    std::vector<uint8_t> oversized(
        PartyQuestDurableResourcePolicy::MaxIdentityArchiveBytes + 1);
    REQUIRE(PartyQuestCampaignPersistence::Decode(oversized).Status ==
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

TEST_CASE("Campaign metadata publication windows survive abrupt process termination", "[quest.party-state.campaign-id][fault][process]")
{
    const auto runBoundary = [](const char* apBoundary)
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_campaign_crash_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        REQUIRE_FALSE(ec);
        RunCampaignPersistenceCrashProcess(root, apBoundary);
        std::filesystem::remove_all(root, ec);
        REQUIRE_FALSE(ec);
    };

    SECTION("verified temporary preserves legacy primary authority")
    {
        runBoundary("TemporaryVerified");
    }
    SECTION("primary move exposes the verified v2 temporary")
    {
        runBoundary("PrimaryMovedToBackup");
    }
    SECTION("primary publication requires backup refresh")
    {
        runBoundary("PrimaryPublished");
    }
    SECTION("verified backup temporary remains restart-convergent")
    {
        runBoundary("BackupTemporaryVerified");
    }
    SECTION("published primary and backup are immediately complete")
    {
        runBoundary("BackupPublished");
    }
}
