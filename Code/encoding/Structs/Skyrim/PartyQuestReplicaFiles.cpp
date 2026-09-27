#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>

namespace
{
bool HasBoundedPathBytes(
    const std::filesystem::path& acPath,
    size_t aMaxBytes) noexcept
{
    try
    {
        const auto value = acPath.lexically_normal().generic_u8string();
        return !value.empty() && value.size() <= aMaxBytes;
    }
    catch (...)
    {
        return false;
    }
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

bool HasExpectedExtension(const PartyQuestReplicaFileSpec& acFile)
{
    switch (acFile.Kind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
        return LowerExtension(acFile.RelativePath) == ".ess";
    case PartyQuestReplicaFileKind::SkseCosave:
        return LowerExtension(acFile.RelativePath) == ".skse";
    case PartyQuestReplicaFileKind::ExternalSidecar:
        return true;
    }

    return false;
}

std::filesystem::path BuildImportDestination(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaFileSpec& acFile)
{
    switch (acFile.Kind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
    case PartyQuestReplicaFileKind::SkseCosave:
        return acPaths.SavesDirectory / acFile.RelativePath;
    case PartyQuestReplicaFileKind::ExternalSidecar:
        return acPaths.SidecarsDirectory / "external" / acFile.RelativePath;
    }

    return {};
}

std::filesystem::path BuildCheckpointDestination(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaFileSpec& acFile)
{
    const std::filesystem::path checkpointRoot = aRevisionScoped
        ? PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
              acPaths,
              aKind,
              aCampaignWorldRevision)
        : PartyQuestCoopSaveLayout::GetCheckpointDirectory(acPaths, aKind);

    switch (acFile.Kind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
    case PartyQuestReplicaFileKind::SkseCosave:
        return checkpointRoot / "saves" / acFile.RelativePath;
    case PartyQuestReplicaFileKind::ExternalSidecar:
        return checkpointRoot / "sidecars" / "external" / acFile.RelativePath;
    }

    return {};
}

PartyQuestReplicaCopyPlan BuildPlan(
    const PartyQuestCoopSavePaths& acPaths,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision)
{
    PartyQuestReplicaCopyPlan plan;

    if (acPaths.PlayerDirectory.empty() ||
        acPaths.SavesDirectory.empty() ||
        acPaths.SidecarsDirectory.empty() ||
        acPaths.CheckpointsDirectory.empty())
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidLayout;
        return plan;
    }

    if (aCheckpoint && aRevisionScoped && aCampaignWorldRevision == 0)
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidWorldRevision;
        return plan;
    }

    if (acFiles.empty())
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::MissingMainSave;
        return plan;
    }

    if (acFiles.size() > PartyQuestReplicaResourcePolicy::MaxFiles)
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::ResourceFileCountExceeded;
        return plan;
    }

    size_t mainSaveCount{};
    uint64_t totalSize{};
    std::set<std::filesystem::path> sources;
    std::set<std::filesystem::path> destinations;

    plan.Operations.reserve(acFiles.size());
    for (const PartyQuestReplicaFileSpec& file : acFiles)
    {
        if (file.SourcePath.empty())
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidSource;
            plan.Operations.clear();
            return plan;
        }

        if (file.Kind == PartyQuestReplicaFileKind::SkyrimSave)
            ++mainSaveCount;

        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(file.RelativePath))
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidRelativePath;
            plan.Operations.clear();
            return plan;
        }

        // Main save/co-save paths are flat by design. External sidecars may use
        // a nested relative path to preserve a plugin-specific directory.
        if ((file.Kind == PartyQuestReplicaFileKind::SkyrimSave ||
             file.Kind == PartyQuestReplicaFileKind::SkseCosave) &&
            file.RelativePath.has_parent_path())
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidRelativePath;
            plan.Operations.clear();
            return plan;
        }

        if (!HasExpectedExtension(file))
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::InvalidExtension;
            plan.Operations.clear();
            return plan;
        }

        if (file.Digest == 0)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::MissingDigest;
            plan.Operations.clear();
            return plan;
        }

        if (file.Size > PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::ResourceFileSizeExceeded;
            plan.Operations.clear();
            return plan;
        }

        if (!PartyQuestReplicaResourcePolicy::IsPathWithinBudget(file.SourcePath) ||
            !PartyQuestReplicaResourcePolicy::IsPathWithinBudget(file.RelativePath))
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::ResourcePathLengthExceeded;
            plan.Operations.clear();
            return plan;
        }
        if (file.Size > std::numeric_limits<uint64_t>::max() - totalSize)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::ResourceTotalSizeExceeded;
            plan.Operations.clear();
            return plan;
        }
        totalSize += file.Size;
        if (totalSize > PartyQuestReplicaResourcePolicy::MaxTotalFileBytes)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::ResourceTotalSizeExceeded;
            plan.Operations.clear();
            return plan;
        }

        const std::filesystem::path normalizedSource = file.SourcePath.lexically_normal();
        if (!sources.emplace(normalizedSource).second)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::DuplicateSource;
            plan.Operations.clear();
            return plan;
        }

        const std::filesystem::path destination = (
            aCheckpoint
                ? BuildCheckpointDestination(
                      acPaths,
                      aCheckpointKind,
                      aRevisionScoped,
                      aCampaignWorldRevision,
                      file)
                : BuildImportDestination(acPaths, file)).lexically_normal();

        if (!PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(destination))
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::ResourcePathLengthExceeded;
            plan.Operations.clear();
            return plan;
        }

        if (!PartyQuestReplicaFilePlanner::IsContainedBy(acPaths.PlayerDirectory, destination))
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::DestinationEscapesPlayerRoot;
            plan.Operations.clear();
            return plan;
        }

        if (normalizedSource == destination)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::SourceDestinationCollision;
            plan.Operations.clear();
            return plan;
        }

        if (!destinations.emplace(destination).second)
        {
            plan.Status = PartyQuestReplicaCopyPlanStatus::DuplicateDestination;
            plan.Operations.clear();
            return plan;
        }

        plan.Operations.push_back({
            file.Kind,
            normalizedSource,
            destination,
            file.Size,
            file.Digest});
    }

    if (mainSaveCount == 0)
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::MissingMainSave;
        plan.Operations.clear();
        return plan;
    }

    if (mainSaveCount != 1)
    {
        plan.Status = PartyQuestReplicaCopyPlanStatus::MultipleMainSaves;
        plan.Operations.clear();
        return plan;
    }

    std::sort(plan.Operations.begin(), plan.Operations.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.DestinationPath.generic_string() < acRight.DestinationPath.generic_string();
    });

    plan.Status = PartyQuestReplicaCopyPlanStatus::Ready;
    return plan;
}
} // namespace

