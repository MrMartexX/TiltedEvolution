#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
PartyQuestReplicaSnapshotResult Failure(
    PartyQuestReplicaSnapshotStatus aStatus) noexcept
{
    PartyQuestReplicaSnapshotResult result;
    result.Status = aStatus;
    return result;
}

bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

enum class PartialRevisionPublicationRecovery : uint8_t
{
    NotPartial,
    Recovered,
    Conflict,
    CleanupFailed
};

struct PublishedRevisionFile
{
    size_t OperationIndex{};
    std::filesystem::path ConfinedPath;
};

std::filesystem::path ExpectedRevisionDestinationRoot(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    PartyQuestReplicaFileKind aFileKind) noexcept
{
    const auto revisionRoot = PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
        acPaths,
        aKind,
        aCampaignWorldRevision);
    if (revisionRoot.empty())
        return {};

    if (aFileKind == PartyQuestReplicaFileKind::ExternalSidecar)
        return revisionRoot / "sidecars" / "external";
    return revisionRoot / "saves";
}

std::optional<std::filesystem::path> ResolveConfinedRevisionDestination(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyOperation& acOperation) noexcept
{
    try
    {
        std::error_code ec;
        const auto root = std::filesystem::absolute(acPaths.Root, ec).lexically_normal();
        if (ec || root.empty())
            return std::nullopt;

        ec.clear();
        const auto playerRoot = std::filesystem::absolute(
            acPaths.PlayerDirectory,
            ec).lexically_normal();
        if (ec || playerRoot.empty() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(root, playerRoot))
        {
            return std::nullopt;
        }

        const auto expectedRootRaw = ExpectedRevisionDestinationRoot(
            acPaths,
            aKind,
            aCampaignWorldRevision,
            acOperation.Kind);
        ec.clear();
        const auto expectedRoot = std::filesystem::absolute(
            expectedRootRaw,
            ec).lexically_normal();
        if (ec || expectedRoot.empty() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(playerRoot, expectedRoot))
        {
            return std::nullopt;
        }

        ec.clear();
        const auto destination = std::filesystem::absolute(
            acOperation.DestinationPath,
            ec).lexically_normal();
        if (ec || destination.empty() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(expectedRoot, destination))
        {
            return std::nullopt;
        }

        ec.clear();
        const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
        if (ec || canonicalRoot.empty())
            return std::nullopt;

        ec.clear();
        const auto canonicalPlayerRoot = std::filesystem::weakly_canonical(playerRoot, ec);
        const auto playerRelative = playerRoot.lexically_relative(root).lexically_normal();
        const auto expectedCanonicalPlayerRoot =
            (canonicalRoot / playerRelative).lexically_normal();
        if (ec || canonicalPlayerRoot.empty() ||
            !PartyQuestReplicaFilePlanner::IsSafeRelativePath(playerRelative) ||
            canonicalPlayerRoot != expectedCanonicalPlayerRoot)
        {
            return std::nullopt;
        }

        ec.clear();
        const auto canonicalExpectedRoot = std::filesystem::weakly_canonical(expectedRoot, ec);
        const auto expectedRelative = expectedRoot.lexically_relative(playerRoot).lexically_normal();
        const auto expectedCanonicalRoot =
            (canonicalPlayerRoot / expectedRelative).lexically_normal();
        if (ec || canonicalExpectedRoot.empty() ||
            !PartyQuestReplicaFilePlanner::IsSafeRelativePath(expectedRelative) ||
            canonicalExpectedRoot != expectedCanonicalRoot)
        {
            return std::nullopt;
        }

        const auto destinationRelative =
            destination.lexically_relative(expectedRoot).lexically_normal();
        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(destinationRelative))
            return std::nullopt;

        const auto confinedDestination =
            (canonicalExpectedRoot / destinationRelative).lexically_normal();
        ec.clear();
        const auto canonicalParent = std::filesystem::weakly_canonical(
            destination.parent_path(),
            ec);
        if (ec || canonicalParent.empty() ||
            canonicalParent != confinedDestination.parent_path())
        {
            return std::nullopt;
        }

        return confinedDestination;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool MatchesPublishedRevisionFile(
    const std::filesystem::path& acPath,
    const PartyQuestReplicaCopyOperation& acOperation) noexcept
{
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(acPath, ec);
    if (ec ||
        std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status))
    {
        return false;
    }

    const auto observed = PartyQuestReplicaFileExecutor::ObserveRegularFile(acPath);
    return observed &&
        observed->Size == acOperation.ExpectedSize &&
        observed->Digest == acOperation.ExpectedDigest;
}

