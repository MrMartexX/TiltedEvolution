#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>

#include <array>
#include <cstdio>
#include <set>
#include <unordered_map>

namespace
{
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

        if (captures.empty())
        {
            PartyQuestCheckpointSidecarMirrorResult result;
            result.Status = PartyQuestCheckpointSidecarMirrorStatus::Ready;
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

        PartyQuestCheckpointSidecarMirrorResult result;
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

        result.Status = PartyQuestCheckpointSidecarMirrorStatus::Ready;
        return result;
    }
    catch (...)
    {
        return Fail(PartyQuestCheckpointSidecarMirrorStatus::SourceInspectionFailed);
    }
}
