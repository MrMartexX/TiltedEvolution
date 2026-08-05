#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace
{
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& aHash, const void* apData, size_t aSize) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(apData);
    for (size_t i = 0; i < aSize; ++i)
    {
        aHash ^= bytes[i];
        aHash *= kFnvPrime;
    }
}

template <class T>
void HashValue(uint64_t& aHash, const T& acValue) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    HashBytes(aHash, &acValue, sizeof(T));
}

void HashString(uint64_t& aHash, const std::string& acValue) noexcept
{
    const uint64_t size = static_cast<uint64_t>(acValue.size());
    HashValue(aHash, size);
    if (!acValue.empty())
        HashBytes(aHash, acValue.data(), acValue.size());
}

bool AuthorizationMatches(
    const PartyQuestCheckpointSidecarAuthorization& acAuthorization,
    const PartyQuestCheckpointSidecarRequirement& acRequirement) noexcept
{
    return acAuthorization.IsVerified() &&
        acAuthorization.GetCapabilityId() == acRequirement.CapabilityId &&
        acAuthorization.GetSchemaVersion() == acRequirement.SchemaVersion &&
        acAuthorization.GetProviderFingerprint() == acRequirement.ProviderFingerprint &&
        acAuthorization.GetRestoreAdapterFingerprint() ==
            acRequirement.RestoreAdapterFingerprint;
}

PartyQuestCheckpointSidecarMirrorResult Fail(
    PartyQuestCheckpointSidecarMirrorStatus aStatus,
    uint64_t aCapabilityId = 0,
    const std::filesystem::path& acPath = {})
{
    PartyQuestCheckpointSidecarMirrorResult result;
    result.Status = aStatus;
    result.FailedCapabilityId = aCapabilityId;
    result.FailedPath = acPath;
    return result;
}

bool IsRegularNonSymlink(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        return !ec &&
            !std::filesystem::is_symlink(status) &&
            std::filesystem::is_regular_file(status);
    }
    catch (...)
    {
        return false;
    }
}

bool IsDirectoryNonSymlink(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        return !ec &&
            !std::filesystem::is_symlink(status) &&
            std::filesystem::is_directory(status);
    }
    catch (...)
    {
        return false;
    }
}

bool HasExpectedCapabilityPrefix(
    const std::filesystem::path& acRelativePath,
    uint64_t aCapabilityId)
{
    if (acRelativePath.empty())
        return false;

    const std::string expected =
        PartyQuestCheckpointSidecarMirrorCollector::FormatCapabilityDirectory(
            aCapabilityId);
    const auto first = acRelativePath.begin();
    return first != acRelativePath.end() && first->string() == expected;
}
} // namespace

uint64_t PartyQuestCheckpointSidecarMirrorAuthorization::ComputeManifestFingerprint(
    const PartyQuestCheckpointSidecarManifest& acManifest) noexcept
{
    try
    {
        uint64_t hash = kFnvOffset;
        const auto requirements = acManifest.GetRequirements();
        const uint64_t count = static_cast<uint64_t>(requirements.size());
        HashValue(hash, count);
        for (const auto& requirement : requirements)
        {
            HashValue(hash, requirement.CapabilityId);
            HashValue(hash, requirement.SchemaVersion);
            HashValue(hash, requirement.ProviderFingerprint);
            HashValue(hash, requirement.RestoreAdapterFingerprint);
            const auto mode = static_cast<uint8_t>(requirement.Mode);
            HashValue(hash, mode);
        }
        return hash != 0 ? hash : 1;
    }
    catch (...)
    {
        return 0;
    }
}

