#include <Structs/Skyrim/PartyQuestPreRepairAuthorizationCommit.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'P', 'R', 'A', 'C', 'M'};
constexpr uint16_t kFormatVersion = 1;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kHeaderBytes = kMagic.size() + sizeof(uint16_t) + sizeof(uint64_t);
constexpr size_t kChecksumBytes = sizeof(uint64_t);
constexpr uint32_t kMaxFiles =
    static_cast<uint32_t>(PartyQuestReplicaResourcePolicy::MaxFiles);
constexpr uint32_t kMaxPathBytes =
    PartyQuestDurableResourcePolicy::MaxSerializedPathBytes;
constexpr uint32_t kKnownApplyActions =
    static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) |
    static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForWorldTargets) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
    static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify) |
    static_cast<uint32_t>(PartyQuestApplyAction::AdapterManaged);

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
    if (aEnd > acBytes.size() ||
        aOffset > aEnd ||
        aEnd - aOffset < sizeof(UnsignedType))
    {
        return false;
    }

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

std::optional<std::string> PathToUtf8(
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        const auto utf8 = acPath.generic_u8string();
        return std::string(
            reinterpret_cast<const char*>(utf8.data()),
            utf8.size());
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> Utf8ToPath(
    const std::string& acUtf8) noexcept
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

bool IsCanonicalRelativePathText(const std::string& acPath) noexcept
{
    if (acPath.empty() ||
        acPath.size() > kMaxPathBytes ||
        acPath.front() == '/' ||
        acPath.front() == '\\' ||
        acPath.back() == '/' ||
        acPath.find('\\') != std::string::npos ||
        acPath.find(':') != std::string::npos ||
        acPath.find('\0') != std::string::npos)
    {
        return false;
    }

    size_t start{};
    while (start < acPath.size())
    {
        const size_t separator = acPath.find('/', start);
        const size_t end =
            separator == std::string::npos ? acPath.size() : separator;
        if (end == start)
            return false;

        const std::string_view component(acPath.data() + start, end - start);
        if (component == "." || component == "..")
            return false;

        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }
    return true;
}

std::string LowerExtension(const std::filesystem::path& acPath)
{
    std::string extension = acPath.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char aCharacter)
        {
            return static_cast<char>(std::tolower(aCharacter));
        });
    return extension;
}

bool IsKnownFileKind(PartyQuestReplicaFileKind aKind) noexcept
{
    return aKind == PartyQuestReplicaFileKind::SkyrimSave ||
        aKind == PartyQuestReplicaFileKind::SkseCosave ||
        aKind == PartyQuestReplicaFileKind::ExternalSidecar;
}

bool IsExpectedRelativePath(
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acRelativePath) noexcept
{
    try
    {
        if (!IsKnownFileKind(aKind) ||
            !PartyQuestReplicaFilePlanner::IsSafeRelativePath(acRelativePath))
        {
            return false;
        }

        const auto text = PathToUtf8(acRelativePath);
        const auto normalizedText =
            PathToUtf8(acRelativePath.lexically_normal());
        if (!text || !normalizedText ||
            *text != *normalizedText ||
            !IsCanonicalRelativePathText(*text))
        {
            return false;
        }

        if (aKind == PartyQuestReplicaFileKind::SkyrimSave)
        {
            return acRelativePath.parent_path() == "saves" &&
                LowerExtension(acRelativePath) == ".ess";
        }

        if (aKind == PartyQuestReplicaFileKind::SkseCosave)
        {
            return acRelativePath.parent_path() == "saves" &&
                LowerExtension(acRelativePath) == ".skse";
        }

        const std::filesystem::path externalRoot =
            std::filesystem::path("sidecars") / "external";
        return acRelativePath != externalRoot &&
            PartyQuestReplicaFilePlanner::IsContainedBy(
                externalRoot,
                acRelativePath);
    }
    catch (...)
    {
        return false;
    }
}

