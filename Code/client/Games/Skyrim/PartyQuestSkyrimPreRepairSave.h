#pragma once

#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class PartyQuestSkyrimPreRepairSaveStatus : uint8_t
{
    Ready,
    InvalidRuntimeState,
    GuardMismatch,
    CaptureEpochMismatch,
    InvalidIdentity,
    InvalidLayout,
    SaveDirectoryUnavailable,
    SaveNameExhausted,
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
    uint64_t CaptureEpochId{};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t AttemptNonce{};
    std::string SaveName;
    std::filesystem::path MainSavePath;
    std::filesystem::path SkseCosavePath;
    std::vector<PartyQuestReplicaFileSpec> CoreFiles;
    PartyQuestRuntimePreRepairCoreAuthorization Authorization;
    bool IncludedSkseCosave{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestSkyrimPreRepairSaveStatus::Ready &&
            Authorization.IsVerified();
    }
};

/**
 * Creates a new engine-generated core save source for a PreRepair checkpoint
 * while the runtime transaction owns the physical process save guard.
 *
 * Every capture attempt uses a unique name. Existing files are never trusted as
 * proof that a prior engine save completed: a crash/failure may have left a
 * partially written .ess. Orphan attempts remain confined to the co-op replica
 * and can be handled by later reference-aware retention/cleanup policy.
 *
 * The production overload binds the resulting .ess/.skse authorization to the
 * exact active CheckpointCaptureEpoch and verifies that the same epoch is still
 * active after Skyrim returns from SaveByName. If the epoch changes while the
 * engine save is in flight, the files remain confined orphan evidence but no
 * production-usable authorization is issued.
 *
 * This is deliberately NOT the durable checkpoint gate. The full assembler
 * must additionally validate every required external sidecar capability from
 * the same epoch before CheckpointCreated can be published.
 *
 * No quest/Papyrus mutation is dispatched here.
 */
class PartyQuestSkyrimPreRepairSave final
{
public:
    /**
     * Legacy diagnostic capture. It produces an epochless core authorization;
     * PartyQuestRuntimePreRepairCheckpointAssembler intentionally rejects that
     * authorization on the production publication path.
     *
     * Must be invoked from Skyrim's normal game thread/save-safe runtime
     * context. The helper does not marshal work to another thread.
     */
    [[nodiscard]] static PartyQuestSkyrimPreRepairSaveResult CaptureCoreSource(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    /**
     * Production capture for INV-SNAPSHOT-001. The epoch is checked both before
     * and after the engine save and the returned core capability is bound to its
     * exact epoch identity.
     */
    [[nodiscard]] static PartyQuestSkyrimPreRepairSaveResult CaptureCoreSource(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCheckpointCaptureEpoch& acEpoch) noexcept;

    [[nodiscard]] static std::string FormatSaveName(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        uint64_t aAttemptNonce);
};
