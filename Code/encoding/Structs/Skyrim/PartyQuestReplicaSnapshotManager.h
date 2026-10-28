#pragma once

#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <cstdint>

class PartyQuestReplicaWorkspacePublicationCapability;

enum class PartyQuestReplicaSnapshotStatus : uint8_t
{
    Ready,
    AlreadyReady,
    InvalidIdentity,
    InvalidPlan,
    ExistingSnapshotConflict,
    CopyFailed,
    FileVerificationFailed,
    ManifestPersistenceFailed,
    ManifestRecoveryRequired,
    ManifestInvalid,
    RevisionCheckpointLimitExceeded,
    WorkspaceBusy,
    WorkspaceLeaseFailure
};

struct PartyQuestReplicaSnapshotResult
{
    PartyQuestReplicaSnapshotStatus Status{PartyQuestReplicaSnapshotStatus::InvalidPlan};
    PartyQuestReplicaExecutionStatus CopyStatus{PartyQuestReplicaExecutionStatus::InvalidPlan};
    PartyQuestReplicaManifestPersistenceStatus ManifestStatus{PartyQuestReplicaManifestPersistenceStatus::InvalidData};
    PartyQuestReplicaManifestVerificationStatus VerificationStatus{PartyQuestReplicaManifestVerificationStatus::InvalidManifest};
    bool AdoptedVerifiedFiles{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestReplicaSnapshotStatus::Ready ||
            Status == PartyQuestReplicaSnapshotStatus::AlreadyReady;
    }
};

/**
 * High-level transaction boundary for filesystem-only co-op snapshots.
 *
 * A multi-file copy is not considered usable until its process-crash-persistent
 * completion manifest is present and the final bytes verify against it. The
 * publication guarantee is PartyQuestPersistenceDurabilityPolicy's
 * CurrentLocalGuarantee; manifest completion alone is not proof of power-loss
 * stable storage. If a prior process copied the complete file set but died
 * before writing the manifest, the manager may adopt those exact verified bytes
 * and finish the manifest instead of overwriting them. Partial or conflicting
 * destinations fail closed.
 *
 * Revision-scoped publication is additionally serialized by the exact
 * campaign/player kernel-backed workspace lease. Standalone callers acquire
 * that lease for the duration of EnsureRevisionCheckpoint(); runtime callers
 * that already own it may pass a pinned publication capability instead of
 * attempting a second OS lock.
 *
 * No Skyrim save API, quest runtime mutation, or solo-save deletion is called.
 */
class PartyQuestReplicaSnapshotManager final
{
public:
    PartyQuestReplicaSnapshotManager(
        PartyQuestCoopSavePaths aPaths,
        PartyQuestCampaignId aCampaignId,
        PartyQuestPlayerProfileId aPlayerProfileId);

    [[nodiscard]] PartyQuestReplicaSnapshotResult EnsureImportedReplica(
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

    /** Legacy kind-root checkpoint publication retained for test/archive tooling. */
    [[nodiscard]] PartyQuestReplicaSnapshotResult EnsureCheckpoint(
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

    /**
     * Standalone immutable revision publication. Acquires the exact workspace
     * lease before admission and holds it through manifest verification.
     */
    [[nodiscard]] PartyQuestReplicaSnapshotResult EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

    /**
     * Owner-bound immutable revision publication. The supplied capability must
     * protect this exact campaign/player layout and pins the native lease state
     * for the full publication call.
     */
    [[nodiscard]] PartyQuestReplicaSnapshotResult EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan,
        const PartyQuestReplicaWorkspacePublicationCapability& acPublicationCapability) const noexcept;

    [[nodiscard]] PartyQuestReplicaSnapshotResult ValidateImportedReplica() const noexcept;

    [[nodiscard]] PartyQuestReplicaSnapshotResult ValidateCheckpoint(
        PartyQuestCheckpointKind aKind) const noexcept;

    [[nodiscard]] PartyQuestReplicaSnapshotResult ValidateRevisionCheckpoint(
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision) const noexcept;

private:
    [[nodiscard]] PartyQuestReplicaSnapshotResult Ensure(
        bool aCheckpoint,
        bool aRevisionScoped,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan,
        const PartyQuestReplicaWorkspacePublicationCapability* apPublicationCapability) const noexcept;

    [[nodiscard]] PartyQuestReplicaSnapshotResult Validate(
        bool aCheckpoint,
        bool aRevisionScoped,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision) const noexcept;

    PartyQuestCoopSavePaths m_paths;
    PartyQuestCampaignId m_campaignId;
    PartyQuestPlayerProfileId m_playerProfileId;
};
