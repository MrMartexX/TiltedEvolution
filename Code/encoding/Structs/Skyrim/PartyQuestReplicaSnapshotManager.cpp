#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <utility>

namespace
{
PartyQuestReplicaSnapshotResult Failure(
    PartyQuestReplicaSnapshotStatus aStatus) noexcept
{
    PartyQuestReplicaSnapshotResult result;
    result.Status = aStatus;
    return result;
}
} // namespace

PartyQuestReplicaSnapshotManager::PartyQuestReplicaSnapshotManager(
    PartyQuestCoopSavePaths aPaths,
    PartyQuestCampaignId aCampaignId,
    PartyQuestPlayerProfileId aPlayerProfileId)
    : m_paths(std::move(aPaths))
    , m_campaignId(aCampaignId)
    , m_playerProfileId(aPlayerProfileId)
{
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::EnsureImportedReplica(
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    return Ensure(false, PartyQuestCheckpointKind::PreJoin, aCampaignWorldRevision, acPlan);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::EnsureCheckpoint(
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    return Ensure(true, aKind, aCampaignWorldRevision, acPlan);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::ValidateImportedReplica() const noexcept
{
    return Validate(false, PartyQuestCheckpointKind::PreJoin);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::ValidateCheckpoint(
    PartyQuestCheckpointKind aKind) const noexcept
{
    return Validate(true, aKind);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::Ensure(
    bool aCheckpoint,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    try
    {
        if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid())
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidIdentity);
        if (!acPlan.IsReady() || acPlan.Operations.empty())
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidPlan);

        const auto expectedManifest = aCheckpoint
            ? PartyQuestReplicaManifestStore::BuildCheckpointManifest(
                  m_paths,
                  m_campaignId,
                  m_playerProfileId,
                  aKind,
                  aCampaignWorldRevision,
                  acPlan)
            : PartyQuestReplicaManifestStore::BuildImportManifest(
                  m_paths,
                  m_campaignId,
                  m_playerProfileId,
                  aCampaignWorldRevision,
                  acPlan);
        if (!expectedManifest)
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidPlan);

        const std::filesystem::path manifestPath = aCheckpoint
            ? PartyQuestReplicaManifestStore::GetCheckpointManifestPath(m_paths, aKind)
            : PartyQuestReplicaManifestStore::GetImportManifestPath(m_paths);

        const auto existing = PartyQuestReplicaManifestStore::Load(manifestPath);
        if (existing.Status == PartyQuestReplicaManifestPersistenceStatus::Success)
        {
            PartyQuestReplicaSnapshotResult result;
            result.ManifestStatus = existing.Status;
            if (!existing.Manifest)
            {
                result.Status = PartyQuestReplicaSnapshotStatus::ManifestInvalid;
                return result;
            }

            result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
                m_paths,
                m_campaignId,
                m_playerProfileId,
                *existing.Manifest);
            if (result.VerificationStatus == PartyQuestReplicaManifestVerificationStatus::InvalidIdentity)
            {
                result.Status = PartyQuestReplicaSnapshotStatus::ExistingSnapshotConflict;
                return result;
            }
            if (result.VerificationStatus != PartyQuestReplicaManifestVerificationStatus::Verified)
            {
                result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
                return result;
            }

            result.Status = *existing.Manifest == *expectedManifest
                ? PartyQuestReplicaSnapshotStatus::AlreadyReady
                : PartyQuestReplicaSnapshotStatus::ExistingSnapshotConflict;
            return result;
        }

        if (existing.Status == PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired)
        {
            PartyQuestReplicaSnapshotResult result;
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestRecoveryRequired;
            result.ManifestStatus = existing.Status;
            return result;
        }
        if (existing.Status != PartyQuestReplicaManifestPersistenceStatus::FileNotFound)
        {
            PartyQuestReplicaSnapshotResult result;
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestInvalid;
            result.ManifestStatus = existing.Status;
            return result;
        }

        PartyQuestReplicaSnapshotResult result;
        const PartyQuestReplicaExecutionReport copy = aCheckpoint
            ? PartyQuestReplicaFileExecutor::ExecuteCheckpoint(m_paths, aKind, acPlan)
            : PartyQuestReplicaFileExecutor::ExecuteImport(m_paths, acPlan);
        result.CopyStatus = copy.Status;

        if (!copy.IsSuccess())
        {
            if (copy.Status != PartyQuestReplicaExecutionStatus::DestinationExists)
            {
                result.Status = PartyQuestReplicaSnapshotStatus::CopyFailed;
                return result;
            }

            // A previous process may have published every final file and died
            // before persisting the completion manifest. Adopt only the exact
            // complete byte set; partial/conflicting destinations remain blocked.
            const PartyQuestReplicaExecutionReport existingFiles = aCheckpoint
                ? PartyQuestReplicaFileExecutor::VerifyCheckpoint(m_paths, aKind, acPlan)
                : PartyQuestReplicaFileExecutor::VerifyImport(m_paths, acPlan);
            if (!existingFiles.IsSuccess())
            {
                result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
                result.CopyStatus = existingFiles.Status;
                return result;
            }
            result.AdoptedVerifiedFiles = true;
        }

        const PartyQuestReplicaExecutionReport finalVerification = aCheckpoint
            ? PartyQuestReplicaFileExecutor::VerifyCheckpoint(m_paths, aKind, acPlan)
            : PartyQuestReplicaFileExecutor::VerifyImport(m_paths, acPlan);
        result.CopyStatus = finalVerification.Status;
        if (!finalVerification.IsSuccess())
        {
            result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
            return result;
        }

        result.ManifestStatus = PartyQuestReplicaManifestStore::SaveAtomically(
            manifestPath,
            *expectedManifest);
        if (result.ManifestStatus != PartyQuestReplicaManifestPersistenceStatus::Success)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestPersistenceFailed;
            return result;
        }

        const auto durable = PartyQuestReplicaManifestStore::Load(manifestPath);
        result.ManifestStatus = durable.Status;
        if (durable.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
            !durable.Manifest ||
            *durable.Manifest != *expectedManifest)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestPersistenceFailed;
            return result;
        }

        result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
            m_paths,
            m_campaignId,
            m_playerProfileId,
            *durable.Manifest);
        if (result.VerificationStatus != PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
            return result;
        }

        result.Status = PartyQuestReplicaSnapshotStatus::Ready;
        return result;
    }
    catch (...)
    {
        return Failure(PartyQuestReplicaSnapshotStatus::ManifestInvalid);
    }
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::Validate(
    bool aCheckpoint,
    PartyQuestCheckpointKind aKind) const noexcept
{
    try
    {
        if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid())
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidIdentity);

        const std::filesystem::path manifestPath = aCheckpoint
            ? PartyQuestReplicaManifestStore::GetCheckpointManifestPath(m_paths, aKind)
            : PartyQuestReplicaManifestStore::GetImportManifestPath(m_paths);
        const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);

        PartyQuestReplicaSnapshotResult result;
        result.ManifestStatus = loaded.Status;
        if (loaded.Status == PartyQuestReplicaManifestPersistenceStatus::BackupRecoveryRequired)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestRecoveryRequired;
            return result;
        }
        if (loaded.Status != PartyQuestReplicaManifestPersistenceStatus::Success || !loaded.Manifest)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ManifestInvalid;
            return result;
        }

        const bool shapeMatches = aCheckpoint
            ? loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::Checkpoint &&
                  loaded.Manifest->CheckpointKind == aKind
            : loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::ImportedReplica;
        if (!shapeMatches)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ExistingSnapshotConflict;
            return result;
        }

        result.VerificationStatus = PartyQuestReplicaManifestStore::VerifyPublishedFiles(
            m_paths,
            m_campaignId,
            m_playerProfileId,
            *loaded.Manifest);
        if (result.VerificationStatus == PartyQuestReplicaManifestVerificationStatus::InvalidIdentity)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::ExistingSnapshotConflict;
            return result;
        }
        if (result.VerificationStatus != PartyQuestReplicaManifestVerificationStatus::Verified)
        {
            result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
            return result;
        }

        result.Status = PartyQuestReplicaSnapshotStatus::Ready;
        return result;
    }
    catch (...)
    {
        return Failure(PartyQuestReplicaSnapshotStatus::ManifestInvalid);
    }
}
