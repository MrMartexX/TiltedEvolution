#include <Structs/Skyrim/PartyQuestReconciliation.h>
#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <algorithm>

namespace
{
bool ContainsStage(const std::vector<uint16_t>& acStages, uint16_t aStage) noexcept
{
    return std::ranges::find(acStages, aStage) != acStages.end();
}
} // namespace

PartyQuestReconciliationResult PartyQuestReconciliationPlanner::Plan(const PartyQuestReconciliationInput& acInput) noexcept
{
    PartyQuestReconciliationResult result;

    if (!acInput.CampaignId.IsValid())
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "invalid-campaign-id";
        return result;
    }

    if (acInput.TransactionId == 0 || acInput.TargetWorldRevision == 0)
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "invalid-transaction-or-revision";
        return result;
    }

    if (!acInput.CanonicalSnapshot.QuestId)
    {
        result.Classification = PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped;
        result.Outcome = PartyQuestReconciliationOutcome::MissingModOrQuest;
        result.Reason = "missing-canonical-quest";
        return result;
    }

    if (acInput.RuntimeGeneration == 0)
    {
        result.Classification = PartyQuestReconciliationClassification::InsufficientOrUnstableObservation;
        result.Outcome = PartyQuestReconciliationOutcome::RetryAfterFreshSnapshot;
        result.Reason = "stale-or-zero-generation";
        return result;
    }

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (fence.IsPoisoned() || fence.IsLifecycleTransitionPending() || fence.GetGeneration() != acInput.RuntimeGeneration)
    {
        result.Classification = PartyQuestReconciliationClassification::InsufficientOrUnstableObservation;
        result.Outcome = PartyQuestReconciliationOutcome::RetryAfterFreshSnapshot;
        result.Reason = "generation-invalidation-pending";
        return result;
    }

    if (acInput.RecoveryBlocked || acInput.PriorTransactionPending)
    {
        result.Classification = PartyQuestReconciliationClassification::RecoveryRequired;
        result.Outcome = PartyQuestReconciliationOutcome::RecoveryRequired;
        result.Reason = "recovery-required-or-pending-transaction";
        return result;
    }

    for (const uint64_t retired : acInput.RetiredTransactions)
    {
        if (retired == acInput.TransactionId)
        {
            result.Classification = PartyQuestReconciliationClassification::ExactEquivalent;
            result.Outcome = PartyQuestReconciliationOutcome::NoOpEquivalent;
            result.Reason = "transaction-already-retired";
            return result;
        }
    }

    if (acInput.LastCommittedWorldRevision != 0 && acInput.TargetWorldRevision < acInput.LastCommittedWorldRevision)
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "stale-revision-retired";
        return result;
    }

    if (!acInput.LocalSnapshot.has_value())
    {
        result.Classification = PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped;
        result.Outcome = PartyQuestReconciliationOutcome::MissingModOrQuest;
        result.Reason = "missing-local-save-quest";
        return result;
    }

    const auto& local = *acInput.LocalSnapshot;
    const auto& canonical = acInput.CanonicalSnapshot;

    if (local.QuestId != canonical.QuestId)
    {
        result.Classification = PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped;
        result.Outcome = PartyQuestReconciliationOutcome::MissingModOrQuest;
        result.Reason = "quest-id-mismatch";
        return result;
    }

    if (local.Status == QuestSnapshotStatus::Stopped && canonical.Status == QuestSnapshotStatus::Running)
    {
        result.Classification = PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped;
        result.Outcome = PartyQuestReconciliationOutcome::MissingModOrQuest;
        result.Reason = "local-quest-stopped-or-failed";
        return result;
    }

    if (local.Status == QuestSnapshotStatus::Inactive && canonical.Status == QuestSnapshotStatus::Running)
    {
        if (!acInput.ReviewedTransition.has_value() || acInput.ReviewedTransition->Identity.SourceStage != 0)
        {
            result.Classification = PartyQuestReconciliationClassification::MissingOrNotStartedOrStopped;
            result.Outcome = PartyQuestReconciliationOutcome::MissingModOrQuest;
            result.Reason = "local-quest-not-started";
            return result;
        }
    }

    if (acInput.ReviewedTransition.has_value())
    {
        const auto& transition = *acInput.ReviewedTransition;
        if (transition.Identity.QuestId != canonical.QuestId)
        {
            result.Classification = PartyQuestReconciliationClassification::EnvironmentMismatch;
            result.Outcome = PartyQuestReconciliationOutcome::EnvironmentRejected;
            result.Reason = "transition-quest-identity-mismatch";
            return result;
        }

        if (acInput.ObservedEnvironmentFingerprint != 0 && transition.Identity.EnvironmentFingerprint != 0 &&
            acInput.ObservedEnvironmentFingerprint != transition.Identity.EnvironmentFingerprint)
        {
            result.Classification = PartyQuestReconciliationClassification::EnvironmentMismatch;
            result.Outcome = PartyQuestReconciliationOutcome::EnvironmentRejected;
            result.Reason = "environment-fingerprint-mismatch";
            return result;
        }
    }

    if (IsExactMatch(canonical, local))
    {
        result.Classification = PartyQuestReconciliationClassification::ExactEquivalent;
        result.Outcome = PartyQuestReconciliationOutcome::NoOpEquivalent;
        result.Reason = "canonical-facts-match-local-save";
        return result;
    }

    if (local.CurrentStage == canonical.CurrentStage)
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "alternate-branch-with-same-stage";
        return result;
    }

    if (local.CurrentStage > canonical.CurrentStage)
    {
        if (IsLocallyAheadCompatible(canonical, local))
        {
            result.Classification = PartyQuestReconciliationClassification::LocallyAheadCompatible;
            result.Outcome = PartyQuestReconciliationOutcome::ConfirmLocallyAhead;
            result.Reason = "local-save-ahead-compatible-with-canonical";
            return result;
        }

        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "local-save-ahead-incompatible-branch";
        return result;
    }

    if (!acInput.ReviewedTransition.has_value())
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "unreviewed-transition-edge";
        return result;
    }

    const auto& transition = *acInput.ReviewedTransition;

    if (transition.Identity.SourceStage != local.CurrentStage || transition.Identity.TargetStage != canonical.CurrentStage)
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "no-direct-reviewed-edge-for-stage-gap";
        return result;
    }

    if (!HasPriorStages(local, transition.RequiredPriorStages))
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "missing-required-prior-stages";
        return result;
    }

    if (!HasRequiredObjectives(local, transition.RequiredObjectives))
    {
        result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
        result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
        result.Reason = "missing-required-objectives";
        return result;
    }

    if (!local.ReferenceAliases.empty() || !local.LocationAliases.empty() || !canonical.ReferenceAliases.empty() || !canonical.LocationAliases.empty())
    {
        if (!transition.RequiresReadiness)
        {
            result.Classification = PartyQuestReconciliationClassification::DivergentBranchOrState;
            result.Outcome = PartyQuestReconciliationOutcome::UnsupportedDivergence;
            result.Reason = "unsupported-alias-dependency";
            return result;
        }
    }

    PartyQuestRuntimeApplyRequest candidate;
    candidate.TransactionId = acInput.TransactionId;
    candidate.TargetWorldRevision = acInput.TargetWorldRevision;
    candidate.CanonicalSnapshot = canonical;
    candidate.CanonicalSnapshot.Canonicalize();
    candidate.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();

    candidate.Plan.DryRunOnly = true;
    candidate.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    candidate.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    candidate.Plan.Actions = PartyQuestApplyAction::StageTransition | PartyQuestApplyAction::WaitForPapyrusQuiescence | PartyQuestApplyAction::ResnapshotAndVerify;
    if (transition.RequiresReadiness)
    {
        candidate.Plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
    }

    candidate.Plan.MutationAuthorization = PartyQuestRuntimeMutationAuthorization(
        canonical.QuestId, canonical.ComputeDigest(), transition.Identity.EnvironmentFingerprint, PartyQuestVerificationComponent::QuestSnapshot, candidate.Plan.Actions, true);

    result.Classification = PartyQuestReconciliationClassification::LocallyBehindReviewedEdge;
    result.Outcome = PartyQuestReconciliationOutcome::ApplyReviewedTransition;
    result.CandidateRequest = std::move(candidate);
    result.Reason = "admissible-reviewed-transition-candidate";

    return result;
}

