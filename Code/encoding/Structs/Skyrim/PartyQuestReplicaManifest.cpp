#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <type_traits>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'R', 'P', 'L', 'C', 'M'};
constexpr uint16_t kFormatVersion = 1;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint32_t kMaxFiles = 4096;
constexpr uint32_t kMaxPathBytes = 4096;

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
bool ReadInteger(
    const std::vector<uint8_t>& acBytes,
    size_t& aOffset,
    size_t aEnd,
    T& aValue) noexcept
{
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    if (aEnd > acBytes.size() || aOffset > aEnd || aEnd - aOffset < sizeof(UnsignedType))
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

std::string PathToUtf8(const std::filesystem::path& acPath)
{
    const auto utf8 = acPath.generic_u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::optional<std::filesystem::path> Utf8ToPath(const std::string& acUtf8)
{
    try
    {
        return std::filesystem::u8path(acUtf8);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> AbsoluteNormalized(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty())
        return std::nullopt;

    std::error_code ec;
    auto absolute = std::filesystem::absolute(acPath, ec);
    if (ec || absolute.empty())
        return std::nullopt;
    return absolute.lexically_normal();
}

bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acCandidate) noexcept
{
    return PartyQuestReplicaFilePlanner::IsContainedBy(
        acRoot.lexically_normal(),
        acCandidate.lexically_normal());
}

std::string LowerExtension(const std::filesystem::path& acPath)
{
    std::string extension = acPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char aCharacter)
    {
        return static_cast<char>(std::tolower(aCharacter));
    });
    return extension;
}

bool HasExpectedExtension(
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acPath)
{
    switch (aKind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
        return LowerExtension(acPath) == ".ess";
    case PartyQuestReplicaFileKind::SkseCosave:
        return LowerExtension(acPath) == ".skse";
    case PartyQuestReplicaFileKind::ExternalSidecar:
        return true;
    }

    return false;
}

bool IsExpectedPublishedRelativePath(
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acRelativePath) noexcept
{
    if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(acRelativePath))
        return false;

    const std::filesystem::path normalized = acRelativePath.lexically_normal();
    if (!HasExpectedExtension(aKind, normalized))
        return false;

    if (aKind == PartyQuestReplicaFileKind::ExternalSidecar)
        return IsInside(std::filesystem::path("sidecars") / "external", normalized);

    return normalized.parent_path() == "saves";
}

bool IsCheckpointType(PartyQuestReplicaSnapshotType aType) noexcept
{
    return aType == PartyQuestReplicaSnapshotType::Checkpoint ||
        aType == PartyQuestReplicaSnapshotType::RevisionCheckpoint;
}

bool IsRevisionCheckpointType(PartyQuestReplicaSnapshotType aType) noexcept
{
    return aType == PartyQuestReplicaSnapshotType::RevisionCheckpoint;
}

bool ValidateManifestData(const PartyQuestReplicaManifest& acManifest)
{
    if (!acManifest.CampaignId.IsValid() ||
        !acManifest.PlayerProfileId.IsValid() ||
        acManifest.Files.empty() ||
        acManifest.Files.size() > kMaxFiles)
    {
        return false;
    }

    if (static_cast<uint8_t>(acManifest.SnapshotType) >
        static_cast<uint8_t>(PartyQuestReplicaSnapshotType::RevisionCheckpoint))
    {
        return false;
    }
    if (static_cast<uint8_t>(acManifest.CheckpointKind) >
        static_cast<uint8_t>(PartyQuestCheckpointKind::LastKnownGood))
    {
        return false;
    }
    if (IsRevisionCheckpointType(acManifest.SnapshotType) && acManifest.CampaignWorldRevision == 0)
        return false;

    size_t mainSaveCount{};
    std::set<std::filesystem::path> relativePaths;
    for (const PartyQuestReplicaPublishedFile& file : acManifest.Files)
    {
        if (file.Digest == 0 ||
            !IsExpectedPublishedRelativePath(file.Kind, file.RelativePath) ||
            !relativePaths.emplace(file.RelativePath.lexically_normal()).second)
        {
            return false;
        }

        const std::string pathBytes = PathToUtf8(file.RelativePath.lexically_normal());
        if (pathBytes.empty() || pathBytes.size() > kMaxPathBytes)
            return false;

        if (file.Kind == PartyQuestReplicaFileKind::SkyrimSave)
            ++mainSaveCount;
    }

    return mainSaveCount == 1;
}

