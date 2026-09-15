#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <atomic>

namespace
{
PartyQuestRuntimeEnvelopeResult EnvelopeResult(
    PartyQuestRuntimeEnvelopeOutcome aOutcome,
    PartyQuestRuntimeEnvelopeDisposition aDisposition =
        PartyQuestRuntimeEnvelopeDisposition::NoOp) noexcept
{
    PartyQuestRuntimeEnvelopeResult result;
    result.Outcome = aOutcome;
    result.Disposition = aDisposition;
    return result;
}

bool IsCanonicalSource(const QuestSnapshot& acSnapshot,
    const PartyQuestTransitionIdentity& acTransition) noexcept
{
    return acSnapshot.QuestId == acTransition.QuestId &&
        acSnapshot.CurrentStage == acTransition.SourceStage;
}

bool HasForbiddenSurface(const PartyQuestTransitionEvidence& acEvidence) noexcept
{
    return acEvidence.SceneCount != 0 || acEvidence.AliasCount != 0 ||
        acEvidence.CreatedReferenceCount != 0 || acEvidence.HasPlayerAlias ||
        acEvidence.HasInventoryMutation || acEvidence.HasQuestObjectMutation ||
        acEvidence.HasWorldMutation || acEvidence.HasAliasMutation ||
        acEvidence.HasUnboundedScriptEffects;
}

bool IsExactPostcondition(const QuestSnapshot& acExpected,
    QuestSnapshot aObserved) noexcept
{
    QuestSnapshot expected = acExpected;
    expected.Canonicalize();
    aObserved.Canonicalize();
    return expected == aObserved;
}

PartyQuestRuntimeGuardedVerificationResult BuildResult(PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeVerificationMonitor& aMonitor, uint64_t aTransactionId,
    PartyQuestRuntimeVerificationEvidenceStatus aEvidence) noexcept
{
    PartyQuestRuntimeGuardedVerificationResult result;
    result.TransactionId = aTransactionId;
    result.MonitorStatus = aMonitor.GetStatus();
    result.EvidenceStatus = aEvidence;
    const auto& guard = aGuarded.GetSaveGuard();
    result.GuardHeld = aTransactionId != 0 && guard.IsActive() &&
        guard.GetTransactionId() == aTransactionId;
    return result;
}

bool HasProcessOwner(PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession) noexcept
{
    auto& guard = PartyQuestSaveGuard::GetProcessGuard();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    return owner.IsBound() && owner.GetGuardedSession() == &aGuarded &&
        owner.GetRuntimeSession() == &aSession &&
        &aGuarded.GetRuntimeSession() == &aSession &&
        &aGuarded.GetSaveGuard() == &guard;
}

PartyQuestRuntimeGuardedVerificationResult SubmitMismatch(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    uint64_t aTransactionId, uint64_t aNowMs) noexcept
{
    QuestSnapshot invalid;
    auto result = aGuarded.SubmitVerificationResnapshot(
        aMonitor, aTransactionId, aNowMs, std::move(invalid));
    result.EvidenceStatus = PartyQuestRuntimeVerificationEvidenceStatus::VerifiedMismatch;
    return result;
}
} // namespace

