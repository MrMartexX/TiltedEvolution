#include <Structs/Skyrim/PartyQuestRuntimeProcessRequestGate.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

namespace
{
bool MatchesPublishedHead(
    const PartyQuestRuntimeCanonicalCandidate& acCandidate,
    const PartyQuestReplica& acPublishedReplica) noexcept
{
    if (acPublishedReplica.GetWorldRevision() != acCandidate.WorldRevision)
        return false;

    const auto* pPublished =
        acPublishedReplica.FindQuest(acCandidate.CanonicalSnapshot.QuestId);
    return pPublished && *pPublished == acCandidate.CanonicalSnapshot;
}

bool MatchesLatestCandidate(
    const PartyQuestRuntimeCanonicalInbox& acInbox,
    const PartyQuestRuntimeCanonicalCandidate& acCandidate) noexcept
{
    const auto* pLatest = acInbox.FindLatest(acCandidate.CanonicalSnapshot.QuestId);
    return pLatest && *pLatest == acCandidate;
}

bool MatchesProcessOwnerCampaign(
    const PartyQuestCampaignId& acCampaignId) noexcept
{
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto* pSession = owner.GetRuntimeSession();
    return owner.IsBound() &&
        pSession &&
        pSession->GetCampaignId() == acCampaignId;
}
} // namespace

PartyQuestRuntimeProcessRequestResult
PartyQuestRuntimeProcessRequestGate::PlanLatest(
    PartyQuestRuntimeCanonicalInbox& aInbox,
    const PartyQuestReplica& acPublishedReplica,
    const GameId& acQuestId,
    const PartyQuestRuntimeCompatibilityManifest& acCompatibilityManifest,
    const EvidenceObserver& acEvidenceObserver) noexcept
{
    PartyQuestRuntimeProcessRequestResult result;
    try
    {
        if (!acQuestId || !acEvidenceObserver)
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::InvalidInput;
            return result;
        }

        // Pin the process runtime before the first process-owner dereference.
        // PrepareAndRelease() uses the exclusive side of this same fence before
        // Clear(), so every pointer read below remains valid until this planning
        // attempt returns. Previously owner/session were read first and only then
        // pinned, leaving a concrete Clear()-vs-dereference lifetime window.
        auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
        const uint64_t generation = generationFence.GetGeneration();
        auto generationLease = generationFence.TryAcquire(generation);
        if (generation == 0 || !generationLease || !generationLease->IsValid())
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::RuntimeGenerationChanged;
            return result;
        }
        result.RuntimeGeneration = generation;

        auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
        const auto* pRuntimeSession = owner.GetRuntimeSession();
        if (!owner.IsBound() || !pRuntimeSession)
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::ProcessOwnerUnavailable;
            return result;
        }
        if (owner.IsRecoveryBlocked())
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::RecoveryBlocked;
            return result;
        }

        const PartyQuestCampaignId campaignId = pRuntimeSession->GetCampaignId();
        if (!campaignId.IsValid() || aInbox.GetCampaignId() != campaignId)
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::CampaignMismatch;
            return result;
        }

        const auto* pLatest = aInbox.FindLatest(acQuestId);
        if (!pLatest ||
            pLatest->CampaignId != campaignId ||
            !MatchesPublishedHead(*pLatest, acPublishedReplica))
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::CandidateUnavailable;
            return result;
        }
        const PartyQuestRuntimeCanonicalCandidate candidate = *pLatest;

        const auto* pRequirement =
            acCompatibilityManifest.FindRequirement(acQuestId);
        if (!pRequirement)
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::RequirementUnavailable;
            return result;
        }

        if (!MatchesProcessOwnerCampaign(campaignId) ||
            !MatchesLatestCandidate(aInbox, candidate) ||
            !MatchesPublishedHead(candidate, acPublishedReplica))
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::PostPlanRevalidationFailed;
            return result;
        }

        std::optional<PartyQuestRuntimeProcessPlanningEvidence> evidence;
        try
        {
            evidence = acEvidenceObserver(candidate);
        }
        catch (...)
        {
            evidence.reset();
        }
        if (!evidence)
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::EvidenceUnavailable;
            return result;
        }

        if (generationFence.GetGeneration() != generation)
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::RuntimeGenerationChanged;
            return result;
        }

        // Sampling local evidence may synchronously re-enter protocol/runtime
        // integrations. Revalidate the exact candidate before issuing any new
        // canonical authorization so a newer head cannot mint authority that is
        // then accidentally paired with this stale candidate.
        if (!MatchesProcessOwnerCampaign(campaignId) ||
            aInbox.GetCampaignId() != campaignId ||
            !MatchesLatestCandidate(aInbox, candidate) ||
            !MatchesPublishedHead(candidate, acPublishedReplica))
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::PostPlanRevalidationFailed;
            return result;
        }

        // Issue canonical provenance only after all independent local evidence
        // exists and the original candidate has survived point-of-authority
        // revalidation. A transient missing provider or reentrant canonical
        // advance therefore cannot consume/redirect planning authority.
        auto authorization = aInbox.TryAuthorizeLatest(
            acQuestId,
            acPublishedReplica);
        if (!authorization || !authorization->IsVerified())
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::CanonicalAuthorizationUnavailable;
            return result;
        }

        result.Planner = PartyQuestRuntimeRequestPlanner::Build(
            candidate,
            std::move(*authorization),
            evidence->SyncFacts,
            *pRequirement,
            evidence->CompatibilityFacts,
            evidence->SidecarManifest);
        if (!result.Planner.IsPlanned() || !result.Planner.Request)
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::PlannerRejected;
            return result;
        }

        if (generationFence.GetGeneration() != generation)
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::RuntimeGenerationChanged;
            return result;
        }

        // Point-of-return revalidation. The request may leave this function only
        // if it still describes the exact current process owner + canonical head
        // observed under the same pinned runtime generation.
        if (!MatchesProcessOwnerCampaign(campaignId) ||
            aInbox.GetCampaignId() != campaignId ||
            !MatchesLatestCandidate(aInbox, candidate) ||
            !MatchesPublishedHead(candidate, acPublishedReplica))
        {
            result.Status =
                PartyQuestRuntimeProcessRequestStatus::PostPlanRevalidationFailed;
            return result;
        }

        // P0-D proves production planning/ownership only. P0-F is the first
        // milestone allowed to turn a reviewed request into a non-dry mutation
        // request, so fail closed if that boundary changes unexpectedly.
        if (!result.Planner.Request->Plan.DryRunOnly)
        {
            result.Status = PartyQuestRuntimeProcessRequestStatus::PlannerRejected;
            return result;
        }

        result.Request = std::move(result.Planner.Request);
        result.Status = PartyQuestRuntimeProcessRequestStatus::PlannedDryRun;
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestRuntimeProcessRequestStatus::InvalidInput;
        result.Request.reset();
        return result;
    }
}
