#include <Structs/Skyrim/PartyQuestReplicaDurableRestoreExecutor.h>

#include <Structs/Skyrim/PartyQuestReplicaDurableSnapshot.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
enum class NodeState : uint8_t
{
    Missing,
    RegularFile,
    Directory,
    Unsafe,
    Error
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

    try
    {
        std::error_code ec;
        const auto path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty())
            return std::nullopt;
        return path;
    }
    catch (...)
    {
        return std::nullopt;
    }
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
        if (status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory)
        {
            return NodeState::Missing;
        }
        if (ec)
            return NodeState::Error;
        if (std::filesystem::is_symlink(status))
            return NodeState::Unsafe;
        if (std::filesystem::is_regular_file(status))
            return NodeState::RegularFile;
        if (std::filesystem::is_directory(status))
            return NodeState::Directory;
        return NodeState::Unsafe;
    }
    catch (...)
    {
        return NodeState::Error;
    }
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

bool LayoutMatchesState(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    const auto expected = PartyQuestCoopSaveLayout::Build(
        acPaths.Root, acState.CampaignId, acState.PlayerProfileId);
    if (!expected)
        return false;

    return expected->CampaignDirectory.lexically_normal() ==
               acPaths.CampaignDirectory.lexically_normal() &&
        expected->PlayerDirectory.lexically_normal() ==
               acPaths.PlayerDirectory.lexically_normal() &&
        expected->CheckpointsDirectory.lexically_normal() ==
               acPaths.CheckpointsDirectory.lexically_normal() &&
        expected->SavesDirectory.lexically_normal() ==
               acPaths.SavesDirectory.lexically_normal() &&
        expected->SidecarsDirectory.lexically_normal() ==
               acPaths.SidecarsDirectory.lexically_normal() &&
        expected->MetadataDirectory.lexically_normal() ==
               acPaths.MetadataDirectory.lexically_normal() &&
        expected->RuntimeApplySidecar.lexically_normal() ==
               acPaths.RuntimeApplySidecar.lexically_normal();
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
        const auto metadataRoot = AbsoluteNormalized(acPaths.MetadataDirectory);
        const auto checkpointRoot = AbsoluteNormalized(
            PartyQuestCoopSaveLayout::GetCheckpointDirectory(
                acPaths, acState.CheckpointKind));
        const auto transaction = AbsoluteNormalized(acState.TransactionDirectory);
        const auto journal = AbsoluteNormalized(acJournalPath);
        const auto expectedJournal = AbsoluteNormalized(
            PartyQuestReplicaRestoreJournal::GetJournalPath(acState));
        const auto expectedTransaction = AbsoluteNormalized(
            acPaths.MetadataDirectory / "restore" / FormatRestoreId(acState.RestoreId));
        if (!playerRoot || !metadataRoot || !checkpointRoot || !transaction ||
            !journal || !expectedJournal || !expectedTransaction ||
            *journal != *expectedJournal || *transaction != *expectedTransaction ||
            !IsInside(*playerRoot, *transaction) ||
            !IsInside(*metadataRoot, *transaction) ||
            !IsParentResolvedInside(*playerRoot, *journal))
        {
            return false;
        }

        const auto savesRoot = AbsoluteNormalized(acPaths.SavesDirectory);
        const auto externalSidecarsRoot = AbsoluteNormalized(
            acPaths.SidecarsDirectory / "external");
        const auto runtimeApply = AbsoluteNormalized(acPaths.RuntimeApplySidecar);
        if (!savesRoot || !externalSidecarsRoot || !runtimeApply)
            return false;

        std::set<std::filesystem::path> destinations;
        std::set<std::filesystem::path> rollbacks;
        for (const auto& operation : acState.Operations)
        {
            const auto source = AbsoluteNormalized(operation.CheckpointSourcePath);
            const auto destination = AbsoluteNormalized(operation.ReplicaDestinationPath);
            const auto rollback = AbsoluteNormalized(operation.RollbackPath);
            if (!source || !destination || !rollback ||
                !IsInside(*checkpointRoot, *source) ||
                !IsInside(*playerRoot, *destination) ||
                *destination == *runtimeApply ||
                !destinations.emplace(*destination).second ||
                !rollbacks.emplace(*rollback).second)
            {
                return false;
            }

            if (operation.Kind == PartyQuestReplicaFileKind::ExternalSidecar)
            {
                if (!IsInside(*externalSidecarsRoot, *destination))
                    return false;
            }
            else if (destination->parent_path() != *savesRoot ||
                     !HasExpectedExtension(operation.Kind, *destination))
            {
                return false;
            }

            const auto relative = destination->lexically_relative(*playerRoot).lexically_normal();
            if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
                return false;

            const auto expectedRollback = AbsoluteNormalized(
                *transaction / "rollback" / relative);
            if (!expectedRollback || *rollback != *expectedRollback ||
                !IsInside(*transaction / "rollback", *rollback) ||
                !IsParentResolvedInside(*checkpointRoot, *source) ||
                !IsParentResolvedInside(*playerRoot, *destination) ||
                !IsParentResolvedInside(*transaction, *rollback))
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

bool MatchesFile(
    const std::filesystem::path& acPath,
    uint64_t aExpectedSize,
    uint64_t aExpectedDigest) noexcept
{
    const auto observed = PartyQuestReplicaFileExecutor::ObserveRegularFile(acPath);
    return observed &&
        observed->Size == aExpectedSize &&
        observed->Digest == aExpectedDigest;
}

bool EnsureFileDurableAndMatches(
    const std::filesystem::path& acPath,
    uint64_t aExpectedSize,
    uint64_t aExpectedDigest) noexcept
{
    if (!MatchesFile(acPath, aExpectedSize, aExpectedDigest))
        return false;
    if (PartyQuestStableStorage::FlushFile(acPath) != PartyQuestStableStorageStatus::Success ||
        PartyQuestStableStorage::FlushDirectory(acPath.parent_path()) !=
            PartyQuestStableStorageStatus::Success)
    {
        return false;
    }
    return MatchesFile(acPath, aExpectedSize, aExpectedDigest);
}

bool MatchesOriginal(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    if (acOperation.DestinationExisted)
    {
        return MatchesFile(
            acOperation.ReplicaDestinationPath,
            acOperation.OriginalSize,
            acOperation.OriginalDigest);
    }
    return InspectNode(acOperation.ReplicaDestinationPath) == NodeState::Missing;
}

bool EnsureOriginalDurableAndMatches(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    if (acOperation.DestinationExisted)
    {
        return EnsureFileDurableAndMatches(
            acOperation.ReplicaDestinationPath,
            acOperation.OriginalSize,
            acOperation.OriginalDigest);
    }

    if (InspectNode(acOperation.ReplicaDestinationPath) != NodeState::Missing ||
        PartyQuestStableStorage::FlushDirectory(
            acOperation.ReplicaDestinationPath.parent_path()) !=
            PartyQuestStableStorageStatus::Success)
    {
        return false;
    }
    return InspectNode(acOperation.ReplicaDestinationPath) == NodeState::Missing;
}

bool MatchesCheckpoint(
    const PartyQuestReplicaRestoreJournalOperation& acOperation) noexcept
{
    return MatchesFile(
        acOperation.CheckpointSourcePath,
        acOperation.ExpectedRestoredSize,
        acOperation.ExpectedRestoredDigest);
}

bool VerifyAllOriginals(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (const auto& operation : acState.Operations)
    {
        if (!MatchesOriginal(operation))
            return false;
    }
    return PartyQuestReplicaRestoreJournal::VerifyOriginalTargets(acState);
}

bool VerifyAllOriginalsDurably(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (const auto& operation : acState.Operations)
    {
        if (!EnsureOriginalDurableAndMatches(operation))
            return false;
    }
    return PartyQuestReplicaRestoreJournal::VerifyOriginalTargets(acState);
}

bool VerifyAllRestoredDurably(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (const auto& operation : acState.Operations)
    {
        if (!EnsureFileDurableAndMatches(
                operation.ReplicaDestinationPath,
                operation.ExpectedRestoredSize,
                operation.ExpectedRestoredDigest))
        {
            return false;
        }
    }
    return PartyQuestReplicaRestoreJournal::VerifyRestoredTargets(acState);
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

bool RemoveRegularIfPresentDurably(const std::filesystem::path& acPath) noexcept
{
    const NodeState state = InspectNode(acPath);
    if (state == NodeState::Missing)
        return true;
    if (state != NodeState::RegularFile)
        return false;
    return PartyQuestStableStorage::RemoveFileDurably(acPath) ==
        PartyQuestStableStorageStatus::Success;
}

bool RemoveDirectoryIfPresentDurably(const std::filesystem::path& acPath) noexcept
{
    const NodeState state = InspectNode(acPath);
    if (state == NodeState::Missing)
        return true;
    if (state != NodeState::Directory)
        return false;
    return PartyQuestStableStorage::RemoveEmptyDirectoryDurably(acPath) ==
        PartyQuestStableStorageStatus::Success;
}

PartyQuestReplicaDurableRestoreStatus MapLeaseStatus(
    PartyQuestReplicaWorkspaceLeaseStatus aStatus) noexcept
{
    return aStatus == PartyQuestReplicaWorkspaceLeaseStatus::Busy
        ? PartyQuestReplicaDurableRestoreStatus::WorkspaceBusy
        : PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure;
}

PartyQuestReplicaDurableRestoreReport MakeReport(
    PartyQuestReplicaDurableRestoreStatus aStatus,
    const std::filesystem::path& acJournalPath,
    const PartyQuestReplicaRestoreJournalState* apState = nullptr,
    size_t aFailedOperation = 0,
    std::filesystem::path aFailedPath = {})
{
    PartyQuestReplicaDurableRestoreReport report;
    report.Status = aStatus;
    report.JournalPath = acJournalPath;
    report.FailedOperation = aFailedOperation;
    report.FailedPath = std::move(aFailedPath);
    if (apState)
        report.Phase = apState->Phase;
    return report;
}

bool StateMatchesPromotedPlan(
    const PartyQuestReplicaRestoreJournalState& acState,
    const PartyQuestReplicaRestorePlan& acPlan) noexcept
{
    if (!acPlan.IsReady() ||
        acPlan.CampaignId != acState.CampaignId ||
        acPlan.PlayerProfileId != acState.PlayerProfileId ||
        acPlan.CheckpointKind != acState.CheckpointKind ||
        acPlan.CampaignWorldRevision != acState.CampaignWorldRevision ||
        acPlan.Operations.size() != acState.Operations.size())
    {
        return false;
    }

    for (size_t i = 0; i < acPlan.Operations.size(); ++i)
    {
        const auto& plan = acPlan.Operations[i];
        const auto& journal = acState.Operations[i];
        if (plan.Kind != journal.Kind ||
            plan.CheckpointSourcePath.lexically_normal() !=
                journal.CheckpointSourcePath.lexically_normal() ||
            plan.ReplicaDestinationPath.lexically_normal() !=
                journal.ReplicaDestinationPath.lexically_normal() ||
            plan.ExpectedSize != journal.ExpectedRestoredSize ||
            plan.ExpectedDigest != journal.ExpectedRestoredDigest)
        {
            return false;
        }
    }
    return true;
}

PartyQuestReplicaDurableRestoreStatus RebindPromotedCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    const auto promotion = PartyQuestReplicaDurableSnapshot::PromoteRevisionCheckpoint(
        acPaths,
        acState.CampaignId,
        acState.PlayerProfileId,
        acState.CheckpointKind,
        acState.CampaignWorldRevision);
    if (!promotion.IsPromoted())
        return PartyQuestReplicaDurableRestoreStatus::CheckpointDurabilityUnavailable;

    const auto manifestPath = PartyQuestReplicaManifestStore::GetRevisionCheckpointManifestPath(
        acPaths,
        acState.CheckpointKind,
        acState.CampaignWorldRevision);
    const auto loaded = PartyQuestReplicaManifestStore::Load(manifestPath);
    if (loaded.Status != PartyQuestReplicaManifestPersistenceStatus::Success ||
        !loaded.Manifest)
    {
        return PartyQuestReplicaDurableRestoreStatus::CheckpointDurabilityUnavailable;
    }

    const auto plan = PartyQuestReplicaRestorePlanner::Build(
        acPaths,
        acState.CampaignId,
        acState.PlayerProfileId,
        *loaded.Manifest);
    return StateMatchesPromotedPlan(acState, plan)
        ? PartyQuestReplicaDurableRestoreStatus::Success
        : PartyQuestReplicaDurableRestoreStatus::CheckpointPlanMismatch;
}

bool VerifyRollbackEvidenceDurably(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    for (const auto& operation : acState.Operations)
    {
        if (!operation.DestinationExisted)
        {
            if (InspectNode(operation.RollbackPath) != NodeState::Missing)
                return false;
            continue;
        }
        if (!EnsureFileDurableAndMatches(
                operation.RollbackPath,
                operation.OriginalSize,
                operation.OriginalDigest))
        {
            return false;
        }
    }
    return PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(acState);
}

PartyQuestReplicaDurableRestoreStatus BuildForwardStages(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    std::vector<std::filesystem::path>& aStages,
    size_t& aFailedOperation,
    std::filesystem::path& aFailedPath) noexcept
{
    aStages.clear();
    aStages.reserve(acState.Operations.size());

    std::set<std::filesystem::path> destinations;
    for (const auto& operation : acState.Operations)
        destinations.emplace(operation.ReplicaDestinationPath.lexically_normal());

    std::set<std::filesystem::path> stages;
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        const auto stage = BuildSiblingTemporary(
            operation.ReplicaDestinationPath,
            "durable-stage",
            acState.RestoreId,
            i).lexically_normal();
        if (!PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(stage) ||
            stage.parent_path() != operation.ReplicaDestinationPath.parent_path() ||
            !IsInside(acPaths.PlayerDirectory, stage) ||
            destinations.contains(stage) ||
            !stages.emplace(stage).second)
        {
            aFailedOperation = i;
            aFailedPath = stage;
            return PartyQuestReplicaDurableRestoreStatus::UnsafePath;
        }
        aStages.push_back(stage);
    }

    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto& operation = acState.Operations[i];
        const auto& stage = aStages[i];
        if (!MatchesCheckpoint(operation))
        {
            aFailedOperation = i;
            aFailedPath = operation.CheckpointSourcePath;
            return PartyQuestReplicaDurableRestoreStatus::CheckpointSourceChanged;
        }
        if (PartyQuestStableStorage::EnsureDirectoryTreeDurably(
                operation.ReplicaDestinationPath.parent_path()) !=
            PartyQuestStableStorageStatus::Success)
        {
            aFailedOperation = i;
            aFailedPath = operation.ReplicaDestinationPath.parent_path();
            return PartyQuestReplicaDurableRestoreStatus::StagingFailed;
        }

        const NodeState stageState = InspectNode(stage);
        if (stageState == NodeState::RegularFile)
        {
            if (!MatchesFile(
                    stage,
                    operation.ExpectedRestoredSize,
                    operation.ExpectedRestoredDigest))
            {
                if (!RemoveRegularIfPresentDurably(stage))
                {
                    aFailedOperation = i;
                    aFailedPath = stage;
                    return PartyQuestReplicaDurableRestoreStatus::StagingFailed;
                }
            }
        }
        else if (stageState != NodeState::Missing)
        {
            aFailedOperation = i;
            aFailedPath = stage;
            return PartyQuestReplicaDurableRestoreStatus::UnsafePath;
        }

        if (InspectNode(stage) == NodeState::Missing)
        {
            if (PartyQuestStableStorage::CopyFileDurably(
                    operation.CheckpointSourcePath,
                    stage) != PartyQuestStableStorageStatus::Success)
            {
                aFailedOperation = i;
                aFailedPath = stage;
                return PartyQuestReplicaDurableRestoreStatus::StagingFailed;
            }
        }

        if (!EnsureFileDurableAndMatches(
                stage,
                operation.ExpectedRestoredSize,
                operation.ExpectedRestoredDigest) ||
            !MatchesCheckpoint(operation))
        {
            aFailedOperation = i;
            aFailedPath = stage;
            return PartyQuestReplicaDurableRestoreStatus::StagingFailed;
        }
    }

    return PartyQuestReplicaDurableRestoreStatus::Success;
}