PartyQuestRuntimeEnvelopeResult PartyQuestRuntimeVerificationGate::AuthorizeEnvelope(
    const PartyQuestReviewedTransition& acReviewed,
    const PartyQuestTransitionEvidence& acAdmissionEvidence,
    const PartyQuestRuntimeEnvelopeIdentity& acExpectedIdentity,
    const QuestSnapshot& acExpectedTarget,
    const PartyQuestRuntimeEnvelopePreconditions& acObserved) noexcept
{
    try
    {
        const auto admission = PartyQuestCompatibilityAdmissionPolicy::Evaluate(
            acReviewed, acAdmissionEvidence);
        if (!admission.IsEligible() || HasForbiddenSurface(acAdmissionEvidence) ||
            admission.TransitionFingerprint != acExpectedIdentity.CompatibilityFingerprint ||
            acExpectedIdentity.Transition != acReviewed.Identity ||
            acExpectedIdentity.CampaignId.IsValid() == false ||
            acExpectedIdentity.PlayerProfileId.IsValid() == false ||
            acExpectedIdentity.SessionId == 0 || acExpectedIdentity.PartyId == 0 ||
            acExpectedIdentity.RuntimeGeneration == 0 ||
            acExpectedIdentity.TransactionId == 0 ||
            acExpectedIdentity.AuthoritativeRevision == 0 ||
            acExpectedIdentity.OperationId == 0 ||
            acExpectedIdentity.ExpectedCurrentSnapshotDigest == 0 ||
            acExpectedIdentity.ExpectedTargetSnapshotDigest == 0 ||
            acAdmissionEvidence.AuthoritativeRevision != acExpectedIdentity.AuthoritativeRevision ||
            acAdmissionEvidence.OperationId != acExpectedIdentity.OperationId ||
            acExpectedTarget.QuestId != acReviewed.Identity.QuestId ||
            acExpectedTarget.CurrentStage != acReviewed.Identity.TargetStage ||
            acExpectedTarget.ComputeDigest() != acExpectedIdentity.ExpectedTargetSnapshotDigest)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::Unsupported);

        if (acObserved.CurrentIdentity != acExpectedIdentity ||
            !acObserved.ProcessOwnerCurrent || !acObserved.SessionCurrent ||
            !acObserved.PartyCurrent || !acObserved.TransactionActive ||
            !acObserved.RevisionCurrent || !acObserved.CompatibilityCacheCurrent)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::RejectedStale);

        if (!IsCanonicalSource(acObserved.CurrentSnapshot, acReviewed.Identity) ||
            acObserved.CurrentSnapshot.ComputeDigest() !=
                acExpectedIdentity.ExpectedCurrentSnapshotDigest)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::RejectedStale);
        if (acReviewed.RequiresReadiness &&
            (!acObserved.ReferenceEvidenceAvailable || !acObserved.ReferenceReady ||
             acObserved.ReferenceGeneration != acExpectedIdentity.RuntimeGeneration))
            return EnvelopeResult(acObserved.ReferenceEvidenceAvailable
                    ? PartyQuestRuntimeEnvelopeOutcome::BusyRetryable
                    : PartyQuestRuntimeEnvelopeOutcome::ObserverUnavailable);
        if (acReviewed.RequiresQuiescence && !acObserved.PapyrusObserverAvailable)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::ObserverUnavailable);
        if (acReviewed.RequiresQuiescence && !acObserved.PapyrusQuiescent)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::BusyRetryable);
        if (acReviewed.RecoveryClass == PartyQuestRecoveryClass::DurableCheckpointRequired &&
            (!acObserved.CheckpointAuthorized ||
             acObserved.CheckpointTransactionId != acExpectedIdentity.TransactionId ||
             acObserved.CheckpointRevision != acExpectedIdentity.AuthoritativeRevision))
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::RejectedStale);

        static std::atomic<uint64_t> nextNonce{1};
        uint64_t nonce = nextNonce.fetch_add(1, std::memory_order_relaxed);
        if (nonce == 0)
            nonce = nextNonce.fetch_add(1, std::memory_order_relaxed);
        if (nonce == 0)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::Unsupported);

        PartyQuestRuntimeEnvelopeResult result =
            EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::Authorized);
        PartyQuestRuntimeEnvelopeAuthorization authorization;
        authorization.m_identity = acExpectedIdentity;
        authorization.m_expectedTarget = acExpectedTarget;
        authorization.m_expectedTarget.Canonicalize();
        authorization.m_nonce = nonce;
        result.Authorization.emplace(std::move(authorization));
        return result;
    }
    catch (...)
    {
        return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::ObserverUnavailable);
    }
}

