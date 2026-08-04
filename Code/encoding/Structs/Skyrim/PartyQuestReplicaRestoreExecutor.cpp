#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
enum class NodeState : uint8_t
{
    Missing,
    Regular,
    Unsafe,
    Error
};

struct RestoreFailure
{
    PartyQuestReplicaRestoreExecutionStatus Status{
        PartyQuestReplicaRestoreExecutionStatus::Success};
    size_t Index{};
    std::filesystem::path Path;

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Status == PartyQuestReplicaRestoreExecutionStatus::Success;
    }
};

std::string FormatRestoreId(uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return stream.str();
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
    try
    {
        return PartyQuestReplicaFilePlanner::IsContainedBy(
            acRoot.lexically_normal(), acCandidate.lexically_normal());
    }
    catch (...)
    {
        return false;
    }
}

NodeState InspectNode(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (ec)
            return NodeState::Error;
        if (status.type() == std::filesystem::file_type::not_found)
            return NodeState::Missing;
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
            return NodeState::Unsafe;
        return NodeState::Regular;
    }
    catch (...)
    {
        return NodeState::Error;
    }
}

bool Matches(
    const std::filesystem::path& acPath,
    uint64_t aExpectedSize,
    uint64_t aExpectedDigest) noexcept
{
    const auto observation = PartyQuestReplicaFileExecutor::ObserveRegularFile(acPath);
    return observation &&
        observation->Size == aExpectedSize &&
        observation->Digest == aExpectedDigest;
}

bool IsParentResolvedInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        const auto root = AbsoluteNormalized(acRoot);
        const auto parent = AbsoluteNormalized(acPath.parent_path());
        if (!root || !parent || !IsInside(*root, *parent))
            return false;

        std::error_code ec;
        const auto resolvedRoot = std::filesystem::weakly_canonical(*root, ec);
        if (ec || resolvedRoot.empty())
            return false;
        ec.clear();
        const auto resolvedParent = std::filesystem::weakly_canonical(*parent, ec);
        if (ec || resolvedParent.empty())
            return false;
        return IsInside(resolvedRoot, resolvedParent);
    }
    catch (...)
    {
        return false;
    }
}

bool EnsureParentResolvedInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acPath) noexcept
{
    if (!IsParentResolvedInside(acRoot, acPath))
        return false;

    try
    {
        std::error_code ec;
        std::filesystem::create_directories(acPath.parent_path(), ec);
        return !ec && IsParentResolvedInside(acRoot, acPath);
    }
    catch (...)
    {
        return false;
    }
}

bool RemoveRegularIfPresent(const std::filesystem::path& acPath) noexcept
{
    const NodeState state = InspectNode(acPath);
    if (state == NodeState::Missing)
        return true;
    if (state != NodeState::Regular)
        return false;

    try
    {
        std::error_code ec;
        const bool removed = std::filesystem::remove(acPath, ec);
        return !ec && removed;
    }
    catch (...)
    {
        return false;
    }
}

bool RenameFile(
    const std::filesystem::path& acFrom,
    const std::filesystem::path& acTo) noexcept
{
    try
    {
        std::error_code ec;
        std::filesystem::rename(acFrom, acTo, ec);
        return !ec;
    }
    catch (...)
    {
        return false;
    }
}

std::filesystem::path BuildSiblingTemporary(
    const std::filesystem::path& acDestination,
    const char* apPurpose,
    uint64_t aRestoreId,
    size_t aIndex)
{
    std::filesystem::path result = acDestination;
    result += ".tpqrestore-" + std::string(apPurpose) + "-" +
        std::to_string(aRestoreId) + "-" + std::to_string(aIndex);
    return result;
}