bool HasKnownActions(PartyQuestApplyAction aActions) noexcept
{
    const uint32_t actions = static_cast<uint32_t>(aActions);
    return actions != 0 && (actions & ~kKnownApplyActions) == 0;
}

bool ValidateRecord(
    const PartyQuestPreRepairAuthorizationCommit& acCommit,
    bool aRequireCanonicalFileOrder) noexcept
{
    try
    {
        if (!acCommit.CampaignId.IsValid() ||
            !acCommit.PlayerProfileId.IsValid() ||
            acCommit.TransactionId == 0 ||
            acCommit.RuntimeGeneration == 0 ||
            acCommit.CaptureEpochId == 0 ||
            acCommit.TargetWorldRevision == 0 ||
            !static_cast<bool>(acCommit.QuestId) ||
            acCommit.CanonicalDigest == 0 ||
            acCommit.SidecarManifestFingerprint == 0 ||
            !HasKnownActions(acCommit.Actions) ||
            acCommit.ExpectedVerification.SchemaVersion !=
                PartyQuestVerificationEnvelopeV1::kSchemaVersion ||
            acCommit.ExpectedVerification.QuestSnapshotDigest !=
                acCommit.CanonicalDigest ||
            !PartyQuestVerificationPolicy::IsCompleteForActions(
                acCommit.ExpectedVerification,
                acCommit.Actions) ||
            acCommit.Files.empty() ||
            acCommit.Files.size() > kMaxFiles)
        {
            return false;
        }

        size_t mainSaveCount{};
        uint64_t totalSize{};
        std::set<std::string> relativePaths;
        std::string previousPath;

        for (const auto& file : acCommit.Files)
        {
            if (!IsKnownFileKind(file.Kind) ||
                file.Digest == 0 ||
                file.Size > PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes ||
                file.Size >
                    PartyQuestReplicaResourcePolicy::MaxTotalFileBytes - totalSize ||
                !IsExpectedRelativePath(file.Kind, file.RelativePath))
            {
                return false;
            }
            totalSize += file.Size;

            const auto pathText = PathToUtf8(file.RelativePath);
            if (!pathText ||
                pathText->empty() ||
                pathText->size() > kMaxPathBytes ||
                !relativePaths.emplace(*pathText).second)
            {
                return false;
            }

            if (aRequireCanonicalFileOrder &&
                !previousPath.empty() &&
                previousPath >= *pathText)
            {
                return false;
            }
            previousPath = *pathText;

            if (file.Kind == PartyQuestReplicaFileKind::SkyrimSave)
                ++mainSaveCount;
        }

        return mainSaveCount == 1 &&
            totalSize <= PartyQuestReplicaResourcePolicy::MaxTotalFileBytes;
    }
    catch (...)
    {
        return false;
    }
}

std::optional<PartyQuestPreRepairAuthorizationCommit> Canonicalize(
    const PartyQuestPreRepairAuthorizationCommit& acCommit) noexcept
{
    try
    {
        PartyQuestPreRepairAuthorizationCommit canonical = acCommit;
        std::sort(
            canonical.Files.begin(),
            canonical.Files.end(),
            [](const auto& acLeft, const auto& acRight)
            {
                const auto left = PathToUtf8(acLeft.RelativePath);
                const auto right = PathToUtf8(acRight.RelativePath);
                if (!left || !right)
                    return static_cast<uint8_t>(acLeft.Kind) <
                        static_cast<uint8_t>(acRight.Kind);
                if (*left != *right)
                    return *left < *right;
                return static_cast<uint8_t>(acLeft.Kind) <
                    static_cast<uint8_t>(acRight.Kind);
            });

        if (!ValidateRecord(canonical, true))
            return std::nullopt;
        return canonical;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

PartyQuestPreRepairAuthorizationCommitPersistenceStatus ReadArchive(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes) noexcept
{
    try
    {
        if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(acPath))
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded;
        }

        std::error_code ec;
        const auto node = std::filesystem::symlink_status(acPath, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory ||
                ec == std::errc::not_a_directory)
            {
                return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    FileNotFound;
            }
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;
        }

        if (node.type() == std::filesystem::file_type::not_found)
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                FileNotFound;
        }
        if (std::filesystem::is_symlink(node) ||
            !std::filesystem::is_regular_file(node))
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;
        }

        std::ifstream file(acPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;

        const std::streampos end = file.tellg();
        if (end < 0)
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;

        const uint64_t size = static_cast<uint64_t>(end);
        if (size > PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes ||
            size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded;
        }

        aBytes.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        if (!aBytes.empty() &&
            !file.read(
                reinterpret_cast<char*>(aBytes.data()),
                static_cast<std::streamsize>(aBytes.size())))
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;
        }

        ec.clear();
        const auto afterRead = std::filesystem::symlink_status(acPath, ec);
        if (ec ||
            std::filesystem::is_symlink(afterRead) ||
            !std::filesystem::is_regular_file(afterRead))
        {
            return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;
        }

        return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success;
    }
    catch (...)
    {
        return PartyQuestPreRepairAuthorizationCommitPersistenceStatus::IoError;
    }
}