bool CleanupStagesDurably(
    const PartyQuestReplicaRestoreJournalState& acState,
    bool aIncludeRollbackStages) noexcept
{
    bool success = true;
    for (size_t i = 0; i < acState.Operations.size(); ++i)
    {
        const auto forward = BuildSiblingTemporary(
            acState.Operations[i].ReplicaDestinationPath,
            "durable-stage",
            acState.RestoreId,
            i);
        if (!RemoveRegularIfPresentDurably(forward))
            success = false;

        if (aIncludeRollbackStages)
        {
            const auto rollback = BuildSiblingTemporary(
                acState.Operations[i].ReplicaDestinationPath,
                "durable-rollback",
                acState.RestoreId,
                i);
            if (!RemoveRegularIfPresentDurably(rollback))
                success = false;
        }
    }
    return success;
}

bool CompactTerminalTransaction(
    const PartyQuestReplicaRestoreJournalState& acState,
    const std::filesystem::path& acJournalPath) noexcept
{
    const bool committed =
        acState.Phase == PartyQuestReplicaRestoreJournalPhase::Committed &&
        VerifyAllRestoredDurably(acState);
    const bool rolledBack =
        acState.Phase == PartyQuestReplicaRestoreJournalPhase::RolledBack &&
        VerifyAllOriginalsDurably(acState);
    if (!committed && !rolledBack)
        return false;

    bool success = CleanupStagesDurably(acState, true);

    std::set<std::filesystem::path> rollbackDirectories;
    for (const auto& operation : acState.Operations)
    {
        if (!RemoveRegularIfPresentDurably(operation.RollbackPath))
            success = false;

        auto directory = operation.RollbackPath.parent_path().lexically_normal();
        while (!directory.empty() &&
               directory != acState.TransactionDirectory.lexically_normal())
        {
            rollbackDirectories.emplace(directory);
            directory = directory.parent_path().lexically_normal();
        }
    }

    auto temporaryJournal = acJournalPath;
    temporaryJournal += ".tmp";
    auto backupJournal = acJournalPath;
    backupJournal += ".bak";
    if (!RemoveRegularIfPresentDurably(temporaryJournal))
        success = false;
    if (!RemoveRegularIfPresentDurably(backupJournal))
        success = false;

    std::vector<std::filesystem::path> directories(
        rollbackDirectories.begin(), rollbackDirectories.end());
    std::sort(directories.begin(), directories.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.native().size() > acRight.native().size();
    });
    for (const auto& directory : directories)
    {
        if (!RemoveDirectoryIfPresentDurably(directory))
            success = false;
    }

    if (PartyQuestStableStorage::FlushFile(acJournalPath) !=
            PartyQuestStableStorageStatus::Success ||
        PartyQuestStableStorage::FlushDirectory(acState.TransactionDirectory) !=
            PartyQuestStableStorageStatus::Success)
    {
        success = false;
    }

    // The primary terminal journal and transaction directory deliberately stay
    // as a compact RestoreId tombstone. Do not rmdir the transaction root here.
    return success;
}