bool LayoutMatchesState(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    const auto expected = PartyQuestCoopSaveLayout::Build(
        acPaths.Root, acState.CampaignId, acState.PlayerProfileId);
    if (!expected)
        return false;

    return expected->CampaignDirectory.lexically_normal() == acPaths.CampaignDirectory.lexically_normal() &&
        expected->PlayerDirectory.lexically_normal() == acPaths.PlayerDirectory.lexically_normal() &&
        expected->CheckpointsDirectory.lexically_normal() == acPaths.CheckpointsDirectory.lexically_normal() &&
        expected->SavesDirectory.lexically_normal() == acPaths.SavesDirectory.lexically_normal() &&
        expected->SidecarsDirectory.lexically_normal() == acPaths.SidecarsDirectory.lexically_normal() &&
        expected->MetadataDirectory.lexically_normal() == acPaths.MetadataDirectory.lexically_normal() &&
        expected->RuntimeApplySidecar.lexically_normal() == acPaths.RuntimeApplySidecar.lexically_normal();
}

bool ValidateStatePaths(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    const std::filesystem::path& acJournalPath) noexcept
{
    try
    {
        if (!acState.CampaignId.IsValid() ||
            !acState.PlayerProfileId.IsValid() ||
            acState.RestoreId == 0 ||
            acState.CampaignWorldRevision == 0 ||
            acState.Operations.empty() ||
            !LayoutMatchesState(acPaths, acState))
        {
            return false;
        }

        const auto playerRoot = AbsoluteNormalized(acPaths.PlayerDirectory);
        const auto checkpointRoot = AbsoluteNormalized(
            PartyQuestCoopSaveLayout::GetCheckpointDirectory(acPaths, acState.CheckpointKind));
        const auto metadataRoot = AbsoluteNormalized(acPaths.MetadataDirectory);
        const auto transactionDirectory = AbsoluteNormalized(acState.TransactionDirectory);
        const auto journalPath = AbsoluteNormalized(acJournalPath);
        const auto expectedJournalPath = AbsoluteNormalized(
            PartyQuestReplicaRestoreJournal::GetJournalPath(acState));
        if (!playerRoot || !checkpointRoot || !metadataRoot || !transactionDirectory ||
            !journalPath || !expectedJournalPath || *journalPath != *expectedJournalPath)
        {
            return false;
        }

        const auto expectedTransactionDirectory = AbsoluteNormalized(
            acPaths.MetadataDirectory / "restore" / FormatRestoreId(acState.RestoreId));
        if (!expectedTransactionDirectory || *transactionDirectory != *expectedTransactionDirectory ||
            !IsInside(*playerRoot, *transactionDirectory) ||
            !IsInside(*metadataRoot, *transactionDirectory))
        {
            return false;
        }

        const auto savesRoot = AbsoluteNormalized(acPaths.SavesDirectory);
        const auto externalSidecarsRoot = AbsoluteNormalized(acPaths.SidecarsDirectory / "external");
        const auto runtimeApplySidecar = AbsoluteNormalized(acPaths.RuntimeApplySidecar);
        if (!savesRoot || !externalSidecarsRoot || !runtimeApplySidecar)
            return false;

        for (const auto& operation : acState.Operations)
        {
            const auto source = AbsoluteNormalized(operation.CheckpointSourcePath);
            const auto destination = AbsoluteNormalized(operation.ReplicaDestinationPath);
            const auto rollback = AbsoluteNormalized(operation.RollbackPath);
            if (!source || !destination || !rollback ||
                !IsInside(*checkpointRoot, *source) ||
                !IsInside(*playerRoot, *destination) ||
                *destination == *runtimeApplySidecar ||
                operation.ExpectedRestoredDigest == 0 ||
                (operation.DestinationExisted && operation.OriginalDigest == 0))
            {
                return false;
            }

            if (operation.Kind == PartyQuestReplicaFileKind::ExternalSidecar)
            {
                if (!IsInside(*externalSidecarsRoot, *destination))
                    return false;
            }
            else
            {
                if (destination->parent_path() != *savesRoot ||
                    !HasExpectedExtension(operation.Kind, *destination))
                {
                    return false;
                }
            }

            const auto relative = destination->lexically_relative(*playerRoot).lexically_normal();
            if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
                return false;
            const auto expectedRollback = AbsoluteNormalized(
                *transactionDirectory / "rollback" / relative);
            if (!expectedRollback || *rollback != *expectedRollback ||
                !IsInside(*transactionDirectory / "rollback", *rollback))
            {
                return false;
            }

            if (!IsParentResolvedInside(*checkpointRoot, *source) ||
                !IsParentResolvedInside(*playerRoot, *destination))
            {
                return false;
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

PartyQuestReplicaRestoreExecutionReport MakeReport(
    const PartyQuestReplicaRestoreJournalState& acState,
    PartyQuestReplicaRestoreExecutionStatus aStatus,
    size_t aFailedOperation = 0,
    const std::filesystem::path& acFailedPath = {})
{
    PartyQuestReplicaRestoreExecutionReport report;
    report.Status = aStatus;
    report.Phase = acState.Phase;
    report.FailedOperation = aFailedOperation;
    report.FailedPath = acFailedPath;
    report.JournalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(acState);
    return report;
}

RestoreFailure CopyVerified(
    const std::filesystem::path& acConfinementRoot,
    const std::filesystem::path& acSource,
    const std::filesystem::path& acDestination,
    uint64_t aExpectedSize,
    uint64_t aExpectedDigest,
    PartyQuestReplicaRestoreExecutionStatus aSourceFailure,
    PartyQuestReplicaRestoreExecutionStatus aCopyFailure,
    size_t aIndex) noexcept
{
    if (!Matches(acSource, aExpectedSize, aExpectedDigest))
        return {aSourceFailure, aIndex, acSource};
    if (!EnsureParentResolvedInside(acConfinementRoot, acDestination))
    {
        return {PartyQuestReplicaRestoreExecutionStatus::UnsafePath, aIndex, acDestination};
    }
    if (!RemoveRegularIfPresent(acDestination))
        return {PartyQuestReplicaRestoreExecutionStatus::UnsafePath, aIndex, acDestination};

    try
    {
        std::error_code ec;
        const bool copied = std::filesystem::copy_file(
            acSource,
            acDestination,
            std::filesystem::copy_options::none,
            ec);
        if (!copied || ec)
            return {aCopyFailure, aIndex, acDestination};
    }
    catch (...)
    {
        return {aCopyFailure, aIndex, acDestination};
    }

    if (!Matches(acDestination, aExpectedSize, aExpectedDigest) ||
        !Matches(acSource, aExpectedSize, aExpectedDigest))
    {
        RemoveRegularIfPresent(acDestination);
        return {aCopyFailure, aIndex, acDestination};
    }
    return {};
}

RestoreFailure VerifyPreparedDestinations(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        const NodeState node = InspectNode(operation.ReplicaDestinationPath);
        if (operation.DestinationExisted)
        {
            if (node != NodeState::Regular ||
                !Matches(operation.ReplicaDestinationPath, operation.OriginalSize, operation.OriginalDigest))
            {
                return {
                    node == NodeState::Unsafe || node == NodeState::Error
                        ? PartyQuestReplicaRestoreExecutionStatus::UnsafePath
                        : PartyQuestReplicaRestoreExecutionStatus::DestinationChanged,
                    i,
                    operation.ReplicaDestinationPath};
            }
        }
        else if (node != NodeState::Missing)
        {
            return {
                node == NodeState::Unsafe || node == NodeState::Error
                    ? PartyQuestReplicaRestoreExecutionStatus::UnsafePath
                    : PartyQuestReplicaRestoreExecutionStatus::DestinationChanged,
                i,
                operation.ReplicaDestinationPath};
        }
    }
    return {};
}

RestoreFailure CreateRollbackBackups(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    const auto destinationCheck = VerifyPreparedDestinations(aState);
    if (!destinationCheck.IsSuccess())
        return destinationCheck;

    for (size_t i = 0; i < aState.Operations.size(); ++i)
    {
        const auto& operation = aState.Operations[i];
        const NodeState rollbackState = InspectNode(operation.RollbackPath);
        if (!operation.DestinationExisted)
        {
            if (rollbackState != NodeState::Missing)
            {
                return {
                    rollbackState == NodeState::Unsafe || rollbackState == NodeState::Error
                        ? PartyQuestReplicaRestoreExecutionStatus::UnsafePath
                        : PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed,
                    i,
                    operation.RollbackPath};
            }
            continue;
        }

        if (rollbackState == NodeState::Regular)
        {
            if (!Matches(operation.RollbackPath, operation.OriginalSize, operation.OriginalDigest))
            {
                return {
                    PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed,
                    i,
                    operation.RollbackPath};
            }
            continue;
        }
        if (rollbackState != NodeState::Missing)
        {
            return {PartyQuestReplicaRestoreExecutionStatus::UnsafePath, i, operation.RollbackPath};
        }

        std::filesystem::path temporary = operation.RollbackPath;
        temporary += ".tmp";
        const RestoreFailure copied = CopyVerified(
            aState.TransactionDirectory,
            operation.ReplicaDestinationPath,
            temporary,
            operation.OriginalSize,
            operation.OriginalDigest,
            PartyQuestReplicaRestoreExecutionStatus::DestinationChanged,
            PartyQuestReplicaRestoreExecutionStatus::BackupCreationFailed,
            i);
        if (!copied.IsSuccess())
            return copied;

        if (InspectNode(operation.RollbackPath) != NodeState::Missing ||
            !RenameFile(temporary, operation.RollbackPath))
        {
            RemoveRegularIfPresent(temporary);
            return {
                PartyQuestReplicaRestoreExecutionStatus::BackupCreationFailed,
                i,
                operation.RollbackPath};
        }
    }

    if (!PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(aState))
        return {PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed, 0, {}};
    return {};
}

RestoreFailure StageCheckpointBytes(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    std::vector<std::filesystem::path>& aStaged) noexcept
{
    aStaged.clear();
    aStaged.reserve(acState.Operations.size());
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        const auto staged = BuildSiblingTemporary(
            operation.ReplicaDestinationPath, "stage", acState.RestoreId, i);
        const RestoreFailure copied = CopyVerified(
            acPaths.PlayerDirectory,
            operation.CheckpointSourcePath,
            staged,
            operation.ExpectedRestoredSize,
            operation.ExpectedRestoredDigest,
            PartyQuestReplicaRestoreExecutionStatus::CheckpointSourceChanged,
            PartyQuestReplicaRestoreExecutionStatus::StagingFailed,
            i);
        if (!copied.IsSuccess())
        {
            for (const auto& path : aStaged)
                RemoveRegularIfPresent(path);
            return copied;
        }
        aStaged.push_back(staged);
    }
    return {};
}

bool CleanupSiblingTemporaries(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    bool success = true;
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        for (const char* purpose : {"stage", "old", "rollback", "failed"})
        {
            const auto path = BuildSiblingTemporary(
                operation.ReplicaDestinationPath, purpose, acState.RestoreId, i);
            if (!RemoveRegularIfPresent(path))
                success = false;
        }
    }
    return success;
}

bool VerifyOriginalDestinations(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (const auto& operation : acState.Operations)
    {
        const NodeState state = InspectNode(operation.ReplicaDestinationPath);
        if (operation.DestinationExisted)
        {
            if (state != NodeState::Regular ||
                !Matches(operation.ReplicaDestinationPath, operation.OriginalSize, operation.OriginalDigest))
            {
                return false;
            }
        }
        else if (state != NodeState::Missing)
        {
            return false;
        }
    }
    return true;
}

bool RestoreOriginalDestinations(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    bool& aCleanupPending) noexcept
{
    for (size_t reverse = acState.Operations.size(); reverse > 0; --reverse)
    {
        const size_t i = reverse - 1;
        const auto& operation = acState.Operations[i];
        const NodeState destinationState = InspectNode(operation.ReplicaDestinationPath);

        if (!operation.DestinationExisted)
        {
            if (destinationState == NodeState::Missing)
                continue;
            if (destinationState != NodeState::Regular ||
                !RemoveRegularIfPresent(operation.ReplicaDestinationPath))
            {
                return false;
            }
            continue;
        }

        if (!Matches(operation.RollbackPath, operation.OriginalSize, operation.OriginalDigest))
            return false;
        if (destinationState == NodeState::Regular &&
            Matches(operation.ReplicaDestinationPath, operation.OriginalSize, operation.OriginalDigest))
        {
            continue;
        }
        if (destinationState == NodeState::Unsafe || destinationState == NodeState::Error)
            return false;

        const auto rollbackStage = BuildSiblingTemporary(
            operation.ReplicaDestinationPath, "rollback", acState.RestoreId, i);
        const RestoreFailure staged = CopyVerified(
            acPaths.PlayerDirectory,
            operation.RollbackPath,
            rollbackStage,
            operation.OriginalSize,
            operation.OriginalDigest,
            PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed,
            PartyQuestReplicaRestoreExecutionStatus::RollbackFailed,
            i);
        if (!staged.IsSuccess())
            return false;

        const auto failed = BuildSiblingTemporary(
            operation.ReplicaDestinationPath, "failed", acState.RestoreId, i);
        if (!RemoveRegularIfPresent(failed))
            return false;

        bool movedCurrent{};
        if (destinationState == NodeState::Regular)
        {
            if (!RenameFile(operation.ReplicaDestinationPath, failed))
                return false;
            movedCurrent = true;
        }

        if (!RenameFile(rollbackStage, operation.ReplicaDestinationPath))
        {
            if (movedCurrent)
                RenameFile(failed, operation.ReplicaDestinationPath);
            RemoveRegularIfPresent(rollbackStage);
            return false;
        }

        if (!Matches(operation.ReplicaDestinationPath, operation.OriginalSize, operation.OriginalDigest))
            return false;
        if (movedCurrent && !RemoveRegularIfPresent(failed))
            aCleanupPending = true;
    }

    if (!VerifyOriginalDestinations(acState))
        return false;

    if (!CleanupSiblingTemporaries(acState))
        aCleanupPending = true;
    return true;
}

bool RemoveTransactionDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!IsInside(acPaths.MetadataDirectory.lexically_normal(), acState.TransactionDirectory.lexically_normal()))
        return false;

    try
    {
        std::error_code ec;
        std::filesystem::remove_all(acState.TransactionDirectory, ec);
        return !ec;
    }
    catch (...)
    {
        return false;
    }
}

