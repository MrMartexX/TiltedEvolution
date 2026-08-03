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
    return Ensure(false, false, PartyQuestCheckpointKind::PreJoin, aCampaignWorldRevision, acPlan);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::EnsureCheckpoint(
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    return Ensure(true, false, aKind, aCampaignWorldRevision, acPlan);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::EnsureRevisionCheckpoint(
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    return Ensure(true, true, aKind, aCampaignWorldRevision, acPlan);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::ValidateImportedReplica() const noexcept
{
    return Validate(false, false, PartyQuestCheckpointKind::PreJoin, 0);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::ValidateCheckpoint(
    PartyQuestCheckpointKind aKind) const noexcept
{
    return Validate(true, false, aKind, 0);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::ValidateRevisionCheckpoint(
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision) const noexcept
{
    return Validate(true, true, aKind, aCampaignWorldRevision);
}

PartyQuestReplicaSnapshotResult PartyQuestReplicaSnapshotManager::Ensure(
    bool aCheckpoint,
    bool aRevisionScoped,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) const noexcept
{
    try
    {
        if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid())
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidIdentity);
        if (!acPlan.IsReady() || acPlan.Operations.empty() ||
            (aRevisionScoped && aCampaignWorldRevision == 0))
        {
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidPlan);
        }

        std::optional<PartyQuestReplicaManifest> expectedManifest;
        if (!aCheckpoint)
        {
            expectedManifest = PartyQuestReplicaManifestStore::BuildImportManifest(
                m_paths,
                m_campaignId,
                m_playerProfileId,
                aCampaignWorldRevision,
                acPlan);
        }
        else if (aRevisionScoped)
        {
            expectedManifest = PartyQuestReplicaManifestStore::BuildRevisionCheckpointManifest(
                m_paths,
                m_campaignId,
                m_playerProfileId,
                aKind,
                aCampaignWorldRevision,
                acPlan);
        }
        else
        {
            expectedManifest = PartyQuestReplicaManifestStore::BuildCheckpointManifest(
                m_paths,
                m_campaignId,
                m_playerProfileId,
                aKind,
                aCampaignWorldRevision,
                acPlan);
        }
        if (!expectedManifest)
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidPlan);

        const std::filesystem::path manifestPath = !aCheckpoint
            ? PartyQuestReplicaManifestStore::GetImportManifestPath(m_paths)
            : (aRevisionScoped
                  ? PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                        m_paths,
                        aKind,
                        aCampaignWorldRevision)
                  : PartyQuestReplicaManifestStore::GetCheckpointManifestPath(m_paths, aKind));

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
        PartyQuestReplicaExecutionReport copy;
        if (!aCheckpoint)
            copy = PartyQuestReplicaFileExecutor::ExecuteImport(m_paths, acPlan);
        else if (aRevisionScoped)
        {
            copy = PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint(
                m_paths,
                aKind,
                aCampaignWorldRevision,
                acPlan);
        }
        else
            copy = PartyQuestReplicaFileExecutor::ExecuteCheckpoint(m_paths, aKind, acPlan);
        result.CopyStatus = copy.Status;

        if (!copy.IsSuccess())
        {
            if (copy.Status != PartyQuestReplicaExecutionStatus::DestinationExists)
            {
                result.Status = PartyQuestReplicaSnapshotStatus::CopyFailed;
                return result;
            }

            PartyQuestReplicaExecutionReport existingFiles;
            if (!aCheckpoint)
                existingFiles = PartyQuestReplicaFileExecutor::VerifyImport(m_paths, acPlan);
            else if (aRevisionScoped)
            {
                existingFiles = PartyQuestReplicaFileExecutor::VerifyRevisionCheckpoint(
                    m_paths,
                    aKind,
                    aCampaignWorldRevision,
                    acPlan);
            }
            else
                existingFiles = PartyQuestReplicaFileExecutor::VerifyCheckpoint(m_paths, aKind, acPlan);

            if (!existingFiles.IsSuccess())
            {
                result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
                result.CopyStatus = existingFiles.Status;
                return result;
            }
            result.AdoptedVerifiedFiles = true;
        }

        PartyQuestReplicaExecutionReport finalVerification;
        if (!aCheckpoint)
            finalVerification = PartyQuestReplicaFileExecutor::VerifyImport(m_paths, acPlan);
        else if (aRevisionScoped)
        {
            finalVerification = PartyQuestReplicaFileExecutor::VerifyRevisionCheckpoint(
                m_paths,
                aKind,
                aCampaignWorldRevision,
                acPlan);
        }
        else
            finalVerification = PartyQuestReplicaFileExecutor::VerifyCheckpoint(m_paths, aKind, acPlan);

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
    bool aRevisionScoped,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision) const noexcept
{
    try
    {
        if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid())
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidIdentity);
        if (aRevisionScoped && aCampaignWorldRevision == 0)
            return Failure(PartyQuestReplicaSnapshotStatus::InvalidPlan);

        const std::filesystem::path manifestPath = !aCheckpoint
            ? PartyQuestReplicaManifestStore::GetImportManifestPath(m_paths)
            : (aRevisionScoped
                  ? PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
                        m_paths,
                        aKind,
                        aCampaignWorldRevision)
                  : PartyQuestReplicaManifestStore::GetCheckpointManifestPath(m_paths, aKind));
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

        bool shapeMatches{};
        if (!aCheckpoint)
            shapeMatches = loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::ImportedReplica;
        else if (aRevisionScoped)
        {
            shapeMatches = loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::RevisionCheckpoint &&
                loaded.Manifest->CheckpointKind == aKind &&
                loaded.Manifest->CampaignWorldRevision == aCampaignWorldRevision;
        }
        else
        {
            shapeMatches = loaded.Manifest->SnapshotType == PartyQuestReplicaSnapshotType::Checkpoint &&
                loaded.Manifest->CheckpointKind == aKind;
        }

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