bool PartyQuestReplicaFilePlanner::IsSafeRelativePath(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty() || acPath.is_absolute() || acPath.has_root_name() || acPath.has_root_directory())
        return false;

    const std::filesystem::path normalized = acPath.lexically_normal();
    if (normalized.empty() || normalized == ".")
        return false;

    for (const auto& component : normalized)
    {
        if (component == ".." || component == "." || component.empty())
            return false;
    }

    return true;
}

bool PartyQuestReplicaFilePlanner::IsContainedBy(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acCandidate) noexcept
{
    if (acRoot.empty() || acCandidate.empty())
        return false;

    const std::filesystem::path root = acRoot.lexically_normal();
    const std::filesystem::path candidate = acCandidate.lexically_normal();

    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt)
    {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt)
            return false;
    }

    return true;
}

PartyQuestReplicaCopyPlan PartyQuestReplicaFilePlanner::BuildImportPlan(
    const PartyQuestCoopSavePaths& acPaths,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles)
{
    return BuildPlan(
        acPaths,
        acFiles,
        false,
        PartyQuestCheckpointKind::PreJoin,
        false,
        0);
}

PartyQuestReplicaCopyPlan PartyQuestReplicaFilePlanner::BuildCheckpointPlan(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    const std::vector<PartyQuestReplicaFileSpec>& acReplicaFiles)
{
    return BuildPlan(acPaths, acReplicaFiles, true, aKind, false, 0);
}

PartyQuestReplicaCopyPlan PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const std::vector<PartyQuestReplicaFileSpec>& acReplicaFiles)
{
    return BuildPlan(
        acPaths,
        acReplicaFiles,
        true,
        aKind,
        true,
        aCampaignWorldRevision);
}

std::optional<uint64_t> PartyQuestReplicaResourcePolicy::RequiredFreeBytes(
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    if (!acPlan.IsReady() || acPlan.Operations.empty() || acPlan.Operations.size() > MaxFiles)
        return std::nullopt;

    uint64_t totalSize{};
    for (const auto& operation : acPlan.Operations)
    {
        if (!IsPathWithinBudget(operation.SourcePath) ||
            !IsMutablePathWithinBudget(operation.DestinationPath) ||
            operation.ExpectedSize > MaxIndividualFileBytes ||
            operation.ExpectedSize > std::numeric_limits<uint64_t>::max() - totalSize)
        {
            return std::nullopt;
        }
        totalSize += operation.ExpectedSize;
        if (totalSize > MaxTotalFileBytes)
            return std::nullopt;
    }

    if (totalSize > std::numeric_limits<uint64_t>::max() / RequiredFreeSpaceMultiplier)
        return std::nullopt;
    const uint64_t multiplied = totalSize * RequiredFreeSpaceMultiplier;
    if (multiplied > std::numeric_limits<uint64_t>::max() - MinimumFreeSpaceReserveBytes)
        return std::nullopt;
    return multiplied + MinimumFreeSpaceReserveBytes;
}

bool PartyQuestReplicaResourcePolicy::IsPathWithinBudget(
    const std::filesystem::path& acPath) noexcept
{
    return HasBoundedPathBytes(acPath, MaxFilesystemPathBytes);
}

bool PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(
    const std::filesystem::path& acPath) noexcept
{
    return HasBoundedPathBytes(acPath, MaxMutablePathBytes);
}

bool PartyQuestReplicaResourcePolicy::HasSufficientDiskSpace(
    const PartyQuestReplicaCopyPlan& acPlan,
    uint64_t aAvailableBytes) noexcept
{
    const auto required = RequiredFreeBytes(acPlan);
    return required && aAvailableBytes >= *required;
}

std::optional<PartyQuestReplicaCheckpointManifest>
PartyQuestReplicaFilePlanner::BuildCheckpointManifest(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan)
{
    if (!acCampaignId.IsValid() ||
        !acPlayerProfileId.IsValid() ||
        aCampaignWorldRevision == 0 ||
        !acPlan.IsReady() ||
        acPlan.Operations.empty())
    {
        return std::nullopt;
    }

    PartyQuestReplicaCheckpointManifest manifest;
    manifest.CampaignId = acCampaignId;
    manifest.PlayerProfileId = acPlayerProfileId;
    manifest.Kind = aKind;
    manifest.CampaignWorldRevision = aCampaignWorldRevision;
    manifest.Files = acPlan.Operations;
    return manifest;
}