PartyQuestRuntimeEnvelopeResult PartyQuestRuntimeVerificationGate::CompleteEnvelope(
    PartyQuestRuntimeEnvelopeAuthorization&& aAuthorization,
    const PartyQuestRuntimeEnvelopeIdentity& acCurrentIdentity,
    const PartyQuestRuntimeEnvelopePostconditions& acObserved,
    bool aMutationAttempted, bool aMutationReturnedSuccess,
    bool aTimedOut) noexcept
{
    try
    {
        if (!aAuthorization.IsValid())
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::RejectedStale);

        const auto identity = aAuthorization.m_identity;
        const auto expected = aAuthorization.m_expectedTarget;
        aAuthorization.m_nonce = 0; // consume before any fallible observation work

        if (identity != acCurrentIdentity)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::RejectedStale,
                aMutationAttempted ? PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired
                                   : PartyQuestRuntimeEnvelopeDisposition::NoOp);
        if (aTimedOut)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::Timeout,
                aMutationAttempted ? PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired
                                   : PartyQuestRuntimeEnvelopeDisposition::NoOp);
        if (!aMutationAttempted || !aMutationReturnedSuccess)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::MutationFailed,
                aMutationAttempted ? PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired
                                   : PartyQuestRuntimeEnvelopeDisposition::NoOp);
        if (!acObserved.Snapshot)
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::ObserverUnavailable,
                PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired);
        if (acObserved.AliasDelta || acObserved.SceneDelta || acObserved.InventoryDelta ||
            acObserved.QuestObjectDelta || acObserved.WorldDelta ||
            acObserved.StableSamples < 2 ||
            !IsExactPostcondition(expected, *acObserved.Snapshot))
            return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::PostconditionMismatch,
                PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired);
        return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::Verified,
            PartyQuestRuntimeEnvelopeDisposition::Commit);
    }
    catch (...)
    {
        aAuthorization.m_nonce = 0;
        return EnvelopeResult(PartyQuestRuntimeEnvelopeOutcome::ObserverUnavailable,
            aMutationAttempted ? PartyQuestRuntimeEnvelopeDisposition::RecoveryRequired
                               : PartyQuestRuntimeEnvelopeDisposition::NoOp);
    }
}

PartyQuestRuntimeVerificationAttemptResult PartyQuestRuntimeVerificationGate::BeginAttempt(
    PartyQuestRuntimeGuardedSession& aGuarded, PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor, uint64_t aTransactionId) noexcept
{
    PartyQuestRuntimeVerificationAttemptResult result;
    if (!HasProcessOwner(aGuarded, aSession))
        return result;

    const auto* active = aSession.GetCoordinator().GetActive();
    auto& guard = PartyQuestSaveGuard::GetProcessGuard();
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = fence.GetGeneration();
    auto lease = fence.TryAcquire(generation);
    if (!lease || !lease->IsValid() || !active || aTransactionId == 0 ||
        active->TransactionId != aTransactionId ||
        active->State != PartyQuestRuntimeApplyState::Verifying ||
        !active->SaveGuardActive || !active->CheckpointCreated ||
        !active->RuntimeMutationMayHaveOccurred || !guard.IsActive() ||
        guard.GetTransactionId() != aTransactionId ||
        aMonitor.GetTransactionId() != aTransactionId)
    {
        result.Status = PartyQuestRuntimeVerificationEvidenceStatus::Stale;
        return result;
    }

    const uint64_t attemptId = aMonitor.IssueAttempt();
    if (attemptId == 0)
    {
        result.Status = aMonitor.GetStatus() == PartyQuestRuntimeVerificationMonitorStatus::Stable
            ? PartyQuestRuntimeVerificationEvidenceStatus::Duplicate
            : PartyQuestRuntimeVerificationEvidenceStatus::Stale;
        return result;
    }

    PartyQuestRuntimeVerificationAttempt attempt;
    attempt.m_campaignId = aSession.GetCampaignId();
    attempt.m_playerProfileId = aSession.GetPlayerProfileId();
    attempt.m_runtimeGeneration = generation;
    attempt.m_transactionId = active->TransactionId;
    attempt.m_targetWorldRevision = active->TargetWorldRevision;
    attempt.m_questId = active->QuestId;
    attempt.m_actions = active->Actions;
    attempt.m_expected = active->ExpectedVerification;
    attempt.m_attemptId = attemptId;
    result.Status = PartyQuestRuntimeVerificationEvidenceStatus::Accepted;
    result.Attempt.emplace(std::move(attempt));
    return result;
}