PartyQuestReplicaRestoreExecutionReport RollbackAndReport(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    PartyQuestReplicaRestoreExecutionStatus aFailureStatus,
    size_t aFailedOperation,
    const std::filesystem::path& acFailedPath,
    bool aRecoveredRollback) noexcept
{
    PartyQuestReplicaRestoreExecutionReport report = MakeReport(
        acState,
        aRecoveredRollback
            ? PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback
            : aFailureStatus,
        aFailedOperation,
        acFailedPath);

    bool cleanupPending{};
    if (!RestoreOriginalDestinations(acPaths, acState, cleanupPending))
    {
        report.Status = PartyQuestReplicaRestoreExecutionStatus::RollbackFailed;
        report.CleanupPending = cleanupPending;
        return report;
    }

    report.RollbackPerformed = true;
    if (!RemoveTransactionDirectory(acPaths, acState))
        cleanupPending = true;
    report.CleanupPending = cleanupPending;
    return report;
}

RestoreFailure ReplaceStagedFiles(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    const std::vector<std::filesystem::path>& acStaged,
    size_t& aCompleted) noexcept
{
    aCompleted = 0;
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        if (i >= acStaged.size() ||
            !Matches(acStaged[i], operation.ExpectedRestoredSize, operation.ExpectedRestoredDigest) ||
            !IsParentResolvedInside(acPaths.PlayerDirectory, operation.ReplicaDestinationPath))
        {
            return {PartyQuestReplicaRestoreExecutionStatus::StagingFailed, i, operation.ReplicaDestinationPath};
        }

        const NodeState destinationState = InspectNode(operation.ReplicaDestinationPath);
        if (operation.DestinationExisted)
        {
            if (destinationState != NodeState::Regular ||
                !Matches(operation.ReplicaDestinationPath, operation.OriginalSize, operation.OriginalDigest))
            {
                return {
                    destinationState == NodeState::Unsafe || destinationState == NodeState::Error
                        ? PartyQuestReplicaRestoreExecutionStatus::UnsafePath
                        : PartyQuestReplicaRestoreExecutionStatus::DestinationChanged,
                    i,
                    operation.ReplicaDestinationPath};
            }
        }
        else if (destinationState != NodeState::Missing)
        {
            return {
                destinationState == NodeState::Unsafe || destinationState == NodeState::Error
                    ? PartyQuestReplicaRestoreExecutionStatus::UnsafePath
                    : PartyQuestReplicaRestoreExecutionStatus::DestinationChanged,
                i,
                operation.ReplicaDestinationPath};
        }

        const auto oldPath = BuildSiblingTemporary(
            operation.ReplicaDestinationPath, "old", acState.RestoreId, i);
        if (InspectNode(oldPath) != NodeState::Missing)
            return {PartyQuestReplicaRestoreExecutionStatus::ReplacementFailed, i, oldPath};

        bool movedOriginal{};
        if (operation.DestinationExisted)
        {
            if (!RenameFile(operation.ReplicaDestinationPath, oldPath))
                return {PartyQuestReplicaRestoreExecutionStatus::ReplacementFailed, i, operation.ReplicaDestinationPath};
            movedOriginal = true;
        }

        if (!RenameFile(acStaged[i], operation.ReplicaDestinationPath))
        {
            if (movedOriginal)
                RenameFile(oldPath, operation.ReplicaDestinationPath);
            return {PartyQuestReplicaRestoreExecutionStatus::ReplacementFailed, i, operation.ReplicaDestinationPath};
        }

        if (!Matches(
                operation.ReplicaDestinationPath,
                operation.ExpectedRestoredSize,
                operation.ExpectedRestoredDigest))
        {
            return {PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed, i, operation.ReplicaDestinationPath};
        }
        ++aCompleted;
    }
    return {};
}

