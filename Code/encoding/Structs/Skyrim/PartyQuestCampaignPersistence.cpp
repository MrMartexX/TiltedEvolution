#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <type_traits>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'C', 'A', 'M', 'P', 'I'};
constexpr uint16_t kFormatVersion = 1;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kPayloadSize = sizeof(uint64_t) * 2;

template <class T>
void WriteInteger(std::vector<uint8_t>& aBytes, T aValue)
{
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    const auto value = static_cast<UnsignedType>(aValue);
    for (size_t i = 0; i < sizeof(UnsignedType); ++i)
        aBytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}

template <class T>
bool ReadInteger(const std::vector<uint8_t>& acBytes, size_t& aOffset, T& aValue) noexcept
{
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    if (aOffset > acBytes.size() || acBytes.size() - aOffset < sizeof(UnsignedType))
        return false;

    UnsignedType value{};
    for (size_t i = 0; i < sizeof(UnsignedType); ++i)
        value |= static_cast<UnsignedType>(acBytes[aOffset + i]) << (i * 8);

    aOffset += sizeof(UnsignedType);
    aValue = static_cast<T>(value);
    return true;
}

uint64_t ComputeChecksum(const uint8_t* apData, size_t aSize) noexcept
{
    uint64_t checksum = kFnvOffsetBasis;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= kFnvPrime;
    }
    return checksum;
}

uint64_t Mix64(uint64_t aValue) noexcept
{
    aValue ^= aValue >> 30;
    aValue *= 0xBF58476D1CE4E5B9ull;
    aValue ^= aValue >> 27;
    aValue *= 0x94D049BB133111EBull;
    aValue ^= aValue >> 31;
    return aValue;
}

PartyQuestCampaignPersistenceStatus ReadFile(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return std::filesystem::exists(acPath)
            ? PartyQuestCampaignPersistenceStatus::IoError
            : PartyQuestCampaignPersistenceStatus::FileNotFound;
    }

    const std::streampos end = file.tellg();
    if (end < 0)
        return PartyQuestCampaignPersistenceStatus::IoError;

    const auto size = static_cast<uint64_t>(end);
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty() &&
        !file.read(reinterpret_cast<char*>(aBytes.data()), static_cast<std::streamsize>(aBytes.size())))
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    return PartyQuestCampaignPersistenceStatus::Success;
}

bool WriteFile(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    if (!acBytes.empty())
        file.write(reinterpret_cast<const char*>(acBytes.data()), static_cast<std::streamsize>(acBytes.size()));

    file.flush();
    return file.good();
}
} // namespace

PartyQuestCampaignId PartyQuestCampaignPersistence::GenerateCampaignId() noexcept
{
    static std::atomic<uint64_t> sequence{1};

    const uint64_t counter = sequence.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    uint64_t randomHigh{};
    uint64_t randomLow{};
    try
    {
        std::random_device random;
        randomHigh = (static_cast<uint64_t>(random()) << 32) ^ static_cast<uint64_t>(random());
        randomLow = (static_cast<uint64_t>(random()) << 32) ^ static_cast<uint64_t>(random());
    }
    catch (...)
    {
        randomHigh = now ^ (counter * 0x9E3779B97F4A7C15ull);
        randomLow = (now << 17) ^ (now >> 11) ^ counter;
    }

    PartyQuestCampaignId id;
    id.High = Mix64(randomHigh ^ now ^ counter);
    id.Low = Mix64(randomLow ^ (now << 1) ^ (counter * 0xD6E8FEB86659FD93ull));
    if (!id.IsValid())
        id.Low = 1;
    return id;
}

std::vector<uint8_t> PartyQuestCampaignPersistence::Encode(
    const PartyQuestCampaignId& acCampaignId)
{
    if (!acCampaignId.IsValid())
        return {};

    std::vector<uint8_t> payload;
    payload.reserve(kPayloadSize);
    WriteInteger(payload, acCampaignId.High);
    WriteInteger(payload, acCampaignId.Low);

    std::vector<uint8_t> archive;
    archive.reserve(kMagic.size() + sizeof(uint16_t) + sizeof(uint64_t) + payload.size() + sizeof(uint64_t));
    archive.insert(archive.end(), kMagic.begin(), kMagic.end());
    WriteInteger(archive, kFormatVersion);
    WriteInteger<uint64_t>(archive, payload.size());
    archive.insert(archive.end(), payload.begin(), payload.end());
    WriteInteger(archive, ComputeChecksum(payload.data(), payload.size()));
    return archive;
}