PartyQuestRuntimeGuardedVerificationResult PartyQuestRuntimeVerificationGate::Submit(
    PartyQuestRuntimeGuardedSession& aGuarded, PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeVerificationMonitor& aMonitor,
    PartyQuestRuntimeVerificationAttempt&& aAttempt, uint64_t aNowMs,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    const SnapshotObserver& acSnapshotObserver,
    const CompatibilityObserver& acCompatibilityObserver) noexcept
{
    const uint64_t transactionId = aAttempt.m_transactionId;
    if (!aAttempt.IsValid())
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::Duplicate);
    if (!HasProcessOwner(aGuarded, aSession))
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::InvalidEvidence);

    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto lease = fence.TryAcquire(aAttempt.m_runtimeGeneration);
    const auto* active = aSession.GetCoordinator().GetActive();
    const bool matches = lease && lease->IsValid() && active &&
        aSession.GetCampaignId() == aAttempt.m_campaignId &&
        aSession.GetPlayerProfileId() == aAttempt.m_playerProfileId &&
        active->TransactionId == aAttempt.m_transactionId &&
        active->TargetWorldRevision == aAttempt.m_targetWorldRevision &&
        active->QuestId == aAttempt.m_questId && active->Actions == aAttempt.m_actions &&
        active->ExpectedVerification == aAttempt.m_expected &&
        active->State == PartyQuestRuntimeApplyState::Verifying &&
        aMonitor.GetTransactionId() == transactionId &&
        aMonitor.GetStatus() == PartyQuestRuntimeVerificationMonitorStatus::Waiting;
    if (!matches)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::Stale);
    }

    const auto consumed = aMonitor.ConsumeAttempt(aAttempt.m_attemptId);
    if (consumed != PartyQuestRuntimeVerificationEvidenceStatus::Accepted)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId, consumed);
    }
    if (acRequirement.QuestId != active->QuestId ||
        !PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(acRequirement))
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::Stale);
    }
    if (!acSnapshotObserver || !acCompatibilityObserver)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::ObserverUnavailable);
    }

    std::optional<QuestSnapshot> snapshot;
    std::optional<PartyQuestRuntimeCompatibilityFacts> facts;
    try
    {
        snapshot = acSnapshotObserver(active->QuestId);
        facts = acCompatibilityObserver(active->QuestId);
    }
    catch (...)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::ObserverUnavailable);
    }
    if (!snapshot || !facts)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::ObserverUnavailable);
    }

    active = aSession.GetCoordinator().GetActive();
    if (!active || fence.GetGeneration() != aAttempt.m_runtimeGeneration ||
        active->TransactionId != transactionId ||
        active->TargetWorldRevision != aAttempt.m_targetWorldRevision ||
        active->QuestId != aAttempt.m_questId || active->Actions != aAttempt.m_actions ||
        active->ExpectedVerification != aAttempt.m_expected ||
        active->State != PartyQuestRuntimeApplyState::Verifying)
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::Stale);
    }

    snapshot->Canonicalize();
    const auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        acRequirement, *facts);
    if (snapshot->QuestId != active->QuestId || !compatibility.IsAuthorized() ||
        compatibility.SafetyProfile.GetCompatibilityFingerprint() !=
            active->ExpectedVerification.CompatibilityFingerprint ||
        compatibility.SafetyProfile.GetAdapterMutationComponents() !=
            PartyQuestVerificationComponent::QuestSnapshot)
    {
        aAttempt.Invalidate();
        return SubmitMismatch(aGuarded, aMonitor, transactionId, aNowMs);
    }

    if (!aSession.PrepareVerificationCompatibilityInternal(transactionId,
            aAttempt.m_runtimeGeneration, compatibility.SafetyProfile))
    {
        aAttempt.Invalidate();
        return BuildResult(aGuarded, aMonitor, transactionId,
            PartyQuestRuntimeVerificationEvidenceStatus::Stale);
    }
    aAttempt.Invalidate();
    auto result = aGuarded.SubmitVerificationResnapshot(
        aMonitor, transactionId, aNowMs, std::move(*snapshot));
    aSession.m_pendingVerificationCompatibility.reset();
    result.EvidenceStatus = result.Verification == PartyQuestRuntimeVerificationStatus::Stable
        ? PartyQuestRuntimeVerificationEvidenceStatus::VerifiedSuccess
        : (result.Verification == PartyQuestRuntimeVerificationStatus::Diverged
            ? PartyQuestRuntimeVerificationEvidenceStatus::VerifiedMismatch
            : PartyQuestRuntimeVerificationEvidenceStatus::Accepted);
    return result;
}