PartyQuestReplicaRestoreExecutionReport ContinueBeforeMutation(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(aState);

    if (aState.Phase == PartyQuestReplicaRestoreJournalPhase::Prepared)
    {
        const RestoreFailure backups = CreateRollbackBackups(acPaths, aState);
        if (!backups.IsSuccess())
            return MakeReport(aState, backups.Status, backups.Index, backups.Path);

        if (PartyQuestReplicaRestoreJournal::MarkBackupsReady(aState) !=
            PartyQuestReplicaRestoreJournalStatus::Ready)
        {
            return MakeReport(
                aState,
                PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed);
        }
        if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, aState) !=
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            aState.Phase = PartyQuestReplicaRestoreJournalPhase::Prepared;
            return MakeReport(
                aState,
                PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed);
        }
    }
    else if (aState.Phase == PartyQuestReplicaRestoreJournalPhase::BackupsReady)
    {
        if (!PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(aState))
        {
            return MakeReport(
                aState,
                PartyQuestReplicaRestoreExecutionStatus::BackupVerificationFailed);
        }
        const RestoreFailure destinations = VerifyPreparedDestinations(aState);
        if (!destinations.IsSuccess())
            return MakeReport(aState, destinations.Status, destinations.Index, destinations.Path);
    }
    else
    {
        return MakeReport(aState, PartyQuestReplicaRestoreExecutionStatus::InvalidPlan);
    }

    std::vector<std::filesystem::path> staged;
    const RestoreFailure staging = StageCheckpointBytes(acPaths, aState, staged);
    if (!staging.IsSuccess())
        return MakeReport(aState, staging.Status, staging.Index, staging.Path);

    const RestoreFailure destinations = VerifyPreparedDestinations(aState);
    if (!destinations.IsSuccess())
    {
        for (const auto& path : staged)
            RemoveRegularIfPresent(path);
        return MakeReport(aState, destinations.Status, destinations.Index, destinations.Path);
    }

    if (PartyQuestReplicaRestoreJournal::MarkMutationStarted(aState) !=
        PartyQuestReplicaRestoreJournalStatus::Ready)
    {
        for (const auto& path : staged)
            RemoveRegularIfPresent(path);
        return MakeReport(aState, PartyQuestReplicaRestoreExecutionStatus::InvalidPlan);
    }
    if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, aState) !=
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        aState.Phase = PartyQuestReplicaRestoreJournalPhase::BackupsReady;
        for (const auto& path : staged)
            RemoveRegularIfPresent(path);
        return MakeReport(aState, PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed);
    }

    size_t completed{};
    const RestoreFailure replacement = ReplaceStagedFiles(acPaths, aState, staged, completed);
    if (!replacement.IsSuccess())
    {
        auto report = RollbackAndReport(
            acPaths,
            aState,
            replacement.Status,
            replacement.Index,
            replacement.Path,
            false);
        report.CompletedOperations = completed;
        return report;
    }

    if (PartyQuestReplicaRestoreJournal::MarkRestored(aState) !=
        PartyQuestReplicaRestoreJournalStatus::Ready)
    {
        auto report = RollbackAndReport(
            acPaths,
            aState,
            PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed,
            0,
            {},
            false);
        report.CompletedOperations = completed;
        return report;
    }
    if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, aState) !=
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        auto report = RollbackAndReport(
            acPaths,
            aState,
            PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed,
            0,
            journalPath,
            false);
        report.CompletedOperations = completed;
        return report;
    }

    if (PartyQuestReplicaRestoreJournal::MarkCommitted(aState) !=
        PartyQuestReplicaRestoreJournalStatus::Ready)
    {
        return MakeReport(
            aState,
            PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed);
    }
    if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, aState) !=
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        aState.Phase = PartyQuestReplicaRestoreJournalPhase::Restored;
        auto report = MakeReport(
            aState,
            PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed,
            0,
            journalPath);
        report.CompletedOperations = completed;
        return report;
    }

    PartyQuestReplicaRestoreExecutionReport report = MakeReport(
        aState,
        PartyQuestReplicaRestoreExecutionStatus::Success);
    report.CompletedOperations = completed;
    report.CleanupPending = !CleanupSiblingTemporaries(aState);
    return report;
}
} // namespace

