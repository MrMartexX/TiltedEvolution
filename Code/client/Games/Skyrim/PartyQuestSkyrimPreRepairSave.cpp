#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimPreRepairSave.h>
#include <PartyQuestSkyrimSavePathScope.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestPreRepairCaptureAttemptPolicy.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <system_error>

namespace
{
constexpr size_t kSaveNameAttempts = 32;

enum class CandidatePathState : uint8_t
{
    Missing,
    RegularFile,
    Invalid
};

bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

uint64_t NextSaveNonce() noexcept
{
    static std::atomic<uint64_t> sequence{1};
    const uint64_t counter = sequence.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    uint64_t value = now ^ (counter * 0x9E3779B97F4A7C15ull);
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value != 0 ? value : counter;
}

CandidatePathState InspectCandidatePath(
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found || IsMissingError(ec))
            return CandidatePathState::Missing;
        if (ec || std::filesystem::is_symlink(status))
            return CandidatePathState::Invalid;
        return std::filesystem::is_regular_file(status)
            ? CandidatePathState::RegularFile
            : CandidatePathState::Invalid;
    }
    catch (...)
    {
        return CandidatePathState::Invalid;
    }
}

bool IsExistingConfinedSaveDirectory(
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        if (!acPaths.Root.is_absolute() ||
            !acPaths.PlayerDirectory.is_absolute() ||
            !acPaths.SavesDirectory.is_absolute())
        {
            return false;
        }

        std::error_code ec;
        const auto playerStatus = std::filesystem::symlink_status(
            acPaths.PlayerDirectory,
            ec);
        if (ec || std::filesystem::is_symlink(playerStatus) ||
            !std::filesystem::is_directory(playerStatus))
        {
            return false;
        }

        ec.clear();
        const auto savesStatus = std::filesystem::symlink_status(
            acPaths.SavesDirectory,
            ec);
        if (ec || std::filesystem::is_symlink(savesStatus) ||
            !std::filesystem::is_directory(savesStatus))
        {
            return false;
        }

        ec.clear();
        const auto resolvedPlayer = std::filesystem::weakly_canonical(
            acPaths.PlayerDirectory,
            ec);
        if (ec || resolvedPlayer.empty())
            return false;

        ec.clear();
        const auto resolvedSaves = std::filesystem::weakly_canonical(
            acPaths.SavesDirectory,
            ec);
        if (ec || resolvedSaves.empty())
            return false;

        return PartyQuestReplicaFilePlanner::IsContainedBy(
            resolvedPlayer,
            resolvedSaves);
    }
    catch (...)
    {
        return false;
    }
}

bool AppendInspectedCoreFile(
    PartyQuestSkyrimPreRepairSaveResult& aResult,
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acPath) noexcept
{
    const auto file = PartyQuestReplicaFileExecutor::InspectSource(
        aKind,
        acPath,
        acPath.filename());
    if (!file || file->Size == 0 || file->Digest == 0)
        return false;

    aResult.CoreFiles.push_back(*file);
    return true;
}
} // namespace

std::string PartyQuestSkyrimPreRepairSave::FormatSaveName(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    uint64_t aAttemptNonce)
{
    if (aTransactionId == 0 || aTargetWorldRevision == 0 || aAttemptNonce == 0)
        return {};

    std::array<char, 96> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX",
        static_cast<unsigned long long>(aTransactionId),
        static_cast<unsigned long long>(aTargetWorldRevision),
        static_cast<unsigned long long>(aAttemptNonce));
    return buffer.data();
}