PartyQuestReplicaDurableRestoreReport RollbackFromDurableBarrier(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestoreJournalState& acState,
    const std::filesystem::path& acJournalPath,
    PartyQuestReplicaDurableRestoreHooks aHooks) noexcept
{
    auto report = MakeReport(
        PartyQuestReplicaDurableRestoreStatus::RecoveredRollback,
        acJournalPath,
        &acState);

    for (size_t reverse = acState.Operations.size(); reverse > 0; --reverse)
    {
        const size_t i = reverse - 1;
        const auto& operation = acState.Operations[i];

        if (!operation.DestinationExisted)
        {
            const NodeState destination = InspectNode(operation.ReplicaDestinationPath);
            if (destination == NodeState::RegularFile)
            {
                if (!RemoveRegularIfPresentDurably(operation.ReplicaDestinationPath))
                {
                    report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                    report.FailedOperation = i;
                    report.FailedPath = operation.ReplicaDestinationPath;
                    report.RequiresRecovery = true;
                    return report;
                }
            }
            else if (destination != NodeState::Missing)
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = operation.ReplicaDestinationPath;
                report.RequiresRecovery = true;
                return report;
            }
        }
        else if (!MatchesOriginal(operation))
        {
            if (!MatchesFile(
                    operation.RollbackPath,
                    operation.OriginalSize,
                    operation.OriginalDigest))
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = operation.RollbackPath;
                report.RequiresRecovery = true;
                return report;
            }

            const NodeState destination = InspectNode(operation.ReplicaDestinationPath);
            if (destination != NodeState::Missing && destination != NodeState::RegularFile)
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = operation.ReplicaDestinationPath;
                report.RequiresRecovery = true;
                return report;
            }

            const auto stage = BuildSiblingTemporary(
                operation.ReplicaDestinationPath,
                "durable-rollback",
                acState.RestoreId,
                i);
            if (!PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(stage) ||
                !IsInside(acPaths.PlayerDirectory, stage) ||
                PartyQuestStableStorage::EnsureDirectoryTreeDurably(
                    operation.ReplicaDestinationPath.parent_path()) !=
                    PartyQuestStableStorageStatus::Success)
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = stage;
                report.RequiresRecovery = true;
                return report;
            }

            const NodeState stageState = InspectNode(stage);
            if (stageState == NodeState::RegularFile)
            {
                if (!RemoveRegularIfPresentDurably(stage))
                {
                    report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                    report.FailedOperation = i;
                    report.FailedPath = stage;
                    report.RequiresRecovery = true;
                    return report;
                }
            }
            else if (stageState != NodeState::Missing)
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = stage;
                report.RequiresRecovery = true;
                return report;
            }

            if (PartyQuestStableStorage::CopyFileDurably(
                    operation.RollbackPath,
                    stage) != PartyQuestStableStorageStatus::Success ||
                !EnsureFileDurableAndMatches(
                    stage,
                    operation.OriginalSize,
                    operation.OriginalDigest) ||
                PartyQuestStableStorage::PublishFileRename(
                    stage,
                    operation.ReplicaDestinationPath,
                    true) != PartyQuestStableStorageStatus::Success ||
                !EnsureFileDurableAndMatches(
                    operation.ReplicaDestinationPath,
                    operation.OriginalSize,
                    operation.OriginalDigest))
            {
                report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
                report.FailedOperation = i;
                report.FailedPath = operation.ReplicaDestinationPath;
                report.RequiresRecovery = true;
                return report;
            }
        }

        ++report.CompletedOperations;
        if (aHooks.Invoke(
                PartyQuestReplicaDurableRestoreBoundary::OriginalStateRestored,
                i) == PartyQuestReplicaDurableRestoreDirective::FailClosed)
        {
            report.Status = PartyQuestReplicaDurableRestoreStatus::FaultInjected;
            report.FailedOperation = i;
            report.FailedPath = operation.ReplicaDestinationPath;
            report.RequiresRecovery = true;
            return report;
        }
    }

    if (!VerifyAllOriginalsDurably(acState))
    {
        report.Status = PartyQuestReplicaDurableRestoreStatus::RollbackFailed;
        report.RequiresRecovery = true;
        return report;
    }

    auto rolledBack = acState;
    if (PartyQuestReplicaRestoreJournal::MarkRolledBack(rolledBack) !=
            PartyQuestReplicaRestoreJournalStatus::Ready ||
        PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
            acJournalPath,
            rolledBack) != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        report.Status = PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed;
        report.Phase = rolledBack.Phase;
        report.RollbackPerformed = true;
        report.CleanupPending = true;
        report.RequiresRecovery = true;
        return report;
    }

    report.Phase = rolledBack.Phase;
    report.RollbackPerformed = true;
    if (aHooks.Invoke(
            PartyQuestReplicaDurableRestoreBoundary::RolledBackDurable) ==
        PartyQuestReplicaDurableRestoreDirective::FailClosed)
    {
        report.Status = PartyQuestReplicaDurableRestoreStatus::FaultInjected;
        report.CleanupPending = true;
        report.RequiresRecovery = true;
        return report;
    }

    report.CleanupPending = !CompactTerminalTransaction(rolledBack, acJournalPath);
    report.RequiresRecovery = false;
    return report;
}

