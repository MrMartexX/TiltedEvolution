#include <Structs/Skyrim/PartyQuestRuntimeRequestPlanner.h>

#include <utility>

PartyQuestRuntimeRequestPlanResult PartyQuestRuntimeRequestPlanner::Build(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    QuestSnapshot aCanonicalSnapshot,
    const PartyQuestSyncFacts& acLocalSyncFacts,
    const PartyQuestRuntimeCompatibilityRequirement& acCompatibilityRequirement,
    const PartyQuestRuntimeCompatibilityFacts& acLocalCompatibilityFacts,
    const PartyQuestCheckpointSidecarManifest& acSidecarManifest)
{
    PartyQuestRuntimeRequestPlanResult result;

    aCanonicalSnapshot.Canonicalize();
    if (aTransactionId == 0 ||
        aTargetWorldRevision == 0 ||
        !aCanonicalSnapshot.QuestId ||
        aCanonicalSnapshot.Revision == 0 ||
        acCompatibilityRequirement.QuestId != aCanonicalSnapshot.QuestId)
    {
        return result;
    }

    const uint64_t sidecarManifestFingerprint =
        acSidecarManifest.ComputeFingerprint();
    if (sidecarManifestFingerprint == 0)
        return result;

    const auto admission = PartyQuestAdmissionPolicy::Evaluate(
        aCanonicalSnapshot.QuestId,
        acLocalSyncFacts);
    result.AdmissionStatus = admission.Status;
    if (!admission.IsAdmitted())
    {
        result.Status = PartyQuestRuntimeRequestPlanStatus::AdmissionRejected;
        return result;
    }

    const auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        acCompatibilityRequirement,
        acLocalCompatibilityFacts);
    result.CompatibilityStatus = compatibility.Status;
    if (!compatibility.IsAuthorized())
    {
        result.Status = PartyQuestRuntimeRequestPlanStatus::CompatibilityRejected;
        return result;
    }

    PartyQuestApplyPlan plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        admission,
        aCanonicalSnapshot,
        compatibility.SafetyProfile);
    result.SafetyStatus = plan.Safety.Status;

    if (plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !plan.DryRunOnly ||
        !plan.MutationAuthorization.IsVerified())
    {
        result.Status = PartyQuestRuntimeRequestPlanStatus::UnsafePlan;
        return result;
    }

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aTargetWorldRevision;
    request.SidecarManifestFingerprint = sidecarManifestFingerprint;
    request.CanonicalSnapshot = std::move(aCanonicalSnapshot);
    request.Plan = std::move(plan);

    if (!PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request))
    {
        result.Status = PartyQuestRuntimeRequestPlanStatus::InvalidRuntimeIdentity;
        return result;
    }

    result.Status = PartyQuestRuntimeRequestPlanStatus::Planned;
    result.Request = std::move(request);
    return result;
}