PartyQuestPreRepairAuthorizationCommitLoadResult DecodeFile(
    const std::filesystem::path& acPath) noexcept
{
    std::vector<uint8_t> bytes;
    PartyQuestPreRepairAuthorizationCommitLoadResult result;
    result.Status = ReadArchive(acPath, bytes);
    if (result.Status !=
        PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success)
    {
        return result;
    }

    try
    {
        return PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes);
    }
    catch (...)
    {
        result.Status =
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData;
        return result;
    }
}

PartyQuestPreRepairAuthorizationCommitPublishStatus MapStableFailure(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    return aStatus == PartyQuestStableStorageStatus::Unsupported
        ? PartyQuestPreRepairAuthorizationCommitPublishStatus::
              StableStorageUnsupported
        : PartyQuestPreRepairAuthorizationCommitPublishStatus::
              StableStorageFailure;
}

PartyQuestPreRepairAuthorizationCommitPublishStatus ReestablishExistingDurability(
    const std::filesystem::path& acFinalPath,
    const PartyQuestPreRepairAuthorizationCommit& acCanonical) noexcept
{
    auto stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
        acFinalPath.parent_path());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableFailure(stable);

    stable = PartyQuestStableStorage::FlushFile(acFinalPath);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableFailure(stable);

    stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
        acFinalPath.parent_path());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableFailure(stable);

    const auto reloaded = DecodeFile(acFinalPath);
    if (reloaded.Status !=
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success ||
        !reloaded.Record ||
        *reloaded.Record != acCanonical)
    {
        return PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict;
    }

    return PartyQuestPreRepairAuthorizationCommitPublishStatus::AlreadyCommitted;
}

bool HookContinues(
    const PartyQuestPreRepairAuthorizationCommitHooks& acHooks,
    PartyQuestPreRepairAuthorizationCommitBoundary aBoundary) noexcept
{
    return acHooks.Invoke(aBoundary) ==
        PartyQuestPreRepairAuthorizationCommitDirective::Continue;
}
} // namespace

std::filesystem::path
PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTargetWorldRevision)
{
    return PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
               acPaths,
               PartyQuestCheckpointKind::PreRepair,
               aTargetWorldRevision) /
        "pre_repair_commit.bin";
}