PartyQuestReplicaDurableRestoreReport LoadFailureReport(
    const std::filesystem::path& acJournalPath,
    PartyQuestReplicaRestoreJournalPersistenceStatus aStatus)
{
    return MakeReport(
        aStatus == PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound
            ? PartyQuestReplicaDurableRestoreStatus::JournalNotFound
            : PartyQuestReplicaDurableRestoreStatus::JournalLoadFailed,
        acJournalPath);
}

bool ValidateExpectedIdentity(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    return acCampaignId.IsValid() && acPlayerProfileId.IsValid();
}
} // namespace

PartyQuestReplicaDurableRestoreReport PartyQuestReplicaDurableRestoreExecutor::Continue(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const std::filesystem::path& acJournalPath,
    PartyQuestReplicaDurableRestoreHooks aHooks) noexcept
{
#ifdef _WIN32
    (void)acPaths;
    (void)acExpectedCampaignId;
    (void)acExpectedPlayerProfileId;
    (void)aHooks;
    return MakeReport(
        PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform,
        acJournalPath);
#else
    try
    {
        if (!ValidateExpectedIdentity(
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath);
        }

        PartyQuestReplicaWorkspaceLease workspace;
        const auto lease = workspace.Acquire(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId);
        if (lease != PartyQuestReplicaWorkspaceLeaseStatus::Acquired)
            return MakeReport(MapLeaseStatus(lease), acJournalPath);

        const auto capability = workspace.CreatePublicationCapability(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId);
        if (!capability.Protects(
                acPaths,
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure,
                acJournalPath);
        }

        return ContinueAuthorized(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId,
            acJournalPath,
            capability,
            aHooks);
    }
    catch (...)
    {
        auto report = MakeReport(
            PartyQuestReplicaDurableRestoreStatus::UnsafePath,
            acJournalPath);
        report.RequiresRecovery = true;
        return report;
    }
#endif
}

