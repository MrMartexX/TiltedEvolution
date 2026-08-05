#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

#include <algorithm>
#include <string>
#include <type_traits>

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

uint64_t FingerprintFiles(
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
{
    try
    {
        std::vector<const PartyQuestReplicaFileSpec*> ordered;
        ordered.reserve(acFiles.size());
        for (const auto& file : acFiles)
            ordered.push_back(&file);

        std::sort(ordered.begin(), ordered.end(), [](const auto* apLeft, const auto* apRight)
        {
            const std::string leftRelative = apLeft->RelativePath.lexically_normal().generic_string();
            const std::string rightRelative = apRight->RelativePath.lexically_normal().generic_string();
            if (leftRelative != rightRelative)
                return leftRelative < rightRelative;

            const std::string leftSource = apLeft->SourcePath.lexically_normal().generic_string();
            const std::string rightSource = apRight->SourcePath.lexically_normal().generic_string();
            if (leftSource != rightSource)
                return leftSource < rightSource;
            return static_cast<uint8_t>(apLeft->Kind) < static_cast<uint8_t>(apRight->Kind);
        });

        uint64_t hash = kFnvOffset;
        const uint64_t count = static_cast<uint64_t>(ordered.size());
        HashValue(hash, count);
        for (const auto* file : ordered)
        {
            const auto kind = static_cast<uint8_t>(file->Kind);
            HashValue(hash, kind);
            HashString(hash, file->SourcePath.lexically_normal().generic_string());
            HashString(hash, file->RelativePath.lexically_normal().generic_string());
            HashValue(hash, file->Size);
            HashValue(hash, file->Digest);
        }
        return hash != 0 ? hash : 1;
    }
    catch (...)
    {
        return 0;
    }
}

bool IsInsideNamespace(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acSource) noexcept
{
    return !acRoot.empty() &&
        !acSource.empty() &&
        acRoot.is_absolute() &&
        acSource.is_absolute() &&
        PartyQuestReplicaFilePlanner::IsContainedBy(acRoot, acSource);
}

bool ValidateCoreFiles(
    const PartyQuestCoopSavePaths& acPaths,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
{
    try
    {
        size_t essCount{};
        size_t skseCount{};
        if (acFiles.empty())
            return false;

        for (const auto& file : acFiles)
        {
            if (file.SourcePath.empty() ||
                file.RelativePath.empty() ||
                file.Digest == 0 ||
                !PartyQuestReplicaFilePlanner::IsSafeRelativePath(file.RelativePath) ||
                file.RelativePath.has_parent_path() ||
                !IsInsideNamespace(acPaths.SavesDirectory, file.SourcePath))
            {
                return false;
            }

            switch (file.Kind)
            {
            case PartyQuestReplicaFileKind::SkyrimSave:
                ++essCount;
                break;
            case PartyQuestReplicaFileKind::SkseCosave:
                ++skseCount;
                break;
            case PartyQuestReplicaFileKind::ExternalSidecar:
                return false;
            }
        }

        return essCount == 1 && skseCount <= 1;
    }
    catch (...)
    {
        return false;
    }
}

bool ValidateSidecarFiles(
    const PartyQuestCoopSavePaths& acPaths,
    const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept
{
    try
    {
        const std::filesystem::path externalRoot =
            acPaths.SidecarsDirectory / "external";
        for (const auto& file : acFiles)
        {
            if (file.Kind != PartyQuestReplicaFileKind::ExternalSidecar ||
                file.SourcePath.empty() ||
                file.RelativePath.empty() ||
                file.Digest == 0 ||
                !PartyQuestReplicaFilePlanner::IsSafeRelativePath(file.RelativePath) ||
                !IsInsideNamespace(externalRoot, file.SourcePath))
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
} // namespace

uint64_t PartyQuestRuntimePreRepairCoreAuthorization::ComputeFilesFingerprint(
    const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept
{
    return FingerprintFiles(acCoreFiles);
}

PartyQuestRuntimePreRepairCoreAuthorization::
PartyQuestRuntimePreRepairCoreAuthorization(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept
    : m_transactionId(aTransactionId)
    , m_targetWorldRevision(aTargetWorldRevision)
    , m_filesFingerprint(ComputeFilesFingerprint(acCoreFiles))
    , m_fileCount(acCoreFiles.size())
    , m_verified(
          aTransactionId != 0 &&
          aTargetWorldRevision != 0 &&
          !acCoreFiles.empty() &&
          m_filesFingerprint != 0)
{
}

bool PartyQuestRuntimePreRepairCoreAuthorization::Matches(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) const noexcept
{
    return m_verified &&
        aTransactionId == m_transactionId &&
        aTargetWorldRevision == m_targetWorldRevision &&
        acCoreFiles.size() == m_fileCount &&
        ComputeFilesFingerprint(acCoreFiles) == m_filesFingerprint;
}

PartyQuestRuntimePreRepairCheckpointResult
PartyQuestRuntimePreRepairCheckpointAssembler::Complete(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestRuntimePreRepairCoreAuthorization& acCoreAuthorization,
    const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles,
    const PartyQuestCheckpointSidecarManifest& acSidecarManifest,
    const PartyQuestCheckpointSidecarMirrorResult& acSidecars) noexcept
{
    PartyQuestRuntimePreRepairCheckpointResult result;

    try
    {
        auto& session = aGuardedSession.GetRuntimeSession();
        const auto* active = session.GetCoordinator().GetActive();
        if (!active ||
            active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint ||
            !active->SaveGuardActive ||
            active->CheckpointCreated ||
            active->RuntimeMutationMayHaveOccurred ||
            active->TransactionId == 0 ||
            active->TargetWorldRevision == 0 ||
            active->SidecarManifestFingerprint == 0)
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidRuntimeState;
            return result;
        }

        auto& guard = aGuardedSession.GetSaveGuard();
        if (!guard.IsActive() ||
            guard.GetTransactionId() != active->TransactionId)
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::GuardMismatch;
            return result;
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                session.GetCampaignId(),
                session.GetPlayerProfileId()))
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidRuntimeState;
            return result;
        }

        const uint64_t sidecarManifestFingerprint =
            acSidecarManifest.ComputeFingerprint();
        if (sidecarManifestFingerprint == 0 ||
            sidecarManifestFingerprint != active->SidecarManifestFingerprint)
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::SidecarManifestMismatch;
            return result;
        }

        if (!acCoreAuthorization.Matches(
                active->TransactionId,
                active->TargetWorldRevision,
                acCoreFiles))
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidCoreAuthorization;
            return result;
        }

        if (!acSidecars.IsReady() ||
            !acSidecars.Authorization.Matches(
                acSidecarManifest,
                active->TransactionId,
                active->TargetWorldRevision,
                acSidecars.Files))
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidSidecarAuthorization;
            return result;
        }

        if (!ValidateCoreFiles(acPaths, acCoreFiles))
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidCoreFileSet;
            return result;
        }
        if (!ValidateSidecarFiles(acPaths, acSidecars.Files))
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidSidecarFileSet;
            return result;
        }

        std::vector<PartyQuestReplicaFileSpec> files;
        files.reserve(acCoreFiles.size() + acSidecars.Files.size());
        files.insert(files.end(), acCoreFiles.begin(), acCoreFiles.end());
        files.insert(files.end(), acSidecars.Files.begin(), acSidecars.Files.end());

        const auto plan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
            acPaths,
            PartyQuestCheckpointKind::PreRepair,
            active->TargetWorldRevision,
            files);
        result.PlanStatus = plan.Status;
        if (!plan.IsReady())
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidCheckpointPlan;
            return result;
        }

        const PartyQuestRuntimeCheckpointCoverageAuthorization coverage(
            active->TransactionId,
            active->TargetWorldRevision,
            plan);
        if (!coverage.IsVerified())
        {
            result.Status = PartyQuestRuntimePreRepairCheckpointStatus::InvalidCheckpointPlan;
            return result;
        }

        result.Checkpoint = aGuardedSession.EnsurePreRepairCheckpoint(
            acPaths,
            plan,
            coverage);
        result.Status = result.Checkpoint.IsReady()
            ? PartyQuestRuntimePreRepairCheckpointStatus::Ready
            : PartyQuestRuntimePreRepairCheckpointStatus::CheckpointFailed;
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestRuntimePreRepairCheckpointStatus::CheckpointFailed;
        return result;
    }
}
