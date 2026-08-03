#pragma once

#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <cstdint>

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
    ManifestInvalid
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
 * A multi-file copy is not considered usable until its durable manifest is
 * present and the final bytes verify against it. If a prior process copied the
 * complete file set but died before writing the manifest, the manager may adopt
 * those exact verified bytes and finish the manifest instead of overwriting
 * them. Partial or conflicting destinations fail closed.
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

    /** Production-oriented immutable revision checkpoint publication. */
    [[nodiscard]] PartyQuestReplicaSnapshotResult EnsureRevisionCheckpoint(
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

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
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

    [[nodiscard]] PartyQuestReplicaSnapshotResult Validate(
        bool aCheckpoint,
        bool aRevisionScoped,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision) const noexcept;

    PartyQuestCoopSavePaths m_paths;
    PartyQuestCampaignId m_campaignId;
    PartyQuestPlayerProfileId m_playerProfileId;
};
