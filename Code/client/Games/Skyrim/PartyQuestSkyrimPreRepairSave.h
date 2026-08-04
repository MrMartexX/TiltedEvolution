#pragma once

#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class PartyQuestSkyrimPreRepairSaveStatus : uint8_t
{
    Ready,
    ReusedExistingSource,
    InvalidRuntimeState,
    GuardMismatch,
    InvalidIdentity,
    InvalidLayout,
    SaveDirectoryUnavailable,
    ExistingSourceConflict,
    SavePathOverrideFailed,
    ControlledSaveAuthorizationFailed,
    SaveManagerUnavailable,
    EngineSaveFailed,
    MainSaveMissing,
    SourceInspectionFailed
};

struct PartyQuestSkyrimPreRepairSaveResult
{
    PartyQuestSkyrimPreRepairSaveStatus Status{
        PartyQuestSkyrimPreRepairSaveStatus::InvalidRuntimeState};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    std::string SaveName;
    std::filesystem::path MainSavePath;
    std::filesystem::path SkseCosavePath;
    std::vector<PartyQuestReplicaFileSpec> CoreFiles;
    bool IncludedSkseCosave{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestSkyrimPreRepairSaveStatus::Ready ||
            Status == PartyQuestSkyrimPreRepairSaveStatus::ReusedExistingSource;
    }
};

/**
 * Creates or re-adopts the engine-generated core save source for a PreRepair
 * checkpoint while the runtime transaction owns the physical process save
 * guard.
 *
 * This is deliberately NOT the durable checkpoint gate. It captures only the
 * Skyrim .ess and an SKSE .skse co-save when one is actually produced. Future
 * compatibility/sidecar policy must append every required external sidecar,
 * build the immutable revision checkpoint plan, and only then call
 * PartyQuestRuntimeGuardedSession::EnsurePreRepairCheckpoint().
 *
 * No quest/Papyrus mutation is dispatched here.
 */
class PartyQuestSkyrimPreRepairSave final
{
public:
    /**
     * Must be invoked from Skyrim's normal game thread/save-safe runtime
     * context. The helper does not marshal work to another thread.
     */
    [[nodiscard]] static PartyQuestSkyrimPreRepairSaveResult CaptureCoreSource(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    [[nodiscard]] static std::string FormatSaveName(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision);
};