PartyQuestSkyrimPreRepairSaveResult
PartyQuestSkyrimPreRepairSave::CaptureCoreSource(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    PartyQuestSkyrimPreRepairSaveResult result;

    try
    {
        auto& runtimeSession = aGuardedSession.GetRuntimeSession();
        const auto* active = runtimeSession.GetCoordinator().GetActive();
        if (!active ||
            active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint ||
            !active->SaveGuardActive ||
            active->CheckpointCreated ||
            active->RuntimeMutationMayHaveOccurred ||
            active->TransactionId == 0 ||
            active->TargetWorldRevision == 0)
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::InvalidRuntimeState;
            return result;
        }

        result.TransactionId = active->TransactionId;
        result.TargetWorldRevision = active->TargetWorldRevision;

        auto& guard = aGuardedSession.GetSaveGuard();
        auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
        if (&guard != &processGuard ||
            !guard.IsActive() ||
            guard.GetTransactionId() != active->TransactionId)
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::GuardMismatch;
            return result;
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                runtimeSession.GetCampaignId(),
                runtimeSession.GetPlayerProfileId()))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::InvalidIdentity;
            return result;
        }

        if (acPaths.Root.filename() != "CoopCampaigns" ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(
                acPaths.PlayerDirectory,
                acPaths.SavesDirectory))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::InvalidLayout;
            return result;
        }

        if (!IsExistingConfinedSaveDirectory(acPaths))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SaveDirectoryUnavailable;
            return result;
        }

        const auto retentionDecision =
            PartyQuestPreRepairCaptureAttemptPolicy::ReclaimHistoricalAttempts(
                acPaths,
                active->TransactionId,
                active->TargetWorldRevision);
        if (!retentionDecision.IsReady())
        {
            result.Status =
                PartyQuestSkyrimPreRepairSaveStatus::CaptureAttemptCleanupFailed;
            return result;
        }

        const auto attemptDecision =
            PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
                acPaths,
                active->TransactionId,
                active->TargetWorldRevision);
        if (!attemptDecision.IsReady())
        {
            result.Status =
                attemptDecision.Status ==
                    PartyQuestPreRepairCaptureAttemptStatus::AttemptLimitExceeded
                ? PartyQuestSkyrimPreRepairSaveStatus::CaptureAttemptLimitExceeded
                : PartyQuestSkyrimPreRepairSaveStatus::CaptureAttemptInspectionFailed;
            return result;
        }

        bool selected{};
        for (size_t attempt = 0; attempt < kSaveNameAttempts; ++attempt)
        {
            const uint64_t nonce = NextSaveNonce();
            const std::string saveName = FormatSaveName(
                active->TransactionId,
                active->TargetWorldRevision,
                nonce);
            if (saveName.empty())
                continue;

            const auto mainSavePath = acPaths.SavesDirectory / (saveName + ".ess");
            const auto skseCosavePath = acPaths.SavesDirectory / (saveName + ".skse");
            if (InspectCandidatePath(mainSavePath) != CandidatePathState::Missing ||
                InspectCandidatePath(skseCosavePath) != CandidatePathState::Missing)
            {
                continue;
            }

            result.AttemptNonce = nonce;
            result.SaveName = saveName;
            result.MainSavePath = mainSavePath;
            result.SkseCosavePath = skseCosavePath;
            selected = true;
            break;
        }

        if (!selected)
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SaveNameExhausted;
            return result;
        }

        {
            PartyQuestSkyrimSavePathScope pathScope(
                runtimeSession.GetCampaignId(),
                runtimeSession.GetPlayerProfileId());
            if (!pathScope.IsArmed())
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::SavePathOverrideFailed;
                return result;
            }

            PartyQuestControlledSaveScope controlledSave(
                guard,
                active->TransactionId);
            if (!controlledSave.IsArmed())
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::ControlledSaveAuthorizationFailed;
                return result;
            }

            // Recheck immediately before entering Skyrim. A conflicting file
            // appearing after candidate selection is never treated as a valid
            // retry source.
            if (InspectCandidatePath(result.MainSavePath) != CandidatePathState::Missing ||
                InspectCandidatePath(result.SkseCosavePath) != CandidatePathState::Missing)
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::ExistingSourceConflict;
                return result;
            }

            BGSSaveLoadManager* pSaveLoadManager = BGSSaveLoadManager::Get();
            if (!pSaveLoadManager)
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::SaveManagerUnavailable;
                return result;
            }

            if (!pSaveLoadManager->SaveByName(result.SaveName.c_str()))
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::EngineSaveFailed;
                return result;
            }
        }

        const CandidatePathState mainState = InspectCandidatePath(result.MainSavePath);
        if (mainState == CandidatePathState::Missing)
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::MainSaveMissing;
            return result;
        }
        if (mainState != CandidatePathState::RegularFile ||
            !AppendInspectedCoreFile(
                result,
                PartyQuestReplicaFileKind::SkyrimSave,
                result.MainSavePath))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
            result.CoreFiles.clear();
            return result;
        }

        const CandidatePathState cosaveState = InspectCandidatePath(result.SkseCosavePath);
        if (cosaveState == CandidatePathState::Invalid)
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
            result.CoreFiles.clear();
            return result;
        }
        if (cosaveState == CandidatePathState::RegularFile)
        {
            if (!AppendInspectedCoreFile(
                    result,
                    PartyQuestReplicaFileKind::SkseCosave,
                    result.SkseCosavePath))
            {
                result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
                result.CoreFiles.clear();
                return result;
            }
            result.IncludedSkseCosave = true;
        }

        result.Authorization = PartyQuestRuntimePreRepairCoreAuthorization(
            result.TransactionId,
            result.TargetWorldRevision,
            result.CoreFiles);
        if (!result.Authorization.IsVerified())
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
            result.CoreFiles.clear();
            return result;
        }

        result.Status = PartyQuestSkyrimPreRepairSaveStatus::Ready;
        spdlog::info(
            "PartyQuest captured isolated PreRepair core save source: transaction={} revision={} save={} skse={}",
            result.TransactionId,
            result.TargetWorldRevision,
            result.SaveName,
            result.IncludedSkseCosave);
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
        result.CoreFiles.clear();
        return result;
    }
}