uint64_t PartyQuestCheckpointSidecarMirrorAuthorization::ComputeFilesFingerprint(
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
{
    try
    {
        uint64_t hash = kFnvOffset;
        const uint64_t count = static_cast<uint64_t>(acFiles.size());
        HashValue(hash, count);
        for (const auto& file : acFiles)
        {
            const auto kind = static_cast<uint8_t>(file.Kind);
            HashValue(hash, kind);
            HashString(hash, file.SourcePath.lexically_normal().generic_string());
            HashString(hash, file.RelativePath.lexically_normal().generic_string());
            HashValue(hash, file.Size);
            HashValue(hash, file.Digest);
        }
        return hash != 0 ? hash : 1;
    }
    catch (...)
    {
        return 0;
    }
}

PartyQuestCheckpointSidecarMirrorAuthorization::
PartyQuestCheckpointSidecarMirrorAuthorization(
    const PartyQuestCheckpointSidecarManifest& acManifest,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
    : m_transactionId(aTransactionId)
    , m_targetWorldRevision(aTargetWorldRevision)
    , m_manifestFingerprint(ComputeManifestFingerprint(acManifest))
    , m_filesFingerprint(ComputeFilesFingerprint(acFiles))
    , m_fileCount(acFiles.size())
    , m_verified(
          aTransactionId != 0 &&
          aTargetWorldRevision != 0 &&
          m_manifestFingerprint != 0 &&
          m_filesFingerprint != 0)
{
}

PartyQuestCheckpointSidecarMirrorAuthorization::
PartyQuestCheckpointSidecarMirrorAuthorization(
    const PartyQuestCheckpointSidecarManifest& acManifest,
    const PartyQuestCheckpointCaptureEpoch& acEpoch,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
    : m_captureEpochId(acEpoch.GetEpochId())
    , m_transactionId(acEpoch.GetTransactionId())
    , m_targetWorldRevision(acEpoch.GetTargetWorldRevision())
    , m_manifestFingerprint(ComputeManifestFingerprint(acManifest))
    , m_filesFingerprint(ComputeFilesFingerprint(acFiles))
    , m_fileCount(acFiles.size())
    , m_verified(
          acEpoch.IsVerified() &&
          m_captureEpochId != 0 &&
          m_transactionId != 0 &&
          m_targetWorldRevision != 0 &&
          m_manifestFingerprint != 0 &&
          m_manifestFingerprint == acEpoch.GetSidecarManifestFingerprint() &&
          m_filesFingerprint != 0)
{
}

bool PartyQuestCheckpointSidecarMirrorAuthorization::Matches(
    const PartyQuestCheckpointSidecarManifest& acManifest,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) const noexcept
{
    if (!m_verified ||
        aTransactionId != m_transactionId ||
        aTargetWorldRevision != m_targetWorldRevision ||
        acFiles.size() != m_fileCount)
    {
        return false;
    }

    return ComputeManifestFingerprint(acManifest) == m_manifestFingerprint &&
        ComputeFilesFingerprint(acFiles) == m_filesFingerprint;
}

bool PartyQuestCheckpointSidecarMirrorAuthorization::Matches(
    const PartyQuestCheckpointSidecarManifest& acManifest,
    const PartyQuestCheckpointCaptureEpoch& acEpoch,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) const noexcept
{
    if (!m_verified ||
        m_captureEpochId == 0 ||
        !acEpoch.IsVerified() ||
        acEpoch.GetEpochId() != m_captureEpochId ||
        acEpoch.GetTransactionId() != m_transactionId ||
        acEpoch.GetTargetWorldRevision() != m_targetWorldRevision ||
        acEpoch.GetSidecarManifestFingerprint() != m_manifestFingerprint ||
        acFiles.size() != m_fileCount)
    {
        return false;
    }

    return ComputeManifestFingerprint(acManifest) == m_manifestFingerprint &&
        ComputeFilesFingerprint(acFiles) == m_filesFingerprint;
}

std::string PartyQuestCheckpointSidecarMirrorCollector::FormatCapabilityDirectory(
    uint64_t aCapabilityId)
{
    if (aCapabilityId == 0)
        return {};

    std::array<char, 28> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "Capability_%016llX",
        static_cast<unsigned long long>(aCapabilityId));
    return buffer.data();
}

PartyQuestCheckpointSidecarMirrorResult
PartyQuestCheckpointSidecarMirrorCollector::Collect(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCheckpointSidecarManifest& acManifest,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const std::vector<PartyQuestCheckpointSidecarCapture>& acCaptures) noexcept
{
    try
    {
        if (aTransactionId == 0 || aTargetWorldRevision == 0)
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::InvalidContext);

        if (acPaths.PlayerDirectory.empty() ||
            acPaths.SidecarsDirectory.empty() ||
            !acPaths.PlayerDirectory.is_absolute() ||
            !acPaths.SidecarsDirectory.is_absolute() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(
                acPaths.PlayerDirectory,
                acPaths.SidecarsDirectory))
        {
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::InvalidLayout);
        }

        const std::filesystem::path externalRoot =
            acPaths.SidecarsDirectory / "external";

        std::unordered_map<uint64_t, const PartyQuestCheckpointSidecarCapture*> captures;
        captures.reserve(acCaptures.size());
        for (const auto& capture : acCaptures)
        {
            if (!capture.Authorization.IsVerified())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::InvalidAuthorization,
                    capture.Authorization.GetCapabilityId());
            }

            const uint64_t capabilityId = capture.Authorization.GetCapabilityId();
            const auto* requirement = acManifest.FindRequirement(capabilityId);
            if (!requirement)
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::UnexpectedCapability,
                    capabilityId);
            }

            if (!AuthorizationMatches(capture.Authorization, *requirement))
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::AuthorizationMismatch,
                    capabilityId);
            }

            if (!captures.emplace(capabilityId, &capture).second)
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::DuplicateCapabilityCapture,
                    capabilityId);
            }
        }

        // Required/optional coverage is resolved before filesystem access. An
        // absent optional provider does not require sidecars/external to exist.
        for (const auto& requirement : acManifest.GetRequirements())
        {
            const auto it = captures.find(requirement.CapabilityId);
            if (it == captures.end())
            {
                if (requirement.Mode == PartyQuestCheckpointSidecarRequirementMode::Required)
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::MissingRequiredCapture,
                        requirement.CapabilityId);
                }
                continue;
            }

            const auto& capture = *it->second;
            if (capture.TransactionId != aTransactionId)
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::TransactionMismatch,
                    requirement.CapabilityId);
            }
            if (capture.TargetWorldRevision != aTargetWorldRevision)
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::WorldRevisionMismatch,
                    requirement.CapabilityId);
            }
            if (capture.MirrorRelativeFiles.empty())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::EmptyCapture,
                    requirement.CapabilityId);
            }
        }

        PartyQuestCheckpointSidecarMirrorResult result;
        if (captures.empty())
        {
            result.Status = PartyQuestCheckpointSidecarMirrorStatus::Ready;
            result.Authorization = PartyQuestCheckpointSidecarMirrorAuthorization(
                acManifest,
                aTransactionId,
                aTargetWorldRevision,
                result.Files);
            return result;
        }

        if (!IsDirectoryNonSymlink(externalRoot))
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::MirrorRootUnavailable);

        std::error_code ec;
        const auto canonicalPlayer = std::filesystem::weakly_canonical(
            acPaths.PlayerDirectory,
            ec);
        if (ec || canonicalPlayer.empty())
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::InvalidLayout);

        ec.clear();
        const auto canonicalExternal = std::filesystem::weakly_canonical(
            externalRoot,
            ec);
        if (ec || canonicalExternal.empty() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(
                canonicalPlayer,
                canonicalExternal))
        {
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::MirrorEscape);
        }

        std::set<std::filesystem::path> seenSources;
        std::set<std::filesystem::path> seenRelativeFiles;

        for (const auto& requirement : acManifest.GetRequirements())
        {
            const auto captureIt = captures.find(requirement.CapabilityId);
            if (captureIt == captures.end())
                continue;

            const auto& capture = *captureIt->second;
            for (const auto& relativePath : capture.MirrorRelativeFiles)
            {
                if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relativePath))
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::InvalidRelativePath,
                        requirement.CapabilityId,
                        relativePath);
                }

                if (!HasExpectedCapabilityPrefix(
                        relativePath,
                        requirement.CapabilityId))
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::CapabilityPathMismatch,
                        requirement.CapabilityId,
                        relativePath);
                }

                const auto source = (externalRoot / relativePath).lexically_normal();
                if (!seenSources.emplace(source).second ||
                    !seenRelativeFiles.emplace(relativePath.lexically_normal()).second)
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::DuplicateFile,
                        requirement.CapabilityId,
                        relativePath);
                }

                if (!IsRegularNonSymlink(source))
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed,
                        requirement.CapabilityId,
                        source);
                }

                ec.clear();
                const auto canonicalSource = std::filesystem::weakly_canonical(
                    source,
                    ec);
                if (ec || canonicalSource.empty() ||
                    !PartyQuestReplicaFilePlanner::IsContainedBy(
                        canonicalExternal,
                        canonicalSource))
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::MirrorEscape,
                        requirement.CapabilityId,
                        source);
                }

                const auto file = PartyQuestReplicaFileExecutor::InspectSource(
                    PartyQuestReplicaFileKind::ExternalSidecar,
                    source,
                    relativePath);
                if (!file || file->Digest == 0)
                {
                    return Fail(
                        PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed,
                        requirement.CapabilityId,
                        source);
                }

                result.Files.push_back(*file);
            }
        }

        std::sort(result.Files.begin(), result.Files.end(), [](const auto& acLeft, const auto& acRight)
        {
            if (acLeft.RelativePath != acRight.RelativePath)
                return acLeft.RelativePath.generic_string() < acRight.RelativePath.generic_string();
            return acLeft.SourcePath.generic_string() < acRight.SourcePath.generic_string();
        });

        result.Status = PartyQuestCheckpointSidecarMirrorStatus::Ready;
        result.Authorization = PartyQuestCheckpointSidecarMirrorAuthorization(
            acManifest,
            aTransactionId,
            aTargetWorldRevision,
            result.Files);
        if (!result.Authorization.IsVerified())
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed);
        return result;
    }
    catch (...)
    {
        return Fail(PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed);
    }
}