PartyQuestReplicaRestoreExecutionReport PartyQuestReplicaRestoreExecutor::Execute(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId) noexcept
{
    try
    {
        const auto prepared = PartyQuestReplicaRestoreJournal::Prepare(acPaths, acPlan, aRestoreId);
        if (!prepared.IsReady())
        {
            PartyQuestReplicaRestoreJournalState emptyState;
            auto report = MakeReport(
                emptyState,
                prepared.Status == PartyQuestReplicaRestoreJournalStatus::InvalidIdentity
                    ? PartyQuestReplicaRestoreExecutionStatus::InvalidIdentity
                    : PartyQuestReplicaRestoreExecutionStatus::InvalidPlan);
            report.JournalPath.clear();
            return report;
        }

        auto state = *prepared.State;
        const auto journalPath = PartyQuestReplicaRestoreJournal::GetJournalPath(state);
        if (!ValidateStatePaths(acPaths, state, journalPath))
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::UnsafePath);

        std::error_code ec;
        const auto transactionStatus = std::filesystem::symlink_status(state.TransactionDirectory, ec);
        if (ec)
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::UnsafePath);
        if (transactionStatus.type() != std::filesystem::file_type::not_found)
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::RestoreIdConflict);

        if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(journalPath, state) !=
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            RemoveTransactionDirectory(acPaths, state);
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed);
        }

        return ContinueBeforeMutation(acPaths, state);
    }
    catch (...)
    {
        PartyQuestReplicaRestoreJournalState emptyState;
        auto report = MakeReport(
            emptyState,
            PartyQuestReplicaRestoreExecutionStatus::InvalidPlan);
        report.JournalPath.clear();
        return report;
    }
}

