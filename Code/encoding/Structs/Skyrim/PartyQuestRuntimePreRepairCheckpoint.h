#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecarMirror.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class PartyQuestSkyrimPreRepairSave;
class PartyQuestRuntimePreRepairCheckpointTestAccess;

/**
 * Encapsulation-backed proof that an exact .ess/.skse core file set belongs to
 * one transaction/revision and, for the production checkpoint gate, one active
 * logical capture epoch.
 *
 * The epochless constructor is retained only for isolated diagnostics/tests;
 * PartyQuestRuntimePreRepairCheckpointAssembler never accepts it.
 */
class PartyQuestRuntimePreRepairCoreAuthorization final
{
public:
    PartyQuestRuntimePreRepairCoreAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }
    [[nodiscard]] uint64_t GetCaptureEpochId() const noexcept { return m_captureEpochId; }

    /** Legacy diagnostic match; does not prove temporal coherence. */
    [[nodiscard]] bool Matches(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) const noexcept;

    /** Production gate: exact active capture epoch + exact core file set. */
    [[nodiscard]] bool Matches(
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) const noexcept;

private:
    PartyQuestRuntimePreRepairCoreAuthorization(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept;

    PartyQuestRuntimePreRepairCoreAuthorization(
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept;

    [[nodiscard]] static uint64_t ComputeFilesFingerprint(
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept;

    uint64_t m_captureEpochId{};
    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    uint64_t m_sidecarManifestFingerprint{};
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
    InvalidCaptureEpoch,
    SidecarManifestMismatch,
    InvalidCoreAuthorization,
    InvalidSidecarAuthorization,
    InvalidCoreFileSet,
    InvalidSidecarFileSet,
    InvalidCheckpointPlan,
    CheckpointFailed,
    CaptureEpochCloseFailed
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
 * Production publication requires all evidence to belong to the same active
 * PartyQuestCheckpointCaptureEpoch:
 *
 *  - controlled core-save authorization;
 *  - exact sidecar manifest fingerprint;
 *  - sidecar mirror authorization;
 *  - guarded AwaitingCheckpoint runtime transaction.
 *
 * Only after the immutable Revision_N checkpoint and durable ReadyToApply
 * transition succeed is the epoch closed. Any mismatch/failure leaves the epoch
 * active so callers must explicitly retry or abort/recapture; runtime mutation
 * stays fenced by PartyQuestRuntimeGuardedSession in either case.
 */
class PartyQuestRuntimePreRepairCheckpointAssembler final
{
public:
    [[nodiscard]] static PartyQuestRuntimePreRepairCheckpointResult Complete(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const PartyQuestRuntimePreRepairCoreAuthorization& acCoreAuthorization,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest,
        const PartyQuestCheckpointSidecarMirrorResult& acSidecars) noexcept;
};
