#pragma once

#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

#include <cstddef>
#include <cstdint>

enum class PartyQuestReplicaWorkspaceRecoveryStatus : uint8_t
{
    NotAttempted,
    Clean,
    Quarantined,
    InvalidLease,
    InvalidLayout,
    InvalidNamespace,
    EntryLimitExceeded,
    CandidateUnsafe,
    DestinationConflict,
    IoError
};

struct PartyQuestReplicaWorkspaceRecoveryResult
{
    PartyQuestReplicaWorkspaceRecoveryStatus Status{
        PartyQuestReplicaWorkspaceRecoveryStatus::NotAttempted};
    size_t InspectedEntries{};
    size_t QuarantinedFiles{};

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Status == PartyQuestReplicaWorkspaceRecoveryStatus::Clean ||
            Status == PartyQuestReplicaWorkspaceRecoveryStatus::Quarantined;
    }
};

/**
 * Crash-convergent recovery for unpublished replica copy temporaries.
 *
 * Only exact .tpqtmp-<nonce>-<index> regular single-link files below the
 * deterministic saves/sidecars/checkpoints roots are moved. Durable .tmp
 * journals are never candidates. Recovery requires the exact live workspace
 * lease and preserves bytes under metadata/orphan_copy_quarantine instead of
 * deleting them.
 */
class PartyQuestReplicaWorkspaceRecovery final
{
public:
    static constexpr size_t MaxInspectedEntries = 100000;
    static constexpr size_t MaxCandidates = 4096;

    [[nodiscard]] static PartyQuestReplicaWorkspaceRecoveryResult
    QuarantineOrphanCopyTemporaries(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const PartyQuestReplicaWorkspaceLease& acLease) noexcept;
};