PartyQuestReplicaRestoreExecutionReport PartyQuestReplicaRestoreExecutor::Recover(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const std::filesystem::path& acJournalPath) noexcept
{
    try
    {
        const auto loaded = PartyQuestReplicaRestoreJournalPersistence::Load(acJournalPath);
        if (loaded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired)
        {
            PartyQuestReplicaRestoreJournalState state;
            if (loaded.State)
                state = *loaded.State;
            auto report = MakeReport(
                state,
                PartyQuestReplicaRestoreExecutionStatus::BackupRecoveryRequired);
            report.JournalPath = acJournalPath;
            return report;
        }
        if (loaded.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success || !loaded.State)
        {
            PartyQuestReplicaRestoreJournalState emptyState;
            auto report = MakeReport(
                emptyState,
                PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
            report.JournalPath = acJournalPath;
            return report;
        }

        auto state = *loaded.State;
        if (state.CampaignId != acExpectedCampaignId ||
            state.PlayerProfileId != acExpectedPlayerProfileId)
        {
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::InvalidIdentity);
        }
        if (!ValidateStatePaths(acPaths, state, acJournalPath))
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::UnsafePath);

        switch (PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(state))
        {
        case PartyQuestReplicaRestoreRecoveryDisposition::ResumeBeforeMutation:
            return ContinueBeforeMutation(acPaths, state);

        case PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired:
            return RollbackAndReport(
                acPaths,
                state,
                PartyQuestReplicaRestoreExecutionStatus::RecoveredRollback,
                0,
                {},
                true);

        case PartyQuestReplicaRestoreRecoveryDisposition::VerifyThenCommit:
        {
            if (!PartyQuestReplicaRestoreJournal::VerifyRestoredTargets(state))
            {
                return RollbackAndReport(
                    acPaths,
                    state,
                    PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed,
                    0,
                    {},
                    false);
            }
            if (PartyQuestReplicaRestoreJournal::MarkCommitted(state) !=
                PartyQuestReplicaRestoreJournalStatus::Ready)
            {
                return MakeReport(
                    state,
                    PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed);
            }
            if (PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(acJournalPath, state) !=
                PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            {
                state.Phase = PartyQuestReplicaRestoreJournalPhase::Restored;
                return MakeReport(
                    state,
                    PartyQuestReplicaRestoreExecutionStatus::JournalPersistenceFailed,
                    0,
                    acJournalPath);
            }
            auto report = MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::Success);
            report.CompletedOperations = state.Operations.size();
            report.CleanupPending = !CleanupSiblingTemporaries(state);
            return report;
        }

        case PartyQuestReplicaRestoreRecoveryDisposition::Clean:
        {
            auto report = MakeReport(
                state,
                PartyQuestReplicaRestoreExecutionStatus::AlreadyCommitted);
            report.CompletedOperations = state.Operations.size();
            report.CleanupPending = !CleanupSiblingTemporaries(state);
            return report;
        }

        case PartyQuestReplicaRestoreRecoveryDisposition::InvalidState:
            return MakeReport(state, PartyQuestReplicaRestoreExecutionStatus::InvalidPlan);
        }
    }
    catch (...)
    {
    }

    PartyQuestReplicaRestoreJournalState emptyState;
    auto report = MakeReport(
        emptyState,
        PartyQuestReplicaRestoreExecutionStatus::JournalLoadFailed);
    report.JournalPath = acJournalPath;
    return report;
}