std::vector<uint8_t> PartyQuestPreRepairAuthorizationCommitStore::Encode(
    const PartyQuestPreRepairAuthorizationCommit& acCommit)
{
    try
    {
        const auto canonical = Canonicalize(acCommit);
        if (!canonical)
            return {};

        std::vector<uint8_t> payload;
        payload.reserve(256 + canonical->Files.size() * 64);

        WriteInteger(payload, canonical->CampaignId.High);
        WriteInteger(payload, canonical->CampaignId.Low);
        WriteInteger(payload, canonical->PlayerProfileId.High);
        WriteInteger(payload, canonical->PlayerProfileId.Low);
        WriteInteger(payload, canonical->TransactionId);
        WriteInteger(payload, canonical->RuntimeGeneration);
        WriteInteger(payload, canonical->CaptureEpochId);
        WriteInteger(payload, canonical->TargetWorldRevision);
        WriteInteger(payload, canonical->QuestId.ModId);
        WriteInteger(payload, canonical->QuestId.BaseId);
        WriteInteger(payload, canonical->CanonicalDigest);
        WriteInteger(payload, canonical->SidecarManifestFingerprint);
        WriteInteger(payload, static_cast<uint32_t>(canonical->Actions));

        WriteInteger(payload, canonical->ExpectedVerification.SchemaVersion);
        WriteInteger(
            payload,
            static_cast<uint32_t>(canonical->ExpectedVerification.Required));
        WriteInteger(payload, canonical->ExpectedVerification.QuestSnapshotDigest);
        WriteInteger(payload, canonical->ExpectedVerification.AliasDigest);
        WriteInteger(payload, canonical->ExpectedVerification.InventoryEffectsDigest);
        WriteInteger(payload, canonical->ExpectedVerification.WorldEffectsDigest);
        WriteInteger(payload, canonical->ExpectedVerification.AdapterStateDigest);
        WriteInteger(
            payload,
            canonical->ExpectedVerification.CompatibilityFingerprint);

        WriteInteger(payload, static_cast<uint32_t>(canonical->Files.size()));
        for (const auto& file : canonical->Files)
        {
            const auto pathText = PathToUtf8(file.RelativePath);
            if (!pathText || pathText->size() > kMaxPathBytes)
                return {};

            WriteInteger(payload, static_cast<uint8_t>(file.Kind));
            WriteInteger(payload, static_cast<uint32_t>(pathText->size()));
            payload.insert(payload.end(), pathText->begin(), pathText->end());
            WriteInteger(payload, file.Size);
            WriteInteger(payload, file.Digest);
        }

        const uint64_t maxArchive =
            PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes;
        if (payload.size() >
            maxArchive - kHeaderBytes - kChecksumBytes)
        {
            return {};
        }

        std::vector<uint8_t> bytes;
        bytes.reserve(kHeaderBytes + payload.size() + kChecksumBytes);
        bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
        WriteInteger(bytes, kFormatVersion);
        WriteInteger(bytes, static_cast<uint64_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        WriteInteger(
            bytes,
            ComputeChecksum(payload.data(), payload.size()));
        return bytes;
    }
    catch (...)
    {
        return {};
    }
}

PartyQuestPreRepairAuthorizationCommitLoadResult
PartyQuestPreRepairAuthorizationCommitStore::Decode(
    const std::vector<uint8_t>& acBytes)
{
    PartyQuestPreRepairAuthorizationCommitLoadResult result;

    try
    {
        if (acBytes.size() >
            PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    ResourceLimitExceeded;
            return result;
        }

        if (acBytes.size() < kMagic.size())
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }

        if (!std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidMagic;
            return result;
        }

        if (acBytes.size() < kHeaderBytes + kChecksumBytes)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }

        size_t offset = kMagic.size();
        uint16_t version{};
        if (!ReadInteger(acBytes, offset, acBytes.size(), version))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }
        if (version != kFormatVersion)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    UnsupportedVersion;
            return result;
        }

        uint64_t payloadLength{};
        if (!ReadInteger(acBytes, offset, acBytes.size(), payloadLength))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }

        const uint64_t maxArchive =
            PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes;
        if (payloadLength >
            maxArchive - kHeaderBytes - kChecksumBytes)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    ResourceLimitExceeded;
            return result;
        }

        const uint64_t expectedArchiveSize =
            kHeaderBytes + payloadLength + kChecksumBytes;
        if (expectedArchiveSize > acBytes.size())
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }
        if (expectedArchiveSize != acBytes.size())
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData;
            return result;
        }

        const size_t payloadStart = kHeaderBytes;
        const size_t payloadEnd =
            payloadStart + static_cast<size_t>(payloadLength);
        size_t checksumOffset = payloadEnd;
        uint64_t encodedChecksum{};
        if (!ReadInteger(
                acBytes,
                checksumOffset,
                acBytes.size(),
                encodedChecksum))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }
        if (encodedChecksum !=
            ComputeChecksum(
                acBytes.data() + payloadStart,
                static_cast<size_t>(payloadLength)))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    ChecksumMismatch;
            return result;
        }

        PartyQuestPreRepairAuthorizationCommit record;
        offset = payloadStart;
        uint32_t actions{};
        uint32_t required{};
        if (!ReadInteger(acBytes, offset, payloadEnd, record.CampaignId.High) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.CampaignId.Low) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.PlayerProfileId.High) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.PlayerProfileId.Low) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.TransactionId) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.RuntimeGeneration) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.CaptureEpochId) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.TargetWorldRevision) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.QuestId.ModId) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.QuestId.BaseId) ||
            !ReadInteger(acBytes, offset, payloadEnd, record.CanonicalDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.SidecarManifestFingerprint) ||
            !ReadInteger(acBytes, offset, payloadEnd, actions) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.SchemaVersion) ||
            !ReadInteger(acBytes, offset, payloadEnd, required) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.QuestSnapshotDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.AliasDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.InventoryEffectsDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.WorldEffectsDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.AdapterStateDigest) ||
            !ReadInteger(
                acBytes,
                offset,
                payloadEnd,
                record.ExpectedVerification.CompatibilityFingerprint))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }

        record.Actions = static_cast<PartyQuestApplyAction>(actions);
        record.ExpectedVerification.Required =
            static_cast<PartyQuestVerificationComponent>(required);

        uint32_t fileCount{};
        if (!ReadInteger(acBytes, offset, payloadEnd, fileCount))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated;
            return result;
        }
        if (fileCount > kMaxFiles)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    ResourceLimitExceeded;
            return result;
        }
        if (fileCount == 0)
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData;
            return result;
        }

        record.Files.reserve(fileCount);
        for (uint32_t index = 0; index < fileCount; ++index)
        {
            uint8_t kind{};
            uint32_t pathLength{};
            if (!ReadInteger(acBytes, offset, payloadEnd, kind) ||
                !ReadInteger(acBytes, offset, payloadEnd, pathLength))
            {
                result.Status =
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        Truncated;
                return result;
            }

            if (pathLength > kMaxPathBytes)
            {
                result.Status =
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        ResourceLimitExceeded;
                return result;
            }
            if (pathLength == 0 ||
                offset > payloadEnd ||
                pathLength > payloadEnd - offset)
            {
                result.Status =
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        InvalidData;
                return result;
            }

            const std::string pathText(
                reinterpret_cast<const char*>(acBytes.data() + offset),
                pathLength);
            offset += pathLength;
            const auto relativePath = Utf8ToPath(pathText);
            if (!relativePath)
            {
                result.Status =
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        InvalidData;
                return result;
            }

            PartyQuestPreRepairAuthorizationCommitFile file;
            file.Kind = static_cast<PartyQuestReplicaFileKind>(kind);
            file.RelativePath = *relativePath;
            if (!ReadInteger(acBytes, offset, payloadEnd, file.Size) ||
                !ReadInteger(acBytes, offset, payloadEnd, file.Digest))
            {
                result.Status =
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        Truncated;
                return result;
            }
            record.Files.push_back(std::move(file));
        }

        if (offset != payloadEnd || !ValidateRecord(record, true))
        {
            result.Status =
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData;
            return result;
        }

        result.Status =
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success;
        result.Record = std::move(record);
        return result;
    }
    catch (...)
    {
        result.Status =
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData;
        result.Record.reset();
        return result;
    }
}