std::filesystem::path SnapshotRoot(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaSnapshotType aSnapshotType,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision)
{
    if (aSnapshotType == PartyQuestReplicaSnapshotType::ImportedReplica)
        return acPaths.PlayerDirectory;
    if (aSnapshotType == PartyQuestReplicaSnapshotType::RevisionCheckpoint)
    {
        return PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            acPaths,
            aKind,
            aCampaignWorldRevision);
    }
    return PartyQuestCoopSaveLayout::GetCheckpointDirectory(acPaths, aKind);
}

std::optional<std::filesystem::path> ExpectedDestinationRoot(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaFileKind aKind,
    PartyQuestReplicaSnapshotType aSnapshotType,
    PartyQuestCheckpointKind aCheckpointKind,
    uint64_t aCampaignWorldRevision)
{
    if (aSnapshotType == PartyQuestReplicaSnapshotType::ImportedReplica)
    {
        if (aKind == PartyQuestReplicaFileKind::ExternalSidecar)
            return acPaths.SidecarsDirectory / "external";
        return acPaths.SavesDirectory;
    }

    const std::filesystem::path checkpointRoot = SnapshotRoot(
        acPaths,
        aSnapshotType,
        aCheckpointKind,
        aCampaignWorldRevision);
    if (checkpointRoot.empty())
        return std::nullopt;
    if (aKind == PartyQuestReplicaFileKind::ExternalSidecar)
        return checkpointRoot / "sidecars" / "external";
    return checkpointRoot / "saves";
}

std::optional<PartyQuestReplicaManifest> BuildManifest(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    PartyQuestReplicaSnapshotType aSnapshotType,
    PartyQuestCheckpointKind aCheckpointKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan)
{
    if (!acCampaignId.IsValid() ||
        !acPlayerProfileId.IsValid() ||
        !acPlan.IsReady() ||
        acPlan.Operations.empty() ||
        (IsRevisionCheckpointType(aSnapshotType) && aCampaignWorldRevision == 0))
    {
        return std::nullopt;
    }

    const std::filesystem::path snapshotRootRaw = SnapshotRoot(
        acPaths,
        aSnapshotType,
        aCheckpointKind,
        aCampaignWorldRevision);
    const auto snapshotRoot = AbsoluteNormalized(snapshotRootRaw);
    if (!snapshotRoot)
        return std::nullopt;

    PartyQuestReplicaManifest manifest;
    manifest.CampaignId = acCampaignId;
    manifest.PlayerProfileId = acPlayerProfileId;
    manifest.SnapshotType = aSnapshotType;
    manifest.CheckpointKind = aCheckpointKind;
    manifest.CampaignWorldRevision = aCampaignWorldRevision;
    manifest.Files.reserve(acPlan.Operations.size());

    std::set<std::filesystem::path> relativePaths;
    for (const PartyQuestReplicaCopyOperation& operation : acPlan.Operations)
    {
        const auto destination = AbsoluteNormalized(operation.DestinationPath);
        const auto expectedRootRaw = ExpectedDestinationRoot(
            acPaths,
            operation.Kind,
            aSnapshotType,
            aCheckpointKind,
            aCampaignWorldRevision);
        const auto expectedRoot = expectedRootRaw ? AbsoluteNormalized(*expectedRootRaw) : std::nullopt;
        if (!destination || !expectedRoot ||
            !IsInside(*snapshotRoot, *destination) ||
            !IsInside(*expectedRoot, *destination) ||
            operation.ExpectedDigest == 0)
        {
            return std::nullopt;
        }

        const std::filesystem::path relative =
            destination->lexically_relative(*snapshotRoot).lexically_normal();
        if (!IsExpectedPublishedRelativePath(operation.Kind, relative) ||
            !relativePaths.emplace(relative).second)
        {
            return std::nullopt;
        }

        manifest.Files.push_back({
            operation.Kind,
            relative,
            operation.ExpectedSize,
            operation.ExpectedDigest});
    }

    std::sort(manifest.Files.begin(), manifest.Files.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.RelativePath.generic_string() < acRight.RelativePath.generic_string();
    });

    if (!ValidateManifestData(manifest))
        return std::nullopt;
    return manifest;
}

PartyQuestReplicaManifestPersistenceStatus ReadFile(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return std::filesystem::exists(acPath)
            ? PartyQuestReplicaManifestPersistenceStatus::IoError
            : PartyQuestReplicaManifestPersistenceStatus::FileNotFound;
    }

    const std::streampos end = file.tellg();
    if (end < 0)
        return PartyQuestReplicaManifestPersistenceStatus::IoError;
    const uint64_t size = static_cast<uint64_t>(end);
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return PartyQuestReplicaManifestPersistenceStatus::InvalidData;

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty() &&
        !file.read(reinterpret_cast<char*>(aBytes.data()), static_cast<std::streamsize>(aBytes.size())))
    {
        return PartyQuestReplicaManifestPersistenceStatus::IoError;
    }
    return PartyQuestReplicaManifestPersistenceStatus::Success;
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

