#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/PartyQuestRuntimeCanonicalInbox.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <cstdint>
#include <optional>

class PartyQuestRuntimeRequestPlannerTestAccess;

enum class PartyQuestRuntimeRequestPlanStatus : uint8_t
{
    Planned,
    InvalidInput,
    CanonicalProvenanceRejected,
    AdmissionRejected,
    CompatibilityRejected,
    UnsafePlan,
    InvalidRuntimeIdentity
};

struct PartyQuestRuntimeRequestPlanResult
{
    PartyQuestRuntimeRequestPlanStatus Status{
        PartyQuestRuntimeRequestPlanStatus::InvalidInput};
    PartyQuestAdmissionStatus AdmissionStatus{
        PartyQuestAdmissionStatus::BlockedLocalOnly};
    PartyQuestRuntimeCompatibilityStatus CompatibilityStatus{
        PartyQuestRuntimeCompatibilityStatus::UnknownQuest};
    PartyQuestRuntimeSafetyStatus SafetyStatus{
        PartyQuestRuntimeSafetyStatus::Blocked};
    std::optional<PartyQuestRuntimeApplyRequest> Request;

    [[nodiscard]] bool IsPlanned() const noexcept
    {
        return Status == PartyQuestRuntimeRequestPlanStatus::Planned &&
            Request.has_value();
    }
};

/**
 * Builds one runtime request only from exact canonical provenance plus
 * independently revalidated local evidence.
 *
 * Production callers cannot provide a naked transaction id/snapshot pair. They
 * must consume a one-shot PartyQuestRuntimeCanonicalAuthorization issued by the
 * canonical inbox for the exact current published replica head. The planner
 * then re-runs local admission, evaluates the exact quest compatibility
 * contract against fresh local facts, lets RuntimeSafetyPolicy issue the
 * otherwise-unforgeable mutation authorization, and finally asks
 * RuntimeApplyCoordinator to validate the complete transaction identity.
 *
 * The current safety milestone still emits DryRunOnly plans. This class does
 * not enqueue, dispatch, save, mutate Skyrim, or bind a process runtime owner.
 */
class PartyQuestRuntimeRequestPlanner final
{
public:
    [[nodiscard]] static PartyQuestRuntimeRequestPlanResult Build(
        PartyQuestRuntimeCanonicalCandidate aCandidate,
        PartyQuestRuntimeCanonicalAuthorization&& aCanonicalAuthorization,
        const PartyQuestSyncFacts& acLocalSyncFacts,
        const PartyQuestRuntimeCompatibilityRequirement& acCompatibilityRequirement,
        const PartyQuestRuntimeCompatibilityFacts& acLocalCompatibilityFacts,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest);

private:
    /**
     * Raw-input seam for deterministic planner unit tests only. Production code
     * has no friend access and must enter through capability-backed Build().
     */
    [[nodiscard]] static PartyQuestRuntimeRequestPlanResult BuildDiagnostic(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        QuestSnapshot aCanonicalSnapshot,
        const PartyQuestSyncFacts& acLocalSyncFacts,
        const PartyQuestRuntimeCompatibilityRequirement& acCompatibilityRequirement,
        const PartyQuestRuntimeCompatibilityFacts& acLocalCompatibilityFacts,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest);

    // Defined only in Code/tests; no production factory/API exists.
    friend class PartyQuestRuntimeRequestPlannerTestAccess;
};