PartyQuestPreRepairAuthorizationCommitLoadResult
PartyQuestPreRepairAuthorizationCommitStore::Load(
    const std::filesystem::path& acFinalPath)
{
    return DecodeFile(acFinalPath);
}

PartyQuestPreRepairAuthorizationCommitPublishStatus
PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestPreRepairAuthorizationCommit& acCommit,
    PartyQuestPreRepairAuthorizationCommitHooks aHooks) noexcept
{
    try
    {
        const auto canonical = Canonicalize(acCommit);
        if (!canonical ||
            !PartyQuestCoopSaveLayout::Matches(
                acPaths,
                canonical->CampaignId,
                canonical->PlayerProfileId))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                InvalidCommit;
        }

        const auto finalPath =
            GetCommitPath(acPaths, canonical->TargetWorldRevision);
        auto temporaryPath = finalPath;
        temporaryPath += ".tmp";

        if (!PartyQuestDurableResourcePolicy::
                IsMutableFilesystemPathWithinBudget(finalPath) ||
            !PartyQuestDurableResourcePolicy::
                IsFilesystemPathWithinBudget(temporaryPath))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                ResourceLimitExceeded;
        }

        const auto existing = DecodeFile(finalPath);
        if (existing.Status ==
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success)
        {
            if (!existing.Record || *existing.Record != *canonical)
            {
                return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    Conflict;
            }
            return ReestablishExistingDurability(finalPath, *canonical);
        }
        if (existing.Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded)
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                ResourceLimitExceeded;
        }
        if (existing.Status !=
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                FileNotFound)
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict;
        }

        const auto encoded = Encode(*canonical);
        if (encoded.empty())
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                InvalidCommit;
        }

        auto stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
            finalPath.parent_path());
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableFailure(stable);
        if (!HookContinues(
                aHooks,
                PartyQuestPreRepairAuthorizationCommitBoundary::
                    DirectoryDurable))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                Interrupted;
        }

        stable = PartyQuestStableStorage::WriteFileDurably(
            temporaryPath,
            encoded.data(),
            encoded.size());
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableFailure(stable);
        if (!HookContinues(
                aHooks,
                PartyQuestPreRepairAuthorizationCommitBoundary::
                    TemporaryDurablyWritten))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                Interrupted;
        }

        const auto temporary = DecodeFile(temporaryPath);
        if (temporary.Status !=
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success ||
            !temporary.Record ||
            *temporary.Record != *canonical)
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                StableStorageFailure;
        }
        if (!HookContinues(
                aHooks,
                PartyQuestPreRepairAuthorizationCommitBoundary::
                    TemporaryVerified))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                Interrupted;
        }

        stable = PartyQuestStableStorage::PublishFileRename(
            temporaryPath,
            finalPath,
            false);
        if (stable != PartyQuestStableStorageStatus::Success)
        {
            const auto afterFailure = DecodeFile(finalPath);
            if (afterFailure.Status ==
                    PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                        Success &&
                afterFailure.Record &&
                *afterFailure.Record == *canonical)
            {
                return ReestablishExistingDurability(finalPath, *canonical);
            }
            if (afterFailure.Status !=
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    FileNotFound)
            {
                return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    Conflict;
            }
            return MapStableFailure(stable);
        }

        if (!HookContinues(
                aHooks,
                PartyQuestPreRepairAuthorizationCommitBoundary::
                    FinalPublished))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                Interrupted;
        }

        const auto final = DecodeFile(finalPath);
        if (final.Status !=
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success ||
            !final.Record ||
            *final.Record != *canonical)
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict;
        }

        if (!HookContinues(
                aHooks,
                PartyQuestPreRepairAuthorizationCommitBoundary::
                    FinalVerified))
        {
            return PartyQuestPreRepairAuthorizationCommitPublishStatus::
                Interrupted;
        }

        return PartyQuestPreRepairAuthorizationCommitPublishStatus::Published;
    }
    catch (...)
    {
        return PartyQuestPreRepairAuthorizationCommitPublishStatus::
            StableStorageFailure;
    }
}
