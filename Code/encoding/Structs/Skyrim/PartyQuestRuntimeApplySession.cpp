#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <optional>
#include <utility>

namespace
{
std::optional<PartyQuestRuntimeApplyIdentity> BuildRuntimeMutationAuthority(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    if (acRequest.Plan.DryRunOnly)
        return std::nullopt;
    return PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(acRequest);
}

bool MatchesRuntimeMutationAuthority(
    const PartyQuestRuntimeApplyIdentity& acAuthority,
    const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acAuthority.QuestId == acActive.QuestId &&
        acAuthority.TargetWorldRevision == acActive.TargetWorldRevision &&
        acAuthority.CanonicalDigest == acActive.CanonicalDigest &&
        acAuthority.SidecarManifestFingerprint == acActive.SidecarManifestFingerprint &&
        acAuthority.Actions == acActive.Actions &&
        acAuthority.ExpectedVerification == acActive.ExpectedVerification;
}
} // namespace

PartyQuestRuntimeApplySession::PartyQuestRuntimeApplySession(
    PartyQuestCampaignId aCampaignId,
    PartyQuestPlayerProfileId aPlayerProfileId,
    DurableStateHandler aDurableStateHandler,
    PartyQuestPersistenceGuarantee aPersistenceGuarantee)
    : m_campaignId(aCampaignId)
    , m_playerProfileId(aPlayerProfileId)
    , m_durableStateHandler(std::move(aDurableStateHandler))
    , m_persistenceGuarantee(
          m_durableStateHandler
              ? aPersistenceGuarantee
              : PartyQuestPersistenceGuarantee::Volatile)
{
}

void PartyQuestRuntimeApplySession::SetDurableStateHandler(
    DurableStateHandler aDurableStateHandler,
    PartyQuestPersistenceGuarantee aPersistenceGuarantee)
{
    m_durableStateHandler = std::move(aDurableStateHandler);
    m_persistenceGuarantee = m_durableStateHandler
        ? aPersistenceGuarantee
        : PartyQuestPersistenceGuarantee::Volatile;
}

bool PartyQuestRuntimeApplySession::Persist(
    const PartyQuestRuntimeApplyCoordinator& acCandidate) const
{
    if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid() || !m_durableStateHandler)
        return false;

    try
    {
        return m_durableStateHandler(
            acCandidate.ExportRecoveryState(m_campaignId, m_playerProfileId));
    }
    catch (...)
    {
        // A storage callback exception is equivalent to a failed durability
        // barrier. Never publish the candidate state in memory afterward.
        return false;
    }
}

PartyQuestRuntimeDurableBeginStatus PartyQuestRuntimeApplySession::TranslateBeginStatus(
    PartyQuestRuntimeApplyBeginStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestRuntimeApplyBeginStatus::Started: return PartyQuestRuntimeDurableBeginStatus::Started;
    case PartyQuestRuntimeApplyBeginStatus::Deferred: return PartyQuestRuntimeDurableBeginStatus::Deferred;
    case PartyQuestRuntimeApplyBeginStatus::DuplicatePending: return PartyQuestRuntimeDurableBeginStatus::DuplicatePending;
    case PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted: return PartyQuestRuntimeDurableBeginStatus::DuplicateCommitted;
    case PartyQuestRuntimeApplyBeginStatus::TransactionConflict: return PartyQuestRuntimeDurableBeginStatus::TransactionConflict;
    case PartyQuestRuntimeApplyBeginStatus::Busy: return PartyQuestRuntimeDurableBeginStatus::Busy;
    case PartyQuestRuntimeApplyBeginStatus::RecoveryBlocked: return PartyQuestRuntimeDurableBeginStatus::RecoveryBlocked;
    case PartyQuestRuntimeApplyBeginStatus::ResourceLimitExceeded: return PartyQuestRuntimeDurableBeginStatus::ResourceLimitExceeded;
    case PartyQuestRuntimeApplyBeginStatus::InvalidRequest: return PartyQuestRuntimeDurableBeginStatus::InvalidRequest;
    case PartyQuestRuntimeApplyBeginStatus::UnsafePlan: return PartyQuestRuntimeDurableBeginStatus::UnsafePlan;
    }

    return PartyQuestRuntimeDurableBeginStatus::InvalidRequest;
}