PartyQuestReplicaDurableRestoreReport PartyQuestReplicaDurableRestoreExecutor::ContinueAuthorized(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const std::filesystem::path& acJournalPath,
    const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
    PartyQuestReplicaDurableRestoreHooks aHooks) noexcept
{
#ifdef _WIN32
    (void)acPaths;
    (void)acExpectedCampaignId;
    (void)acExpectedPlayerProfileId;
    (void)acWorkspaceCapability;
    (void)aHooks;
    return MakeReport(
        PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform,
        acJournalPath);
#else
    try
    {
        if (!ValidateExpectedIdentity(
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath);
        }
        if (!acWorkspaceCapability.Protects(
                acPaths,
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure,
                acJournalPath);
        }

        const auto loaded =
            PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
                acJournalPath);
        if (loaded.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
            !loaded.State)
        {
            return LoadFailureReport(acJournalPath, loaded.Status);
        }

        auto state = *loaded.State;
        if (state.CampaignId != acExpectedCampaignId ||
            state.PlayerProfileId != acExpectedPlayerProfileId)
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath,
                &state);
        }
        if (!ValidateStatePaths(acPaths, state, acJournalPath))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::UnsafePath,
                acJournalPath,
                &state);
        }
        if (state.Phase != PartyQuestReplicaRestoreJournalPhase::BackupsReady)
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidPhase,
                acJournalPath,
                &state);
        }

        const auto checkpointStatus = RebindPromotedCheckpoint(acPaths, state);
        if (checkpointStatus != PartyQuestReplicaDurableRestoreStatus::Success)
            return MakeReport(checkpointStatus, acJournalPath, &state);
        if (!VerifyRollbackEvidenceDurably(state))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::BackupVerificationFailed,
                acJournalPath,
                &state);
        }

        std::vector<std::filesystem::path> stages;
        size_t failedOperation{};
        std::filesystem::path failedPath;
        const auto stageStatus = BuildForwardStages(
            acPaths,
            state,
            stages,
            failedOperation,
            failedPath);
        if (stageStatus != PartyQuestReplicaDurableRestoreStatus::Success)
        {
            auto report = MakeReport(
                stageStatus,
                acJournalPath,
                &state,
                failedOperation,
                failedPath);
            report.CleanupPending = !CleanupStagesDurably(state, false);
            return report;
        }

        for (size_t i = 0; i < state.Operations.size(); ++i)
        {
            if (!MatchesOriginal(state.Operations[i]))
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::DestinationChanged,
                    acJournalPath,
                    &state,
                    i,
                    state.Operations[i].ReplicaDestinationPath);
                report.CleanupPending = !CleanupStagesDurably(state, false);
                return report;
            }
            if (!MatchesCheckpoint(state.Operations[i]))
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::CheckpointSourceChanged,
                    acJournalPath,
                    &state,
                    i,
                    state.Operations[i].CheckpointSourcePath);
                report.CleanupPending = !CleanupStagesDurably(state, false);
                return report;
            }
        }

        auto mutationStarted = state;
        if (PartyQuestReplicaRestoreJournal::MarkMutationStarted(mutationStarted) !=
            PartyQuestReplicaRestoreJournalStatus::Ready)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidPhase,
                acJournalPath,
                &state);
            report.CleanupPending = !CleanupStagesDurably(state, false);
            return report;
        }
        if (PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                acJournalPath,
                mutationStarted) !=
            PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed,
                acJournalPath,
                &mutationStarted);
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }
        state = std::move(mutationStarted);

        if (aHooks.Invoke(
                PartyQuestReplicaDurableRestoreBoundary::MutationStartedDurable) ==
            PartyQuestReplicaDurableRestoreDirective::FailClosed)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::FaultInjected,
                acJournalPath,
                &state);
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }

        size_t completed = 0;
        for (size_t i = 0; i < state.Operations.size(); ++i)
        {
            const auto& operation = state.Operations[i];
            if (i >= stages.size() ||
                !EnsureFileDurableAndMatches(
                    stages[i],
                    operation.ExpectedRestoredSize,
                    operation.ExpectedRestoredDigest) ||
                !MatchesOriginal(operation))
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::ReplacementFailed,
                    acJournalPath,
                    &state,
                    i,
                    operation.ReplicaDestinationPath);
                report.CompletedOperations = completed;
                report.RequiresRecovery = true;
                report.CleanupPending = true;
                return report;
            }

            if (PartyQuestStableStorage::PublishFileRename(
                    stages[i],
                    operation.ReplicaDestinationPath,
                    operation.DestinationExisted) !=
                    PartyQuestStableStorageStatus::Success ||
                !EnsureFileDurableAndMatches(
                    operation.ReplicaDestinationPath,
                    operation.ExpectedRestoredSize,
                    operation.ExpectedRestoredDigest))
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::ReplacementFailed,
                    acJournalPath,
                    &state,
                    i,
                    operation.ReplicaDestinationPath);
                report.CompletedOperations = completed;
                report.RequiresRecovery = true;
                report.CleanupPending = true;
                return report;
            }

            ++completed;
            if (aHooks.Invoke(
                    PartyQuestReplicaDurableRestoreBoundary::RestoredFilePublished,
                    i) == PartyQuestReplicaDurableRestoreDirective::FailClosed)
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::FaultInjected,
                    acJournalPath,
                    &state,
                    i,
                    operation.ReplicaDestinationPath);
                report.CompletedOperations = completed;
                report.RequiresRecovery = true;
                report.CleanupPending = true;
                return report;
            }
        }

        if (!VerifyAllRestoredDurably(state))
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::RestoredVerificationFailed,
                acJournalPath,
                &state);
            report.CompletedOperations = completed;
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }

        auto restored = state;
        if (PartyQuestReplicaRestoreJournal::MarkRestored(restored) !=
                PartyQuestReplicaRestoreJournalStatus::Ready ||
            PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                acJournalPath,
                restored) != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed,
                acJournalPath,
                &restored);
            report.CompletedOperations = completed;
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }
        state = std::move(restored);

        if (aHooks.Invoke(
                PartyQuestReplicaDurableRestoreBoundary::RestoredDurable) ==
            PartyQuestReplicaDurableRestoreDirective::FailClosed)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::FaultInjected,
                acJournalPath,
                &state);
            report.CompletedOperations = completed;
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }

        auto committed = state;
        if (PartyQuestReplicaRestoreJournal::MarkCommitted(committed) !=
                PartyQuestReplicaRestoreJournalStatus::Ready ||
            PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                acJournalPath,
                committed) != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed,
                acJournalPath,
                &committed);
            report.CompletedOperations = completed;
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }
        state = std::move(committed);

        if (aHooks.Invoke(
                PartyQuestReplicaDurableRestoreBoundary::CommittedDurable) ==
            PartyQuestReplicaDurableRestoreDirective::FailClosed)
        {
            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::FaultInjected,
                acJournalPath,
                &state);
            report.CompletedOperations = completed;
            report.RequiresRecovery = true;
            report.CleanupPending = true;
            return report;
        }

        auto report = MakeReport(
            PartyQuestReplicaDurableRestoreStatus::Success,
            acJournalPath,
            &state);
        report.CompletedOperations = completed;
        report.CleanupPending = !CompactTerminalTransaction(state, acJournalPath);
        return report;
    }
    catch (...)
    {
        auto report = MakeReport(
            PartyQuestReplicaDurableRestoreStatus::UnsafePath,
            acJournalPath);
        report.RequiresRecovery = true;
        return report;
    }