/**
 * A revision checkpoint becomes authoritative only when its manifest exists.
 * If the process dies between individual final-file renames, the next retry may
 * see an exact subset of the expected final files but no manifest. Such a set
 * can only be recovered when every published file still matches the immutable
 * plan byte-for-byte and at least one expected destination is still missing.
 *
 * The cleanup does not trust a mutable ready plan or post-crash directory
 * topology. Every destination is resolved through the configured co-op root,
 * player root and exact revision/type namespace. A symlink/reparse-point change
 * below the configured root is therefore a conflict, even when the bytes at the
 * redirected destination happen to match. The entire published subset is
 * revalidated before the first remove so ambiguous evidence is never partially
 * cleaned merely because an earlier operation happened to verify first.
 */
PartialRevisionPublicationRecovery RecoverExactPartialRevisionPublication(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    try
    {
        std::vector<PublishedRevisionFile> published;
        published.reserve(acPlan.Operations.size());
        bool sawMissing{};

        for (size_t i = 0; i < acPlan.Operations.size(); ++i)
        {
            const auto& operation = acPlan.Operations[i];
            const auto confined = ResolveConfinedRevisionDestination(
                acPaths,
                aKind,
                aCampaignWorldRevision,
                operation);
            if (!confined)
                return PartialRevisionPublicationRecovery::Conflict;

            std::error_code ec;
            const auto status = std::filesystem::symlink_status(*confined, ec);
            if (status.type() == std::filesystem::file_type::not_found ||
                IsMissingError(ec))
            {
                sawMissing = true;
                continue;
            }

            if (!MatchesPublishedRevisionFile(*confined, operation))
                return PartialRevisionPublicationRecovery::Conflict;

            published.push_back({i, *confined});
        }

        // No final file exists: normal fresh execution. No expected file is
        // missing: this is a complete orphan and is handled by exact adoption.
        if (published.empty() || !sawMissing)
            return PartialRevisionPublicationRecovery::NotPartial;

        // Re-resolve and re-hash the entire subset before the first deletion.
        // This makes a post-classification topology/byte change fail closed
        // without deleting any earlier verified evidence from this invocation.
        for (const auto& file : published)
        {
            const auto& operation = acPlan.Operations[file.OperationIndex];
            const auto confined = ResolveConfinedRevisionDestination(
                acPaths,
                aKind,
                aCampaignWorldRevision,
                operation);
            if (!confined ||
                *confined != file.ConfinedPath ||
                !MatchesPublishedRevisionFile(*confined, operation))
            {
                return PartialRevisionPublicationRecovery::Conflict;
            }
        }

        for (const auto& file : published)
        {
            std::error_code ec;
            if (std::filesystem::remove(file.ConfinedPath, ec))
                continue;
            if (IsMissingError(ec))
                continue;
            if (ec)
                return PartialRevisionPublicationRecovery::CleanupFailed;

            std::error_code statusError;
            const auto status = std::filesystem::symlink_status(
                file.ConfinedPath,
                statusError);
            if (status.type() == std::filesystem::file_type::not_found ||
                IsMissingError(statusError))
            {
                continue;
            }
            return PartialRevisionPublicationRecovery::CleanupFailed;
        }

        return PartialRevisionPublicationRecovery::Recovered;
    }
    catch (...)
    {
        return PartialRevisionPublicationRecovery::Conflict;
    }
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

            if (existingFiles.IsSuccess())
            {
                result.AdoptedVerifiedFiles = true;
            }
            else if (aCheckpoint && aRevisionScoped)
            {
                const auto recovery = RecoverExactPartialRevisionPublication(
                    m_paths,
                    aKind,
                    aCampaignWorldRevision,
                    acPlan);
                if (recovery == PartialRevisionPublicationRecovery::CleanupFailed)
                {
                    result.Status = PartyQuestReplicaSnapshotStatus::CopyFailed;
                    result.CopyStatus = PartyQuestReplicaExecutionStatus::RollbackFailed;
                    return result;
                }
                if (recovery != PartialRevisionPublicationRecovery::Recovered)
                {
                    result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
                    result.CopyStatus = existingFiles.Status;
                    return result;
                }

                // Retry only after an exact partial publication was removed.
                // A second crash at any point repeats the same classification on
                // the next start; no mismatching evidence is ever auto-deleted.
                copy = PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint(
                    m_paths,
                    aKind,
                    aCampaignWorldRevision,
                    acPlan);
                result.CopyStatus = copy.Status;
                if (!copy.IsSuccess())
                {
                    result.Status = PartyQuestReplicaSnapshotStatus::CopyFailed;
                    return result;
                }
            }
            else
            {
                result.Status = PartyQuestReplicaSnapshotStatus::FileVerificationFailed;
                result.CopyStatus = existingFiles.Status;
                return result;
            }
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