PartyQuestReplicaManifestPersistenceResult DecodeFile(const std::filesystem::path& acPath)
{
    std::vector<uint8_t> bytes;
    PartyQuestReplicaManifestPersistenceResult result;
    result.Status = ReadFile(acPath, bytes);
    if (result.Status != PartyQuestReplicaManifestPersistenceStatus::Success)
        return result;
    return PartyQuestReplicaManifestStore::Decode(bytes);
}
} // namespace

std::optional<PartyQuestReplicaManifest> PartyQuestReplicaManifestStore::BuildImportManifest(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan)
{
    return BuildManifest(
        acPaths,
        acCampaignId,
        acPlayerProfileId,
        PartyQuestReplicaSnapshotType::ImportedReplica,
        PartyQuestCheckpointKind::PreJoin,
        aCampaignWorldRevision,
        acPlan);
}

std::optional<PartyQuestReplicaManifest> PartyQuestReplicaManifestStore::BuildCheckpointManifest(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan)
{
    return BuildManifest(
        acPaths,
        acCampaignId,
        acPlayerProfileId,
        PartyQuestReplicaSnapshotType::Checkpoint,
        aKind,
        aCampaignWorldRevision,
        acPlan);
}

std::optional<PartyQuestReplicaManifest> PartyQuestReplicaManifestStore::BuildRevisionCheckpointManifest(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan)
{
    return BuildManifest(
        acPaths,
        acCampaignId,
        acPlayerProfileId,
        PartyQuestReplicaSnapshotType::RevisionCheckpoint,
        aKind,
        aCampaignWorldRevision,
        acPlan);
}

std::filesystem::path PartyQuestReplicaManifestStore::GetImportManifestPath(
    const PartyQuestCoopSavePaths& acPaths)
{
    return acPaths.MetadataDirectory / "replica_manifest.bin";
}

std::filesystem::path PartyQuestReplicaManifestStore::GetCheckpointManifestPath(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind)
{
    return PartyQuestCoopSaveLayout::GetCheckpointDirectory(acPaths, aKind) / "manifest.bin";
}

std::filesystem::path PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision)
{
    return PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        acPaths,
        aKind,
        aCampaignWorldRevision) / "manifest.bin";
}

std::vector<uint8_t> PartyQuestReplicaManifestStore::Encode(
    const PartyQuestReplicaManifest& acManifest)
{
    if (!ValidateManifestData(acManifest))
        return {};

    std::vector<PartyQuestReplicaPublishedFile> files = acManifest.Files;
    std::sort(files.begin(), files.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.RelativePath.generic_string() < acRight.RelativePath.generic_string();
    });

    std::vector<uint8_t> payload;
    WriteInteger(payload, acManifest.CampaignId.High);
    WriteInteger(payload, acManifest.CampaignId.Low);
    WriteInteger(payload, acManifest.PlayerProfileId.High);
    WriteInteger(payload, acManifest.PlayerProfileId.Low);
    WriteInteger<uint8_t>(payload, static_cast<uint8_t>(acManifest.SnapshotType));
    WriteInteger<uint8_t>(payload, static_cast<uint8_t>(acManifest.CheckpointKind));
    WriteInteger(payload, acManifest.CampaignWorldRevision);
    WriteInteger<uint32_t>(payload, static_cast<uint32_t>(files.size()));

    for (const PartyQuestReplicaPublishedFile& file : files)
    {
        const std::string path = PathToUtf8(file.RelativePath.lexically_normal());
        if (path.empty() || path.size() > kMaxPathBytes)
            return {};

        WriteInteger<uint8_t>(payload, static_cast<uint8_t>(file.Kind));
        WriteInteger<uint32_t>(payload, static_cast<uint32_t>(path.size()));
        payload.insert(payload.end(), path.begin(), path.end());
        WriteInteger(payload, file.Size);
        WriteInteger(payload, file.Digest);
    }

    std::vector<uint8_t> archive;
    archive.reserve(kMagic.size() + sizeof(uint16_t) + sizeof(uint64_t) + payload.size() + sizeof(uint64_t));
    archive.insert(archive.end(), kMagic.begin(), kMagic.end());
    WriteInteger(archive, kFormatVersion);
    WriteInteger<uint64_t>(archive, payload.size());
    archive.insert(archive.end(), payload.begin(), payload.end());
    WriteInteger(archive, ComputeChecksum(payload.data(), payload.size()));
    return archive;
}

