#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

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

bool IsProcessGuardedTransaction(uint64_t aTransactionId) noexcept
{
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    return aTransactionId != 0 &&
        processGuard.IsActive() &&
        processGuard.GetTransactionId() == aTransactionId;
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
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return TranslateBeginStatus(status);
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkWorldReady(
    const PartyQuestRuntimeApplyRequest& acCurrentRequest)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkWorldReady(acCurrentRequest))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    const auto runtimeMutationAuthority = BuildRuntimeMutationAuthority(acCurrentRequest);
    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority = runtimeMutationAuthority;
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
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

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    PartyQuestPapyrusRuntimeMonitor& aMonitor,
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkPapyrusQuiescent(aMonitor, std::move(aAuthorization)))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    PartyQuestPapyrusQuiescenceTracker& aTracker,
    PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.MarkPapyrusQuiescent(aTracker, std::move(aAuthorization)))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::MarkPapyrusQuiescent(
    uint64_t)
{
    return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
}

bool PartyQuestRuntimeApplySession::PrepareVerificationCompatibilityInternal(
    uint64_t aTransactionId,
    uint64_t aRuntimeGeneration,
    const PartyQuestRuntimeSafetyProfile& acSafetyProfile) noexcept
{
    const auto* active = m_coordinator.GetActive();
    if (!active ||
        aTransactionId == 0 ||
        aRuntimeGeneration == 0 ||
        active->TransactionId != aTransactionId ||
        active->State != PartyQuestRuntimeApplyState::Verifying ||
        !active->SaveGuardActive ||
        !active->CheckpointCreated ||
        !active->RuntimeMutationMayHaveOccurred ||
        !IsProcessGuardedTransaction(aTransactionId) ||
        !acSafetyProfile.IsVerifiedFor(active->QuestId) ||
        acSafetyProfile.GetCompatibilityFingerprint() !=
            active->ExpectedVerification.CompatibilityFingerprint ||
        acSafetyProfile.GetAdapterMutationComponents() !=
            PartyQuestVerificationComponent::QuestSnapshot ||
        m_pendingVerificationCompatibility)
    {
        return false;
    }

    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    if (generationFence.GetGeneration() != aRuntimeGeneration)
        return false;

    if (m_verificationRuntimeGeneration != 0 &&
        m_verificationRuntimeGeneration != aRuntimeGeneration)
    {
        return false;
    }

    PendingVerificationCompatibility pending;
    pending.TransactionId = aTransactionId;
    pending.RuntimeGeneration = aRuntimeGeneration;
    pending.SafetyProfile = acSafetyProfile;
    m_pendingVerificationCompatibility = std::move(pending);
    return true;
}

PartyQuestRuntimeDurableVerificationResult PartyQuestRuntimeApplySession::SubmitResnapshot(
    uint64_t aTransactionId,
    QuestSnapshot aObservedSnapshot)
{
    const bool processGuarded = IsProcessGuardedTransaction(aTransactionId);
    uint64_t verifiedGeneration = 0;

    if (processGuarded)
    {
        if (!m_pendingVerificationCompatibility ||
            m_pendingVerificationCompatibility->TransactionId != aTransactionId)
        {
            return {PartyQuestRuntimeVerificationStatus::InvalidState, false};
        }

        const PendingVerificationCompatibility pending =
            std::move(*m_pendingVerificationCompatibility);
        m_pendingVerificationCompatibility.reset();

        const auto* active = m_coordinator.GetActive();
        auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
        if (!active ||
            active->TransactionId != aTransactionId ||
            active->State != PartyQuestRuntimeApplyState::Verifying ||
            !pending.SafetyProfile.IsVerifiedFor(active->QuestId) ||
            pending.SafetyProfile.GetCompatibilityFingerprint() !=
                active->ExpectedVerification.CompatibilityFingerprint ||
            pending.RuntimeGeneration == 0 ||
            generationFence.GetGeneration() != pending.RuntimeGeneration ||
            (m_verificationRuntimeGeneration != 0 &&
             m_verificationRuntimeGeneration != pending.RuntimeGeneration))
        {
            return {PartyQuestRuntimeVerificationStatus::InvalidState, false};
        }

        auto lease = generationFence.TryAcquire(pending.RuntimeGeneration);
        if (!lease || !lease->IsValid())
            return {PartyQuestRuntimeVerificationStatus::InvalidState, false};

        verifiedGeneration = pending.RuntimeGeneration;
    }

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    const PartyQuestRuntimeVerificationStatus verification =
        candidate.SubmitResnapshot(aTransactionId, std::move(aObservedSnapshot));

    if (verification == PartyQuestRuntimeVerificationStatus::InvalidState)
        return {verification, false};

    if (!Persist(candidate))
        return {verification, true};

    m_coordinator = std::move(candidate);
    if (processGuarded && m_verificationRuntimeGeneration == 0)
        m_verificationRuntimeGeneration = verifiedGeneration;
    return {verification, false};
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::Commit(
    uint64_t aTransactionId)
{
    if (IsProcessGuardedTransaction(aTransactionId))
    {
        auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
        if (m_verificationRuntimeGeneration == 0 ||
            generationFence.GetGeneration() != m_verificationRuntimeGeneration)
        {
            m_pendingVerificationCompatibility.reset();
            return PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired;
        }
    }

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.Commit(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::AbortBeforeMutation(
    uint64_t aTransactionId)
{
    m_pendingVerificationCompatibility.reset();

    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.Abort(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (candidate.LastAbortRequiresCheckpointRestore())
    {
        m_verificationRuntimeGeneration = 0;
        return PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired;
    }

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::CompleteLiveCheckpointRestore(
    uint64_t)
{
    return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
}

PartyQuestRuntimeDurableTransitionStatus
PartyQuestRuntimeApplySession::CompleteLiveCheckpointRestoreInternal(
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
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}

PartyQuestRuntimeRecoveryDisposition PartyQuestRuntimeApplySession::RestoreRecoveryState(
    const PartyQuestRuntimeRecoveryState& acState) noexcept
{
    m_runtimeMutationAuthority.reset();
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return m_coordinator.RestoreRecoveryState(
        acState,
        m_campaignId,
        m_playerProfileId);
}

PartyQuestRuntimeDurableTransitionStatus PartyQuestRuntimeApplySession::CompleteCrashCheckpointRestore(
    uint64_t)
{
    return PartyQuestRuntimeDurableTransitionStatus::InvalidState;
}

PartyQuestRuntimeDurableTransitionStatus
PartyQuestRuntimeApplySession::CompleteCrashCheckpointRestoreInternal(
    uint64_t aTransactionId)
{
    PartyQuestRuntimeApplyCoordinator candidate = m_coordinator;
    if (!candidate.AcknowledgeCheckpointRestored(aTransactionId))
        return PartyQuestRuntimeDurableTransitionStatus::InvalidState;

    if (!Persist(candidate))
        return PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure;

    m_coordinator = std::move(candidate);
    m_runtimeMutationAuthority.reset();
    m_pendingVerificationCompatibility.reset();
    m_verificationRuntimeGeneration = 0;
    return PartyQuestRuntimeDurableTransitionStatus::Applied;
}
