#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeRequestPlanner.h>
#include <Structs/Skyrim/PartyQuestRepair.h>

#include <cstdint>
#include <functional>
#include <optional>

enum class PartyQuestRuntimeProcessRequestStatus : uint8_t
{
    PlannedDryRun,
    InvalidInput,
    ProcessOwnerUnavailable,
    RecoveryBlocked,
    CampaignMismatch,
    CandidateUnavailable,
    RequirementUnavailable,
    EvidenceUnavailable,
    RuntimeGenerationChanged,
    CanonicalAuthorizationUnavailable,
    PlannerRejected,
    PostPlanRevalidationFailed
};

/**
 * Fresh local evidence sampled for one exact canonical candidate while the
 * shared process runtime generation is pinned.
 *
 * This object carries data only. The production gate obtains it synchronously
 * from an observer supplied by the concrete Skyrim integration; it is never
 * persisted or accepted as reusable authority.
 */
struct PartyQuestRuntimeProcessPlanningEvidence
{
    PartyQuestSyncFacts SyncFacts;
    PartyQuestRuntimeCompatibilityFacts CompatibilityFacts;
    PartyQuestCheckpointSidecarManifest SidecarManifest;
};

struct PartyQuestRuntimeProcessRequestResult
{
    PartyQuestRuntimeProcessRequestStatus Status{
        PartyQuestRuntimeProcessRequestStatus::InvalidInput};
    PartyQuestRuntimeRequestPlanResult Planner;
    std::optional<PartyQuestRuntimeApplyRequest> Request;
    uint64_t RuntimeGeneration{};

    [[nodiscard]] bool IsPlanned() const noexcept
    {
        return Status == PartyQuestRuntimeProcessRequestStatus::PlannedDryRun &&
            Request.has_value() && Request->Plan.DryRunOnly;
    }
};

/**
 * Final process-owned bridge from canonical protocol evidence to a runtime
 * request. It does not mutate Skyrim and does not begin a durable runtime
 * transaction.
 *
 * The gate requires the shared PartyQuestRuntimeSessionOwner to be bound to the
 * exact campaign, pins PartyQuestRuntimeGenerationFence for the complete fresh
 * evidence sample + one-shot canonical authorization + planner call, and then
 * revalidates the process owner, generation, canonical inbox and published
 * replica head before returning the request. Any lifecycle/mod-map invalidation
 * or canonical advance drops the result.
 *
 * Compatibility requirements come from an immutable reviewed manifest. Fresh
 * local facts and the exact sidecar requirement contract come from one
 * synchronous observer. Callback shape is not itself authority: all returned
 * data still has to pass PartyQuestRuntimeRequestPlanner exact matching and the
 * gate's process/canonical revalidation. The current safety milestone accepts
 * only DryRunOnly planner output; non-dry requests are rejected here until the
 * later mutation milestone explicitly changes this contract.
 */
class PartyQuestRuntimeProcessRequestGate final
{
public:
    using EvidenceObserver = std::function<std::optional<PartyQuestRuntimeProcessPlanningEvidence>(
        const PartyQuestRuntimeCanonicalCandidate&)>;

    [[nodiscard]] static PartyQuestRuntimeProcessRequestResult PlanLatest(
        PartyQuestRuntimeCanonicalInbox& aInbox,
        const PartyQuestReplica& acPublishedReplica,
        const GameId& acQuestId,
        const PartyQuestRuntimeCompatibilityManifest& acCompatibilityManifest,
        const EvidenceObserver& acEvidenceObserver) noexcept;
};