#endif
}

PartyQuestReplicaDurableRestoreReport PartyQuestReplicaDurableRestoreExecutor::Recover(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const std::filesystem::path& acJournalPath,
    PartyQuestReplicaDurableRestoreHooks aHooks) noexcept
{
#ifdef _WIN32
    (void)acPaths;
    (void)acExpectedCampaignId;
    (void)acExpectedPlayerProfileId;
    (void)aHooks;
    return MakeReport(
        PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform,
        acJournalPath);
#else
    try
    {
        if (!ValidateExpectedIdentity(
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath);
        }

        PartyQuestReplicaWorkspaceLease workspace;
        const auto lease = workspace.Acquire(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId);
        if (lease != PartyQuestReplicaWorkspaceLeaseStatus::Acquired)
            return MakeReport(MapLeaseStatus(lease), acJournalPath);

        const auto capability = workspace.CreatePublicationCapability(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId);
        if (!capability.Protects(
                acPaths,
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure,
                acJournalPath);
        }

        return RecoverAuthorized(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId,
            acJournalPath,
            capability,
            aHooks);
    }
    catch (...)
    {
        auto report = MakeReport(
            PartyQuestReplicaDurableRestoreStatus::UnsafePath,
            acJournalPath);
        report.RequiresRecovery = true;
        return report;
    }
#endif
}