bool PartyQuestReconciliationPlanner::IsExactMatch(const QuestSnapshot& acCanonical, const QuestSnapshot& acLocal) noexcept
{
    if (acCanonical.CurrentStage != acLocal.CurrentStage || acCanonical.Status != acLocal.Status)
        return false;

    for (const uint16_t stage : acCanonical.CompletedStages)
    {
        if (!ContainsStage(acLocal.CompletedStages, stage))
            return false;
    }

    for (const auto& canObj : acCanonical.Objectives)
    {
        const auto it = std::ranges::find_if(acLocal.Objectives, [&](const QuestObjectiveSnapshot& aLocObj) { return aLocObj.Index == canObj.Index; });
        if (it == acLocal.Objectives.end() || it->State != canObj.State)
            return false;
    }

    return true;
}

bool PartyQuestReconciliationPlanner::IsLocallyAheadCompatible(const QuestSnapshot& acCanonical, const QuestSnapshot& acLocal) noexcept
{
    if (acLocal.Status != QuestSnapshotStatus::Running && acCanonical.Status == QuestSnapshotStatus::Running)
        return false;

    if (!ContainsStage(acLocal.CompletedStages, acCanonical.CurrentStage) && acLocal.CurrentStage <= acCanonical.CurrentStage)
        return false;

    for (const uint16_t stage : acCanonical.CompletedStages)
    {
        if (!ContainsStage(acLocal.CompletedStages, stage))
            return false;
    }

    for (const auto& canObj : acCanonical.Objectives)
    {
        if (canObj.State == QuestObjectiveState::Completed)
        {
            const auto it = std::ranges::find_if(acLocal.Objectives, [&](const QuestObjectiveSnapshot& aLocObj) { return aLocObj.Index == canObj.Index; });
            if (it == acLocal.Objectives.end() || it->State != QuestObjectiveState::Completed)
                return false;
        }
    }

    return true;
}

bool PartyQuestReconciliationPlanner::HasPriorStages(const QuestSnapshot& acLocal, const std::vector<uint16_t>& acRequiredPrior) noexcept
{
    for (const uint16_t reqStage : acRequiredPrior)
    {
        if (!ContainsStage(acLocal.CompletedStages, reqStage))
            return false;
    }
    return true;
}

bool PartyQuestReconciliationPlanner::HasRequiredObjectives(const QuestSnapshot& acLocal, const std::vector<uint16_t>& acRequiredObjectives) noexcept
{
    for (const uint16_t reqIndex : acRequiredObjectives)
    {
        const auto it = std::ranges::find_if(acLocal.Objectives, [&](const QuestObjectiveSnapshot& aObj) { return aObj.Index == reqIndex; });
        if (it == acLocal.Objectives.end())
            return false;
    }
    return true;
}