PartyQuestSkyrimPreRepairSaveResult
PartyQuestSkyrimPreRepairSave::CaptureCoreSource(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept
{
    PartyQuestSkyrimPreRepairSaveResult result;
    result.CaptureEpochId = acEpoch.GetEpochId();
    result.TransactionId = acEpoch.GetTransactionId();
    result.TargetWorldRevision = acEpoch.GetTargetWorldRevision();

    try
    {
        if (!acEpoch.IsVerified() ||
            !aGuardedSession.IsCheckpointCaptureEpochActive(acEpoch))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::CaptureEpochMismatch;
            return result;
        }

        result = CaptureCoreSource(aGuardedSession, acPaths);
        result.CaptureEpochId = acEpoch.GetEpochId();
        if (!result.IsReady())
            return result;

        // SaveByName may execute nested engine work. Revalidate the exact epoch
        // after returning instead of assuming the pre-save control-plane state
        // is still current. A changed/aborted epoch leaves only confined orphan
        // files and never yields a production-usable core capability.
        if (result.TransactionId != acEpoch.GetTransactionId() ||
            result.TargetWorldRevision != acEpoch.GetTargetWorldRevision() ||
            !aGuardedSession.IsCheckpointCaptureEpochActive(acEpoch))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::CaptureEpochMismatch;
            result.Authorization = {};
            return result;
        }

        result.Authorization = PartyQuestRuntimePreRepairCoreAuthorization(
            acEpoch,
            result.CoreFiles);
        if (!result.Authorization.IsVerified() ||
            !result.Authorization.Matches(acEpoch, result.CoreFiles))
        {
            result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
            result.Authorization = {};
            return result;
        }

        result.Status = PartyQuestSkyrimPreRepairSaveStatus::Ready;
        spdlog::info(
            "PartyQuest bound PreRepair core save source to capture epoch: epoch={} transaction={} revision={} save={} skse={}",
            result.CaptureEpochId,
            result.TransactionId,
            result.TargetWorldRevision,
            result.SaveName,
            result.IncludedSkseCosave);
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestSkyrimPreRepairSaveStatus::SourceInspectionFailed;
        result.Authorization = {};
        return result;
    }
}