PartyQuestReplicaDurableRestoreReport PartyQuestReplicaDurableRestoreExecutor::RecoverAuthorized(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
    const std::filesystem::path& acJournalPath,
    const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
    PartyQuestReplicaDurableRestoreHooks aHooks) noexcept
{
#ifdef _WIN32
    (void)acPaths;
    (void)acExpectedCampaignId;
    (void)acExpectedPlayerProfileId;
    (void)acWorkspaceCapability;
    (void)aHooks;
    return MakeReport(
        PartyQuestReplicaDurableRestoreStatus::UnsupportedPlatform,
        acJournalPath);
#else
    try
    {
        if (!ValidateExpectedIdentity(
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath);
        }
        if (!acWorkspaceCapability.Protects(
                acPaths,
                acExpectedCampaignId,
                acExpectedPlayerProfileId))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::WorkspaceLeaseFailure,
                acJournalPath);
        }

        const auto loaded =
            PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
                acJournalPath);
        if (loaded.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
            !loaded.State)
        {
            return LoadFailureReport(acJournalPath, loaded.Status);
        }

        auto state = *loaded.State;
        if (state.CampaignId != acExpectedCampaignId ||
            state.PlayerProfileId != acExpectedPlayerProfileId)
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::InvalidIdentity,
                acJournalPath,
                &state);
        }
        if (!ValidateStatePaths(acPaths, state, acJournalPath))
        {
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::UnsafePath,
                acJournalPath,
                &state);
        }

        switch (state.Phase)
        {
        case PartyQuestReplicaRestoreJournalPhase::Prepared:
        case PartyQuestReplicaRestoreJournalPhase::BackupsReady:
            return MakeReport(
                PartyQuestReplicaDurableRestoreStatus::ResumeBeforeMutation,
                acJournalPath,
                &state);

        case PartyQuestReplicaRestoreJournalPhase::MutationStarted:
            return RollbackFromDurableBarrier(
                acPaths,
                state,
                acJournalPath,
                aHooks);

        case PartyQuestReplicaRestoreJournalPhase::Restored:
        {
            if (!VerifyAllRestoredDurably(state))
            {
                return RollbackFromDurableBarrier(
                    acPaths,
                    state,
                    acJournalPath,
                    aHooks);
            }

            auto committed = state;
            if (PartyQuestReplicaRestoreJournal::MarkCommitted(committed) !=
                    PartyQuestReplicaRestoreJournalStatus::Ready ||
                PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                    acJournalPath,
                    committed) !=
                    PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::JournalPersistenceFailed,
                    acJournalPath,
                    &committed);
                report.RequiresRecovery = true;
                report.CleanupPending = true;
                return report;
            }
            state = std::move(committed);

            if (aHooks.Invoke(
                    PartyQuestReplicaDurableRestoreBoundary::CommittedDurable) ==
                PartyQuestReplicaDurableRestoreDirective::FailClosed)
            {
                auto report = MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::FaultInjected,
                    acJournalPath,
                    &state);
                report.RequiresRecovery = true;
                report.CleanupPending = true;
                return report;
            }

            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::RecoveredCommit,
                acJournalPath,
                &state);
            report.CompletedOperations = state.Operations.size();
            report.CleanupPending = !CompactTerminalTransaction(state, acJournalPath);
            return report;
        }

        case PartyQuestReplicaRestoreJournalPhase::Committed:
        {
            if (!VerifyAllRestoredDurably(state))
            {
                return MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::CommittedVerificationFailed,
                    acJournalPath,
                    &state);
            }

            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::AlreadyCommitted,
                acJournalPath,
                &state);
            report.CompletedOperations = state.Operations.size();
            report.CleanupPending = !CompactTerminalTransaction(state, acJournalPath);
            return report;
        }

        case PartyQuestReplicaRestoreJournalPhase::RolledBack:
        {
            if (!VerifyAllOriginalsDurably(state))
            {
                return MakeReport(
                    PartyQuestReplicaDurableRestoreStatus::RolledBackVerificationFailed,
                    acJournalPath,
                    &state);
            }

            auto report = MakeReport(
                PartyQuestReplicaDurableRestoreStatus::AlreadyRolledBack,
                acJournalPath,
                &state);
            report.CompletedOperations = state.Operations.size();
            report.RollbackPerformed = true;
            report.CleanupPending = !CompactTerminalTransaction(state, acJournalPath);
            return report;
        }
        }

        return MakeReport(
            PartyQuestReplicaDurableRestoreStatus::InvalidPhase,
            acJournalPath,
            &state);
    }
    catch (...)
    {
        auto report = MakeReport(
            PartyQuestReplicaDurableRestoreStatus::UnsafePath,
            acJournalPath);
        report.RequiresRecovery = true;
        return report;
    }
#endif
}