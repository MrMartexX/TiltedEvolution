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
    DeadlineExceeded,
    IoError
};

/** Testable monotonic clock only; it grants no path or mutation authority. */
struct PartyQuestReplicaWorkspaceRecoveryHooks
{
    using Clock = uint64_t (*)(void*) noexcept;

    Clock MonotonicNow{};
    void* Context{};

    [[nodiscard]] uint64_t NowTicks() const noexcept;
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
 * deleting them. A monotonic deadline is checked between filesystem calls; it
 * cannot interrupt one synchronous OS call already in progress.
 */
class PartyQuestReplicaWorkspaceRecovery final
{
public:
    static constexpr size_t MaxInspectedEntries = 100000;
    static constexpr size_t MaxCandidates = 4096;
    static constexpr uint64_t MaxRecoveryNanoseconds =
        5ull * 60ull * 1000ull * 1000ull * 1000ull;

    [[nodiscard]] static PartyQuestReplicaWorkspaceRecoveryResult
    QuarantineOrphanCopyTemporaries(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const PartyQuestReplicaWorkspaceLease& acLease,
        PartyQuestReplicaWorkspaceRecoveryHooks aHooks = {}) noexcept;
};
