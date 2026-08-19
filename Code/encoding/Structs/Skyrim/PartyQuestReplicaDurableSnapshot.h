#pragma once

#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <cstdint>

enum class PartyQuestReplicaDurableSnapshotStatus : uint8_t
{
    Promoted,
    UnsupportedPlatform,
    InvalidIdentity,
    InvalidRevision,
    ManifestUnavailable,
    ManifestInvalid,
    FileVerificationFailed,
    StableStorageFailure,
    ManifestPersistenceFailed
};

struct PartyQuestReplicaDurableSnapshotResult
{
    PartyQuestReplicaDurableSnapshotStatus Status{
        PartyQuestReplicaDurableSnapshotStatus::ManifestInvalid};
    PartyQuestReplicaManifestPersistenceStatus ManifestStatus{
        PartyQuestReplicaManifestPersistenceStatus::InvalidData};
    PartyQuestReplicaManifestVerificationStatus VerificationStatus{
        PartyQuestReplicaManifestVerificationStatus::InvalidManifest};

    [[nodiscard]] bool IsPromoted() const noexcept
    {
        return Status == PartyQuestReplicaDurableSnapshotStatus::Promoted;
    }
};

/**
 * Promotes one already-complete immutable revision checkpoint from the existing
 * process-crash publication contract to a PowerLossDurable ordering proof.
 *
 * This is intentionally a promotion step rather than a second copy executor.
 * The existing executor may leave exact unmanifested files after interruption;
 * before any caller can treat the checkpoint as power-loss authority, promotion
 * re-verifies the manifest and every final file, establishes stable directory
 * namespace, flushes every data file and its parent, re-verifies the exact file
 * content, and only then durably republishes the manifest as the authority marker.
 *
 * Linux/POSIX currently has the reviewed directory-tree primitive required for
 * this proof. Windows fails closed until durable directory creation/promotion is
 * proven independently; NTFS durable file rename alone is insufficient.
 */
class PartyQuestReplicaDurableSnapshot final
{
public:
    [[nodiscard]] static PartyQuestReplicaDurableSnapshotResult PromoteRevisionCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision) noexcept;
};