PartyQuestReplicaManifestPersistenceResult PartyQuestReplicaManifestStore::Decode(
    const std::vector<uint8_t>& acBytes)
{
    PartyQuestReplicaManifestPersistenceResult result;
    size_t offset{};

    if (acBytes.size() < kMagic.size())
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::Truncated;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidMagic;
        return result;
    }
    offset += kMagic.size();

    uint16_t version{};
    uint64_t payloadSize64{};
    if (!ReadInteger(acBytes, offset, acBytes.size(), version) ||
        !ReadInteger(acBytes, offset, acBytes.size(), payloadSize64))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::Truncated;
        return result;
    }
    if (version != kFormatVersion)
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::UnsupportedVersion;
        return result;
    }
    if (payloadSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) || offset > acBytes.size())
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
        return result;
    }

    const size_t payloadSize = static_cast<size_t>(payloadSize64);
    const size_t remaining = acBytes.size() - offset;
    if (payloadSize > remaining || remaining - payloadSize < sizeof(uint64_t))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::Truncated;
        return result;
    }
    if (remaining - payloadSize != sizeof(uint64_t))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
        return result;
    }

    const size_t payloadOffset = offset;
    const size_t payloadEnd = payloadOffset + payloadSize;
    size_t checksumOffset = payloadEnd;
    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, checksumOffset, acBytes.size(), storedChecksum) ||
        checksumOffset != acBytes.size())
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
        return result;
    }
    if (storedChecksum != ComputeChecksum(acBytes.data() + payloadOffset, payloadSize))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::ChecksumMismatch;
        return result;
    }

    PartyQuestReplicaManifest manifest;
    uint8_t snapshotType{};
    uint8_t checkpointKind{};
    uint32_t fileCount{};
    if (!ReadInteger(acBytes, offset, payloadEnd, manifest.CampaignId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, manifest.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, payloadEnd, manifest.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, manifest.PlayerProfileId.Low) ||
        !ReadInteger(acBytes, offset, payloadEnd, snapshotType) ||
        !ReadInteger(acBytes, offset, payloadEnd, checkpointKind) ||
        !ReadInteger(acBytes, offset, payloadEnd, manifest.CampaignWorldRevision) ||
        !ReadInteger(acBytes, offset, payloadEnd, fileCount))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::Truncated;
        return result;
    }

    if (snapshotType > static_cast<uint8_t>(PartyQuestReplicaSnapshotType::RevisionCheckpoint) ||
        checkpointKind > static_cast<uint8_t>(PartyQuestCheckpointKind::LastKnownGood) ||
        fileCount == 0 || fileCount > kMaxFiles)
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
        return result;
    }
    manifest.SnapshotType = static_cast<PartyQuestReplicaSnapshotType>(snapshotType);
    manifest.CheckpointKind = static_cast<PartyQuestCheckpointKind>(checkpointKind);
    manifest.Files.reserve(fileCount);

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        uint8_t kind{};
        uint32_t pathLength{};
        if (!ReadInteger(acBytes, offset, payloadEnd, kind) ||
            !ReadInteger(acBytes, offset, payloadEnd, pathLength) ||
            kind > static_cast<uint8_t>(PartyQuestReplicaFileKind::ExternalSidecar) ||
            pathLength == 0 || pathLength > kMaxPathBytes ||
            offset > payloadEnd || payloadEnd - offset < pathLength)
        {
            result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
            return result;
        }

        const std::string pathBytes(
            reinterpret_cast<const char*>(acBytes.data() + offset),
            pathLength);
        offset += pathLength;
        const auto relativePath = Utf8ToPath(pathBytes);
        if (!relativePath)
        {
            result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
            return result;
        }

        PartyQuestReplicaPublishedFile file;
        file.Kind = static_cast<PartyQuestReplicaFileKind>(kind);
        file.RelativePath = relativePath->lexically_normal();
        if (!ReadInteger(acBytes, offset, payloadEnd, file.Size) ||
            !ReadInteger(acBytes, offset, payloadEnd, file.Digest))
        {
            result.Status = PartyQuestReplicaManifestPersistenceStatus::Truncated;
            return result;
        }
        manifest.Files.push_back(std::move(file));
    }

    if (offset != payloadEnd || !ValidateManifestData(manifest))
    {
        result.Status = PartyQuestReplicaManifestPersistenceStatus::InvalidData;
        return result;
    }

    result.Status = PartyQuestReplicaManifestPersistenceStatus::Success;
    result.Manifest = std::move(manifest);
    return result;
}