PartyQuestCheckpointSidecarMirrorResult
PartyQuestCheckpointSidecarMirrorCollector::Collect(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCheckpointSidecarManifest& acManifest,
    const PartyQuestCheckpointCaptureEpoch& acEpoch,
    const std::vector<PartyQuestCheckpointSidecarCapture>& acCaptures) noexcept
{
    try
    {
        const uint64_t manifestFingerprint =
            PartyQuestCheckpointSidecarMirrorAuthorization::ComputeManifestFingerprint(
                acManifest);
        if (!acEpoch.IsVerified() ||
            manifestFingerprint == 0 ||
            !acEpoch.MatchesContext(
                acEpoch.GetTransactionId(),
                acEpoch.GetTargetWorldRevision(),
                manifestFingerprint))
        {
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::InvalidContext);
        }

        for (const auto& capture : acCaptures)
        {
            const uint64_t capabilityId = capture.Authorization.GetCapabilityId();
            if (!capture.Authorization.SupportsCoherentCapture())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::CaptureConsistencyUnavailable,
                    capabilityId);
            }
            if (capture.CaptureEpochId != acEpoch.GetEpochId())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::CaptureEpochMismatch,
                    capabilityId);
            }
            if (capture.TransactionId != acEpoch.GetTransactionId())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::TransactionMismatch,
                    capabilityId);
            }
            if (capture.TargetWorldRevision != acEpoch.GetTargetWorldRevision())
            {
                return Fail(
                    PartyQuestCheckpointSidecarMirrorStatus::WorldRevisionMismatch,
                    capabilityId);
            }
        }

        auto result = Collect(
            acPaths,
            acManifest,
            acEpoch.GetTransactionId(),
            acEpoch.GetTargetWorldRevision(),
            acCaptures);
        if (!result.IsReady())
            return result;

        result.Authorization = PartyQuestCheckpointSidecarMirrorAuthorization(
            acManifest,
            acEpoch,
            result.Files);
        if (!result.Authorization.IsVerified())
            return Fail(PartyQuestCheckpointSidecarMirrorStatus::InvalidContext);
        return result;
    }
    catch (...)
    {
        return Fail(PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed);
    }
}
