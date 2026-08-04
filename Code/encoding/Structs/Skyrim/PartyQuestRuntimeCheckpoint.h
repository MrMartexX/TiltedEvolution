#pragma once

#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <cstdint>
#include <filesystem>

enum class PartyQuestRuntimeCheckpointStatus : uint8_t
{
    Ready,
    AlreadyReady,
    InvalidIdentity,
    InvalidLayout,
    InvalidRuntimeState,
    InvalidCheckpointPlan,
    SnapshotFailed,
    RuntimeStatePersistenceFailed
};

struct PartyQuestRuntimeCheckpointResult
{
    PartyQuestRuntimeCheckpointStatus Status{
        PartyQuestRuntimeCheckpointStatus::InvalidRuntimeState};
    PartyQuestReplicaSnapshotStatus SnapshotStatus{
        PartyQuestReplicaSnapshotStatus::InvalidPlan};
    PartyQuestRuntimeDurableTransitionStatus RuntimeTransition{
        PartyQuestRuntimeDurableTransitionStatus::InvalidState};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    std::filesystem::path ManifestPath;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestRuntimeCheckpointStatus::Ready ||
            Status == PartyQuestRuntimeCheckpointStatus::AlreadyReady;
    }
};

/**
 * Durable gate between a runtime repair transaction and its pre-mutation
 * checkpoint.
 *
 * PartyQuestRuntimeApplySession intentionally knows only that a checkpoint was
 * created; it does not own filesystem snapshot logic. This coordinator bridges
 * the two control planes without weakening either one:
 *
 *  1. require an AwaitingCheckpoint runtime transaction with the save guard held;
 *  2. publish/verify an immutable PreRepair checkpoint at exactly
 *     Active.TargetWorldRevision;
 *  3. only after the checkpoint manifest is durable call MarkCheckpointCreated();
 *  4. only after the runtime recovery journal persists that transition may the
 *     transaction become ReadyToApply.
 *
 * If step 3/4 fails after checkpoint publication, a retry adopts the already
 * verified immutable checkpoint and attempts only the runtime-state transition.
 * No Skyrim/Papyrus/save hook is invoked here.
 */
class PartyQuestRuntimeCheckpointCoordinator final
{
public:
    [[nodiscard]] static PartyQuestRuntimeCheckpointResult EnsurePreRepairCheckpoint(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acCheckpointPlan) noexcept;
};