PartyQuestRuntimeDurableBeginStatus PartyQuestRuntimeApplySession::Begin(
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    if (!m_campaignId.IsValid() || !m_playerProfileId.IsValid())
        return PartyQuestRuntimeDurableBeginStatus::InvalidRequest;

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    const PartyQuestRuntimeApplyBeginStatus status = candidate.Begin(acRequest);

    if (status != PartyQuestRuntimeApplyBeginStatus::Started &&
        status != PartyQuestRuntimeApplyBeginStatus::Deferred)
    {
        return TranslateBeginStatus(status);
    }

    const auto runtimeMutationAuthority = BuildRuntimeMutationAuthority(acRequest);
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableBeginStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority = runtimeMutationAuthority;
    return TranslateBeginStatus(status);
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkWorldReady(
    const PartyQuestRuntimeApplyRequest& acCurrentRequest)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkWorldReady(acCurrentRequest))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    const auto runtimeMutationAuthority =
        BuildRuntimeMutationAuthority(acCurrentRequest);
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority = runtimeMutationAuthority;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkCheckpointCreatedInternal(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkCheckpointCreated(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::ArmRuntimeMutationInternal(
    uint64_t aTransactionId)
{
    if (!PartyQuestPersistenceDurabilityPolicy::Meets(
            m_persistenceGuarantee,
            PartyQuestPersistenceDurabilityPolicy::MinimumPoCRuntimeMutationGuarantee))
    {
        return PartyQuestRuntimeDurableTransitionStatus::InsufficientDurability;
    }

    const auto* active = m_coordinator.GetActive();
    if (!active ||
        !m_runtimeMutationAuthority ||
        active->TransactionId != aTransactionId ||
        !MatchesRuntimeMutationAuthority(*m_runtimeMutationAuthority, *active))
    {
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
    }

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkApplyDispatched(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    // The persisted candidate already says RuntimeMutationMayHaveOccurred=true.
    // A future Skyrim executor may run only after this succeeds.
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    PartyQuestPapyrusRuntimeMonitor& aMonitor,
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkPapyrusQuiescent(aMonitor, std::move(aAuthorization)))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    // Trusted observation authorization is consumed before persistence. If
    // durable publication fails, the live coordinator remains WaitingForPapyrus
    // and the monitor must gather a fresh authoritative observation sequence.
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    PartyQuestPapyrusQuiescenceTracker& aTracker,
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkPapyrusQuiescent(aTracker, std::move(aAuthorization)))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    // Authorization is intentionally consumed before persistence. If durable
    // publication fails, the live coordinator remains WaitingForPapyrus and a
    // caller must obtain fresh observations rather than replay stale evidence.
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    uint64_t)
{
    // A naked transaction id is not quiescence evidence.
    return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
}

PartyQuestRuntimeDurableVerificationResult PartyQuestRuntimeApplySession::SubmitResnapshot(
    uint64_t aTransactionId,
    QuestSnapshot aObservedSnapshot)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    const PartyQuestRuntimeVerificationStatus verification =
        candidate.SubmitResnapshot(aTransactionId, std::move(aObservedSnapshot));

    if (verification == PartyQuestRuntimeVerificationStatus::InvalidState)
        return {verification, false};

    if (!Persist(candidate))
        return {verification, true};

    m_coordinator = std::move(candidate);
    return {verification, false};
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::Commit(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.Commit(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    // The committed transaction journal must be durable before the in-memory
    // save guard is released/published as committed.
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::AbortBeforeMutation(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.Abort(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (candidate.LastAbortRequiresCheckpointRestore())
        return PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::CompleteLiveCheckpointRestore(
    uint64_t aTransactionId)
{
    const PartyQuestRuntimeApplyEntry* pActive = m_coordinator.GetActive();
    if (!pActive ||
        pActive->TransactionId != aTransactionId ||
        !pActive->CheckpointCreated ||
        !pActive->RuntimeMutationMayHaveOccurred)
    {
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
    }

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.Abort(aTransactionId) || !candidate.LastAbortRequiresCheckpointRestore())
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeRecoveryDisposition PartyQuestRuntimeApplySession::RestoreRecoveryState(
    const PartyQuestRuntimeRecoveryState& acState) noexcept
{
    m_runtimeMutationAuthority.reset();
    return m_coordinator.RestoreRecoveryState(
        acState,
        m_campaignId,
        m_playerProfileId);
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::CompleteCrashCheckpointRestore(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.AcknowledgeCheckpointRestored(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}
