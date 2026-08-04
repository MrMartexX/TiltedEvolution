#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class PartyQuestSkyrimPreRepairSave;
class PartyQuestRuntimePreRepairCheckpointTestAccess;

/**
 * Unforgeable proof that an exact .ess/.skse core file set came from the
 * controlled Skyrim pre-repair save capture path for one transaction/revision.
 */
class PartyQuestRuntimePreRepairCoreAuthorization final
{
public:
    PartyQuestRuntimePreRepairCoreAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }

    [[nodiscard]] bool Matches(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) const noexcept;

private:
    PartyQuestRuntimePreRepairCoreAuthorization(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept;

    [[nodiscard]] static uint64_t ComputeFilesFingerprint(
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept;

    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    uint64_t m_filesFingerprint{};
    size_t m_fileCount{};
    bool m_verified{};

    friend class PartyQuestSkyrimPreRepairSave;
    // Defined only in Code/tests; no production implementation/API exists.
    friend class PartyQuestRuntimePreRepairCheckpointTestAccess;
};

enum class PartyQuestRuntimePreRepairCheckpointStatus : uint8_t
{
    Ready,
    InvalidRuntimeState,
    GuardMismatch,
    InvalidCoreAuthorization,
    InvalidSidecarAuthorization,
    InvalidCoreFileSet,
    InvalidSidecarFileSet,
    InvalidCheckpointPlan,
    CheckpointFailed
};

struct PartyQuestRuntimePreRepairCheckpointResult
{
    PartyQuestRuntimePreRepairCheckpointStatus Status{
        PartyQuestRuntimePreRepairCheckpointStatus::InvalidRuntimeState};
    PartyQuestReplicaCopyPlanStatus PlanStatus{
        PartyQuestReplicaCopyPlanStatus::InvalidSource};
    PartyQuestRuntimeCheckpointResult Checkpoint;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestRuntimePreRepairCheckpointStatus::Ready &&
            Checkpoint.IsReady();
    }
};

/**
 * Single full-coverage gate for a runtime PreRepair checkpoint.
 *
 * It accepts only:
 *  - a controlled core-save authorization issued by the Skyrim capture helper;
 *  - a sidecar coverage authorization issued by the exact manifest collector;
 *  - the active guarded runtime transaction.
 *
 * It then validates source namespaces, combines the file sets, builds the exact
 * immutable Revision_N plan and issues the private coverage authorization that
 * PartyQuestRuntimeCheckpointCoordinator requires before it can publish
 * CheckpointCreated.
 */
class PartyQuestRuntimePreRepairCheckpointAssembler final
{
public:
    [[nodiscard]] static PartyQuestRuntimePreRepairCheckpointResult Complete(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestRuntimePreRepairCoreAuthorization& acCoreAuthorization,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest,
        const PartyQuestCheckpointSidecarMirrorResult& acSidecars) noexcept;
};