PartyQuestCampaignPersistenceResult PartyQuestCampaignPersistence::Decode(
    const std::vector<uint8_t>& acBytes)
{
    PartyQuestCampaignPersistenceResult result;
    size_t offset{};

    if (acBytes.size() < kMagic.size())
    {
        result.Status = PartyQuestCampaignPersistenceStatus::Truncated;
        return result;
    }

    if (!std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::InvalidMagic;
        return result;
    }
    offset += kMagic.size();

    uint16_t formatVersion{};
    uint64_t payloadSize{};
    if (!ReadInteger(acBytes, offset, formatVersion) || !ReadInteger(acBytes, offset, payloadSize))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::Truncated;
        return result;
    }
    if (formatVersion != kFormatVersion)
    {
        result.Status = PartyQuestCampaignPersistenceStatus::UnsupportedVersion;
        return result;
    }
    if (payloadSize != kPayloadSize)
    {
        result.Status = PartyQuestCampaignPersistenceStatus::InvalidData;
        return result;
    }
    if (offset > acBytes.size() || acBytes.size() - offset < payloadSize + sizeof(uint64_t))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::Truncated;
        return result;
    }

    const size_t payloadOffset = offset;
    PartyQuestCampaignId id;
    if (!ReadInteger(acBytes, offset, id.High) || !ReadInteger(acBytes, offset, id.Low))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::Truncated;
        return result;
    }

    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, offset, storedChecksum))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::Truncated;
        return result;
    }
    if (offset != acBytes.size())
    {
        result.Status = PartyQuestCampaignPersistenceStatus::InvalidData;
        return result;
    }
    if (storedChecksum != ComputeChecksum(acBytes.data() + payloadOffset, kPayloadSize))
    {
        result.Status = PartyQuestCampaignPersistenceStatus::ChecksumMismatch;
        return result;
    }
    if (!id.IsValid())
    {
        result.Status = PartyQuestCampaignPersistenceStatus::InvalidData;
        return result;
    }

    result.Status = PartyQuestCampaignPersistenceStatus::Success;
    result.CampaignId = id;
    return result;
}

PartyQuestCampaignPersistenceStatus PartyQuestCampaignPersistence::SaveAtomically(
    const std::filesystem::path& acPath,
    const PartyQuestCampaignId& acCampaignId)
{
    const std::vector<uint8_t> encoded = Encode(acCampaignId);
    if (encoded.empty())
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    std::error_code ec;
    if (!acPath.parent_path().empty())
    {
        std::filesystem::create_directories(acPath.parent_path(), ec);
        if (ec)
            return PartyQuestCampaignPersistenceStatus::IoError;
    }

    auto temporaryPath = acPath;
    temporaryPath += ".tmp";
    auto backupPath = acPath;
    backupPath += ".bak";

    std::filesystem::remove(temporaryPath, ec);
    ec.clear();

    if (!WriteFile(temporaryPath, encoded))
    {
        std::filesystem::remove(temporaryPath, ec);
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    const bool hadPrimary = std::filesystem::exists(acPath, ec) && !ec;
    if (hadPrimary)
    {
        std::filesystem::remove(backupPath, ec);
        ec.clear();
        std::filesystem::rename(acPath, backupPath, ec);
        if (ec)
        {
            std::filesystem::remove(temporaryPath, ec);
            return PartyQuestCampaignPersistenceStatus::IoError;
        }
    }

    std::filesystem::rename(temporaryPath, acPath, ec);
    if (ec)
    {
        std::error_code restoreError;
        if (hadPrimary && std::filesystem::exists(backupPath, restoreError))
            std::filesystem::rename(backupPath, acPath, restoreError);
        std::filesystem::remove(temporaryPath, restoreError);
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    return PartyQuestCampaignPersistenceStatus::Success;
}

PartyQuestCampaignPersistenceResult PartyQuestCampaignPersistence::Load(
    const std::filesystem::path& acPath)
{
    std::vector<uint8_t> bytes;
    const PartyQuestCampaignPersistenceStatus primaryReadStatus = ReadFile(acPath, bytes);

    PartyQuestCampaignPersistenceResult primaryResult;
    primaryResult.Status = primaryReadStatus;
    if (primaryReadStatus == PartyQuestCampaignPersistenceStatus::Success)
    {
        primaryResult = Decode(bytes);
        if (primaryResult.Status == PartyQuestCampaignPersistenceStatus::Success)
            return primaryResult;
    }

    auto backupPath = acPath;
    backupPath += ".bak";
    bytes.clear();
    const PartyQuestCampaignPersistenceStatus backupReadStatus = ReadFile(backupPath, bytes);
    if (backupReadStatus == PartyQuestCampaignPersistenceStatus::Success)
    {
        auto backupResult = Decode(bytes);
        if (backupResult.Status == PartyQuestCampaignPersistenceStatus::Success)
        {
            backupResult.UsedBackup = true;
            return backupResult;
        }
    }

    return primaryResult;
}
