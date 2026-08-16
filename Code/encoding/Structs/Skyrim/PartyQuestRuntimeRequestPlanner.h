#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <cstdint>
#include <optional>

enum class PartyQuestRuntimeRequestPlanStatus : uint8_t
{
    Planned,
    InvalidInput,
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
 * Builds one runtime request only from independently revalidated local evidence.
 *
 * A server canonical snapshot/transaction is data, not mutation authority. The
 * planner re-runs local admission, evaluates the exact quest compatibility
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
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        QuestSnapshot aCanonicalSnapshot,
        const PartyQuestSyncFacts& acLocalSyncFacts,
        const PartyQuestRuntimeCompatibilityRequirement& acCompatibilityRequirement,
        const PartyQuestRuntimeCompatibilityFacts& acLocalCompatibilityFacts,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest);
};
