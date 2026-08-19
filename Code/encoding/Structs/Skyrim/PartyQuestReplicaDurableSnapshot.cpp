#include <Structs/Skyrim/PartyQuestReplicaDurableSnapshot.h>

#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <filesystem>
#include <optional>
#include <system_error>

namespace
{
std::optional<std::filesystem::path> AbsoluteNormalized(
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || absolute.empty())
            return std::nullopt;
        return absolute;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        return PartyQuestReplicaFilePlanner::IsContainedBy(
            acRoot.lexically_normal(),
            acPath.lexically_normal());
    }
    catch (...)
    {
        return false;
    }
}

PartyQuestReplicaDurableSnapshotResult StableFailure(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    PartyQuestReplicaDurableSnapshotResult result;
    result.Status = aStatus == PartyQuestStableStorageStatus::Unsupported
        ? PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform
        : PartyQuestReplicaDurableSnapshotStatus::StableStorageFailure;
    return result;
}
} // namespace

PartyQuestReplicaDurableSnapshotResult
PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision) noexcept
{
    PartyQuestReplicaDurableSnapshotResult result;
    if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
    {
        result.Status = PartyQuestReplicaDurableSnapshotStatus::InvalidIdentity;
        return result;
    }
    if (aCampaignWorldRevision == 0)
    {
        result.Status = PartyQuestReplicaDurableSnapshotStatus::InvalidRevision;
        return result;
    }

#ifndef _WIN32
    try
    {
        const auto manifestPath =
            PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                acPaths,
                aKind,
                aCampaignWorldRevision);
        const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
        result.ManifestStatus = loaded.Status;
        if (loaded.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
            !loaded.Manifest)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestUnavailable;
            return result;
        }

        const PartyQuestReplicaManifest manifest = *loaded.Manifest;
        if (manifest.CampaignId != acCampaignId ||
            manifest.PlayerProfileId != acPlayerProfileId)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::InvalidIdentity;
            return result;
        }
        if (manifest.SnapshotType != PartyQuestReplicaSnapshotType::RevisionCheckpoint ||
            manifest.CheckpointKind != aKind ||
            manifest.CampaignWorldRevision != aCampaignWorldRevision)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestInvalid;
            return result;
        }

        result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            manifest);
        if (result.VerificationStatus != PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::FileVerificationFailed;
            return result;
        }

        const auto root = AbsoluteNormalized(
            PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
                acPaths,
                aKind,
                aCampaignWorldRevision));
        if (!root)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestInvalid;
            return result;
        }

        auto stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(*root);
        if (stable != PartyQuestStableStorageStatus::Success)
            return StableFailure(stable);

        for (const auto& file : manifest.Files)
        {
            const auto candidate = AbsoluteNormalized(*root / file.RelativePath);
            if (!candidate || !IsInside(*root, *candidate))
            {
                result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestInvalid;
                return result;
            }

            stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
                candidate->parent_path());
            if (stable != PartyQuestStableStorageStatus::Success)
                return StableFailure(stable);

            // The existing copy executor may have used ordinary stream/copy and
            // rename operations. These barriers promote the exact final bytes
            // and namespace before manifest authority is republished.
            stable = PartyQuestStableStorage::FlushFile(*candidate);
            if (stable != PartyQuestStableStorageStatus::Success)
                return StableFailure(stable);
            stable = PartyQuestStableStorage::FlushParentDirectory(*candidate);
            if (stable != PartyQuestStableStorageStatus::Success)
                return StableFailure(stable);

            const auto observation =
                PartyQuestReplicaFileExecutor::ObserveRegularFile(*candidate);
            if (!observation ||
                observation->Size != file.Size ||
                observation->Digest != file.Digest)
            {
                result.Status = PartyQuestReplicaDurableSnapshotStatus::FileVerificationFailed;
                return result;
            }
        }

        stable = PartyQuestStableStorage::EnsureDirectoryTreeDurably(
            manifestPath.parent_path());
        if (stable != PartyQuestStableStorageStatus::Success)
            return StableFailure(stable);

        result.ManifestStatus = PartyQuestReplicaManifestStore::SavePowerLossDurably(
            manifestPath,
            manifest);
        if (result.ManifestStatus != PartyQuestReplicaManifestPersistenceStatus::Success)
        {
            result.Status = result.ManifestStatus ==
                    PartyQuestReplicaManifestPersistenceStatus::PowerLossDurabilityUnsupported
                ? PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform
                : PartyQuestReplicaDurableSnapshotStatus::ManifestPersistenceFailed;
            return result;
        }

        const auto durable = PartyQuestReplicaManifestStore::Load(manifestPath);
        result.ManifestStatus = durable.Status;
        if (durable.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
            !durable.Manifest || *durable.Manifest != manifest)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestPersistenceFailed;
            return result;
        }

        result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            manifest);
        if (result.VerificationStatus != PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            result.Status = PartyQuestReplicaDurableSnapshotStatus::FileVerificationFailed;
            return result;
        }

        result.Status = PartyQuestReplicaDurableSnapshotStatus::Promoted;
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestReplicaDurableSnapshotStatus::ManifestInvalid;
        return result;
    }
#else
    (void)acPaths;
    (void)aKind;
    result.Status = PartyQuestReplicaDurableSnapshotStatus::UnsupportedPlatform;
    result.ManifestStatus =
        PartyQuestReplicaManifestPersistenceStatus::PowerLossDurabilityUnsupported;
    return result;
#endif
}
