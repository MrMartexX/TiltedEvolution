#include <Structs/Skyrim/PartyQuestReplicaRestore.h>

#include <set>

namespace
{
bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acCandidate) noexcept
{
    return PartyQuestReplicaFilePlanner::IsContainedBy(
        acRoot.lexically_normal(),
        acCandidate.lexically_normal());
}

std::optional<std::filesystem::path> BuildReplicaDestination(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaPublishedFile& acFile)
{
    const std::filesystem::path relative = acFile.RelativePath.lexically_normal();
    if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
        return std::nullopt;

    switch (acFile.Kind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
    case PartyQuestReplicaFileKind::SkseCosave:
        if (relative.parent_path() != "saves" || relative.filename().empty())
            return std::nullopt;
        return (acPaths.SavesDirectory / relative.filename()).lexically_normal();

    case PartyQuestReplicaFileKind::ExternalSidecar:
    {
        const std::filesystem::path externalRoot = std::filesystem::path("sidecars") / "external";
        if (!IsInside(externalRoot, relative))
            return std::nullopt;
        const std::filesystem::path pluginRelative = relative.lexically_relative(externalRoot).lexically_normal();
        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(pluginRelative))
            return std::nullopt;
        return (acPaths.SidecarsDirectory / "external" / pluginRelative).lexically_normal();
    }
    }

    return std::nullopt;
}
} // namespace

PartyQuestReplicaRestorePlan PartyQuestReplicaRestorePlanner::Build(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const PartyQuestReplicaManifest& acCheckpointManifest) noexcept
{
    PartyQuestReplicaRestorePlan plan;

    try
    {
        if (!acExpectedCampaignId.IsValid() ||
            !acExpectedPlayerProfileId.IsValid() ||
            acCheckpointManifest.CampaignId != acExpectedCampaignId ||
            acCheckpointManifest.PlayerProfileId != acExpectedPlayerProfileId)
        {
            plan.Status = PartyQuestReplicaRestorePlanStatus::InvalidIdentity;
            return plan;
        }

        if (acCheckpointManifest.SnapshotType != PartyQuestReplicaSnapshotType::Checkpoint)
        {
            plan.Status = PartyQuestReplicaRestorePlanStatus::NotCheckpointManifest;
            return plan;
        }

        if (PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                acPaths,
                acExpectedCampaignId,
                acExpectedPlayerProfileId,
                acCheckpointManifest) != PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            plan.Status = PartyQuestReplicaRestorePlanStatus::CheckpointVerificationFailed;
            return plan;
        }

        const std::filesystem::path checkpointRoot =
            PartyQuestCoopSaveLayout::GetCheckpointDirectory(
                acPaths,
                acCheckpointManifest.CheckpointKind).lexically_normal();
        const std::filesystem::path playerRoot = acPaths.PlayerDirectory.lexically_normal();
        if (checkpointRoot.empty() || playerRoot.empty())
        {
            plan.Status = PartyQuestReplicaRestorePlanStatus::InvalidCheckpointPath;
            return plan;
        }

        plan.CampaignId = acExpectedCampaignId;
        plan.PlayerProfileId = acExpectedPlayerProfileId;
        plan.CheckpointKind = acCheckpointManifest.CheckpointKind;
        plan.CampaignWorldRevision = acCheckpointManifest.CampaignWorldRevision;
        plan.Operations.reserve(acCheckpointManifest.Files.size());

        std::set<std::filesystem::path> destinations;
        for (const PartyQuestReplicaPublishedFile& file : acCheckpointManifest.Files)
        {
            const std::filesystem::path source =
                (checkpointRoot / file.RelativePath).lexically_normal();
            if (!IsInside(checkpointRoot, source))
            {
                plan.Status = PartyQuestReplicaRestorePlanStatus::InvalidCheckpointPath;
                plan.Operations.clear();
                return plan;
            }

            const auto destination = BuildReplicaDestination(acPaths, file);
            if (!destination ||
                !IsInside(playerRoot, *destination) ||
                IsInside(acPaths.CheckpointsDirectory, *destination) ||
                *destination == acPaths.RuntimeApplySidecar.lexically_normal())
            {
                plan.Status = PartyQuestReplicaRestorePlanStatus::InvalidDestinationPath;
                plan.Operations.clear();
                return plan;
            }

            if (!destinations.emplace(*destination).second)
            {
                plan.Status = PartyQuestReplicaRestorePlanStatus::DuplicateDestination;
                plan.Operations.clear();
                return plan;
            }

            plan.Operations.push_back({
                file.Kind,
                source,
                *destination,
                file.Size,
                file.Digest});
        }

        if (plan.Operations.empty())
        {
            plan.Status = PartyQuestReplicaRestorePlanStatus::CheckpointVerificationFailed;
            return plan;
        }

        plan.Status = PartyQuestReplicaRestorePlanStatus::Ready;
        return plan;
    }
    catch (...)
    {
        plan.Status = PartyQuestReplicaRestorePlanStatus::InvalidCheckpointPath;
        plan.Operations.clear();
        return plan;
    }
}
