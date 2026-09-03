#include <Structs/Skyrim/PartyQuestRuntimeVerificationGate.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

namespace
{
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