PartyQuestReplicaManifestPersistenceStatus PartyQuestReplicaManifestStore::SaveAtomically(
    const std::filesystem::path& acPath,
    const PartyQuestReplicaManifest& acManifest)
{
    const std::vector<uint8_t> encoded = Encode(acManifest);
    if (encoded.empty())
        return PartyQuestReplicaManifestPersistenceStatus::InvalidData;

    std::error_code ec;
    if (!acPath.parent_path().empty())
    {
        std::filesystem::create_directories(acPath.parent_path(), ec);
        if (ec)
            return PartyQuestReplicaManifestPersistenceStatus::IoError;
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
        return PartyQuestReplicaManifestPersistenceStatus::IoError;
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
            return PartyQuestReplicaManifestPersistenceStatus::IoError;
        }
    }

    std::filesystem::rename(temporaryPath, acPath, ec);
    if (ec)
    {
        std::error_code restoreError;
        if (hadPrimary && std::filesystem::exists(backupPath, restoreError))
            std::filesystem::rename(backupPath, acPath, restoreError);
        std::filesystem::remove(temporaryPath, restoreError);
        return PartyQuestReplicaManifestPersistenceStatus::IoError;
    }

    return PartyQuestReplicaManifestPersistenceStatus::Success;
}

PartyQuestReplicaManifestPersistenceResult PartyQuestReplicaManifestStore::Load(
    const std::filesystem::path& acPath)
{
    PartyQuestReplicaManifestPersistenceResult primary = DecodeFile(acPath);
    if (primary.Status == PartyQuestReplicaManifestPersistenceStatus::Success)
        return primary;

    auto temporaryPath = acPath;
    temporaryPath += ".tmp";
    PartyQuestReplicaManifestPersistenceResult temporary = DecodeFile(temporaryPath);
    if (temporary.Status == PartyQuestReplicaManifestPersistenceStatus::Success)
    {
        temporary.UsedTemporary = true;
        return temporary;
    }

    auto backupPath = acPath;
    backupPath += ".bak";
    PartyQuestReplicaManifestPersistenceResult backup = DecodeFile(backupPath);
    if (backup.Status == PartyQuestReplicaManifestPersistenceStatus::Success)
    {
        backup.Status = PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired;
        backup.UsedBackup = true;
        return backup;
    }

    return primary;
}

PartyQuestReplicaManifestVerificationStatus PartyQuestReplicaManifestStore::VerifyPublishedFiles(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const PartyQuestReplicaManifest& acManifest) noexcept
{
    try
    {
        if (!acExpectedCampaignId.IsValid() ||
            !acExpectedPlayerProfileId.IsValid() ||
            acManifest.CampaignId != acExpectedCampaignId ||
            acManifest.PlayerProfileId != acExpectedPlayerProfileId)
        {
            return PartyQuestReplicaManifestVerificationStatus::InvalidIdentity;
        }

        if (!ValidateManifestData(acManifest))
            return PartyQuestReplicaManifestVerificationStatus::InvalidManifest;

        const std::filesystem::path rootRaw = SnapshotRoot(
            acPaths,
            acManifest.SnapshotType,
            acManifest.CheckpointKind,
            acManifest.CampaignWorldRevision);
        const auto root = AbsoluteNormalized(rootRaw);
        if (!root)
            return PartyQuestReplicaManifestVerificationStatus::PathEscape;

        std::error_code ec;
        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(*root, ec);
        if (ec || canonicalRoot.empty())
            return PartyQuestReplicaManifestVerificationStatus::MissingOrChangedFile;

        for (const PartyQuestReplicaPublishedFile& file : acManifest.Files)
        {
            const auto candidate = AbsoluteNormalized(rootRaw / file.RelativePath);
            if (!candidate || !IsInside(*root, *candidate))
                return PartyQuestReplicaManifestVerificationStatus::PathEscape;

            const std::filesystem::path canonicalParent =
                std::filesystem::weakly_canonical(candidate->parent_path(), ec);
            if (ec || !IsInside(canonicalRoot, canonicalParent))
                return PartyQuestReplicaManifestVerificationStatus::PathEscape;

            const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(*candidate);
            if (!observation ||
                observation->Size != file.Size ||
                observation->Digest != file.Digest)
            {
                return PartyQuestReplicaManifestVerificationStatus::MissingOrChangedFile;
            }
        }

        return PartyQuestReplicaManifestVerificationStatus::Verified;
    }
    catch (...)
    {
        return PartyQuestReplicaManifestVerificationStatus::InvalidManifest;
    }
}
