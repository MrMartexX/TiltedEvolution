#pragma once

#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

class PartyQuestRuntimePreRepairCheckpointAssembler;

enum class PartyQuestRuntimeCheckpointStatus : uint8_t
{
    Ready,
    AlreadyReady,
    InvalidIdentity,
    InvalidLayout,
    InvalidRuntimeState,
    InvalidCheckpointPlan,
    InvalidCoverageAuthorization,
    SnapshotFailed,
    RuntimeStatePersistenceFailed
};

/**
 * Unforgeable proof that the complete pre-repair file set was assembled through
 * the coverage gate (controlled core save + required sidecar mirror coverage).
 *
 * The token is bound to one TransactionId, one world revision and the exact
 * deterministic copy plan. A default token is intentionally unverified.
 */
class PartyQuestRuntimeCheckpointCoverageAuthorization final
{
public:
    PartyQuestRuntimeCheckpointCoverageAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }
    [[nodiscard]] uint64_t GetTransactionId() const noexcept { return m_transactionId; }
    [[nodiscard]] uint64_t GetTargetWorldRevision() const noexcept
    {
        return m_targetWorldRevision;
    }

    [[nodiscard]] bool Matches(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) const noexcept;

private:
    PartyQuestRuntimeCheckpointCoverageAuthorization(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    [[nodiscard]] static uint64_t ComputePlanFingerprint(
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    uint64_t m_planFingerprint{};
    size_t m_operationCount{};
    bool m_verified{};

    friend class PartyQuestRuntimePreRepairCheckpointAssembler;
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
 *  2. require a coverage authorization bound to this exact checkpoint plan;
 *  3. publish/verify an immutable PreRepair checkpoint at exactly
 *     Active.TargetWorldRevision;
 *  4. only after the checkpoint manifest is durable call MarkCheckpointCreated();
 *  5. only after the runtime recovery journal persists that transition may the
 *     transaction become ReadyToApply.
 *
 * Coverage authorization can only be issued by
 * PartyQuestRuntimePreRepairCheckpointAssembler after the controlled core save
 * and all required external sidecar capabilities have been validated.
 *
 * If checkpoint publication succeeds but runtime-state persistence fails, a
 * retry with the same verified plan may adopt the immutable checkpoint and
 * attempt only the runtime-state transition.
 *
 * A caller must treat every non-ready result as a hard mutation barrier and must
 * not call ArmRuntimeMutation() through a bypass path.
 * No Skyrim/Papyrus/save hook is invoked here.
 */
class PartyQuestRuntimeCheckpointCoordinator final
{
public:
    [[nodiscard]] static PartyQuestRuntimeCheckpointResult EnsurePreRepairCheckpoint(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acCheckpointPlan,
        const PartyQuestRuntimeCheckpointCoverageAuthorization& acCoverage) noexcept;
};
