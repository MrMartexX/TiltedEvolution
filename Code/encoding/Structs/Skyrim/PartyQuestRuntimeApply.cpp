#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <algorithm>
#include <unordered_set>

namespace
{
bool RequiresWorldTargets(const PartyQuestApplyPlan& acPlan) noexcept
{
    return HasPartyQuestApplyAction(acPlan.Actions, PartyQuestApplyAction::WaitForWorldTargets);
}

bool HasValidMutationAuthorization(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    return acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() ==
            PartyQuestVerificationComponent::QuestSnapshot &&
        acRequest.Plan.MutationAuthorization.Matches(
        acRequest.CanonicalSnapshot,
        acRequest.Plan.Actions,
        acRequest.Plan.DryRunOnly);
}
} // namespace

std::optional<PartyQuestRuntimeApplyCoordinator::Fingerprint>
PartyQuestRuntimeApplyCoordinator::ValidateAndFingerprint(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    if (acRequest.TransactionId == 0 ||
        acRequest.TargetWorldRevision == 0 ||
        acRequest.SidecarManifestFingerprint == 0 ||
        !acRequest.CanonicalSnapshot.QuestId ||
        acRequest.CanonicalSnapshot.Revision == 0)
    {
        return std::nullopt;
    }

    if (acRequest.Plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence) ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::ResnapshotAndVerify) ||
        !HasValidMutationAuthorization(acRequest))
    {
        return std::nullopt;
    }

    QuestSnapshot canonical = acRequest.CanonicalSnapshot;
    canonical.Canonicalize();

    Fingerprint fingerprint;
    fingerprint.QuestId = canonical.QuestId;
    fingerprint.TargetWorldRevision = acRequest.TargetWorldRevision;
    fingerprint.CanonicalDigest = canonical.ComputeDigest();
    fingerprint.SidecarManifestFingerprint = acRequest.SidecarManifestFingerprint;
    fingerprint.Actions = acRequest.Plan.Actions;
    const auto envelope = PartyQuestVerificationPolicy::BuildExpected(
        fingerprint.Actions,
        fingerprint.CanonicalDigest,
        acRequest.Plan.MutationAuthorization.GetCompatibilityFingerprint());
    if (!envelope)
        return std::nullopt;
    const auto authorizedCoverage =
        acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() |
        PartyQuestVerificationComponent::Compatibility;
    if (envelope->Required != authorizedCoverage)
        return std::nullopt;
    fingerprint.ExpectedVerification = *envelope;
    return fingerprint;
}

PartyQuestRuntimeApplyCoordinator::Fingerprint
PartyQuestRuntimeApplyCoordinator::FingerprintActive(
    const PartyQuestRuntimeApplyEntry& acEntry) noexcept
{
    Fingerprint fingerprint;
    fingerprint.QuestId = acEntry.QuestId;
    fingerprint.TargetWorldRevision = acEntry.TargetWorldRevision;
    fingerprint.CanonicalDigest = acEntry.CanonicalDigest;
    fingerprint.SidecarManifestFingerprint = acEntry.SidecarManifestFingerprint;
    fingerprint.Actions = acEntry.Actions;
    fingerprint.ExpectedVerification = acEntry.ExpectedVerification;
    return fingerprint;
}

bool PartyQuestRuntimeApplyCoordinator::ValidateRecoveryEntry(
    const PartyQuestRuntimeApplyEntry& acEntry) noexcept
{
    if (acEntry.TransactionId == 0 ||
        acEntry.TargetWorldRevision == 0 ||
        !acEntry.QuestId ||
        acEntry.CanonicalDigest == 0 ||
        acEntry.SidecarManifestFingerprint == 0 ||
        acEntry.Actions == PartyQuestApplyAction::None ||
        !PartyQuestVerificationPolicy::IsCompleteForActions(
            acEntry.ExpectedVerification,
            acEntry.Actions) ||
        acEntry.ExpectedVerification.QuestSnapshotDigest != acEntry.CanonicalDigest)
    {
        return false;
    }

    switch (acEntry.State)
    {
    case PartyQuestRuntimeApplyState::DeferredWorld:
        return !acEntry.SaveGuardActive &&
            !acEntry.CheckpointCreated &&
            !acEntry.RuntimeMutationMayHaveOccurred;

    case PartyQuestRuntimeApplyState::AwaitingCheckpoint:
        return acEntry.SaveGuardActive &&
            !acEntry.CheckpointCreated &&
            !acEntry.RuntimeMutationMayHaveOccurred;

    case PartyQuestRuntimeApplyState::ReadyToApply:
        return acEntry.SaveGuardActive &&
            acEntry.CheckpointCreated &&
            !acEntry.RuntimeMutationMayHaveOccurred;

    case PartyQuestRuntimeApplyState::WaitingForPapyrus:
        return acEntry.SaveGuardActive &&
            acEntry.CheckpointCreated &&
            acEntry.RuntimeMutationMayHaveOccurred;

    case PartyQuestRuntimeApplyState::Verifying:
        return acEntry.SaveGuardActive &&
            acEntry.CheckpointCreated &&
            acEntry.RuntimeMutationMayHaveOccurred;

    case PartyQuestRuntimeApplyState::ReadyToCommit:
        return acEntry.SaveGuardActive &&
            acEntry.CheckpointCreated &&
            acEntry.RuntimeMutationMayHaveOccurred &&
            acEntry.StableCanonicalSamples >= 2;
    }

    return false;
}

PartyQuestRuntimeApplyBeginStatus PartyQuestRuntimeApplyCoordinator::Begin(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    m_lastAbortRequiresCheckpointRestore = false;

    if (m_recoveryBlocked)
        return PartyQuestRuntimeApplyBeginStatus::RecoveryBlocked;

    if (acRequest.TransactionId == 0 ||
        acRequest.TargetWorldRevision == 0 ||
        acRequest.SidecarManifestFingerprint == 0 ||
        !acRequest.CanonicalSnapshot.QuestId ||
        acRequest.CanonicalSnapshot.Revision == 0)
    {
        return PartyQuestRuntimeApplyBeginStatus::InvalidRequest;
    }

    if (acRequest.Plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence) ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::ResnapshotAndVerify) ||
        !HasValidMutationAuthorization(acRequest))
    {
        return PartyQuestRuntimeApplyBeginStatus::UnsafePlan;
    }

    const auto fingerprint = ValidateAndFingerprint(acRequest);
    if (!fingerprint)
        return PartyQuestRuntimeApplyBeginStatus::InvalidRequest;

    const auto committedIt = m_committed.find(acRequest.TransactionId);
    if (committedIt != m_committed.end())
    {
        return committedIt->second == *fingerprint
            ? PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted
            : PartyQuestRuntimeApplyBeginStatus::TransactionConflict;
    }

    if (m_active)
    {
        if (m_active->TransactionId == acRequest.TransactionId)
        {
            return FingerprintActive(*m_active) == *fingerprint
                ? PartyQuestRuntimeApplyBeginStatus::DuplicatePending
                : PartyQuestRuntimeApplyBeginStatus::TransactionConflict;
        }

        return PartyQuestRuntimeApplyBeginStatus::Busy;
    }

    PartyQuestRuntimeApplyEntry entry;
    entry.TransactionId = acRequest.TransactionId;
    entry.TargetWorldRevision = acRequest.TargetWorldRevision;
    entry.QuestId = fingerprint->QuestId;
    entry.CanonicalDigest = fingerprint->CanonicalDigest;
    entry.SidecarManifestFingerprint = fingerprint->SidecarManifestFingerprint;
    entry.Actions = fingerprint->Actions;
    entry.ExpectedVerification = fingerprint->ExpectedVerification;

    if (RequiresWorldTargets(acRequest.Plan))
    {
        // Do not hold the save guard while waiting potentially minutes for a
        // cell/world target to become available.
        entry.State = PartyQuestRuntimeApplyState::DeferredWorld;
        entry.SaveGuardActive = false;
        m_active = entry;
        return PartyQuestRuntimeApplyBeginStatus::Deferred;
    }

    entry.State = PartyQuestRuntimeApplyState::AwaitingCheckpoint;
    entry.SaveGuardActive = true;
    m_active = entry;
    return PartyQuestRuntimeApplyBeginStatus::Started;
}

bool PartyQuestRuntimeApplyCoordinator::MarkWorldReady(uint64_t aTransactionId) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::DeferredWorld)
    {
        return false;
    }

    m_active->State = PartyQuestRuntimeApplyState::AwaitingCheckpoint;
    m_active->SaveGuardActive = true;
    return true;
}

bool PartyQuestRuntimeApplyCoordinator::MarkCheckpointCreated(uint64_t aTransactionId) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::AwaitingCheckpoint ||
        !m_active->SaveGuardActive)
    {
        return false;
    }

    m_active->CheckpointCreated = true;
    m_active->State = PartyQuestRuntimeApplyState::ReadyToApply;
    return true;
}

bool PartyQuestRuntimeApplyCoordinator::MarkApplyDispatched(uint64_t aTransactionId) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::ReadyToApply ||
        !m_active->SaveGuardActive ||
        !m_active->CheckpointCreated)
    {
        return false;
    }

    m_active->RuntimeMutationMayHaveOccurred = true;
    m_active->State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    return true;
}

bool PartyQuestRuntimeApplyCoordinator::MarkPapyrusQuiescent(uint64_t aTransactionId) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::WaitingForPapyrus)
    {
        return false;
    }

    m_active->LastObservedDigest = 0;
    m_active->StableCanonicalSamples = 0;
    m_active->State = PartyQuestRuntimeApplyState::Verifying;
    return true;
}

PartyQuestRuntimeVerificationStatus PartyQuestRuntimeApplyCoordinator::SubmitResnapshot(
    uint64_t aTransactionId,
    QuestSnapshot aObservedSnapshot) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::Verifying)
    {
        return PartyQuestRuntimeVerificationStatus::InvalidState;
    }

    aObservedSnapshot.Canonicalize();
    if (aObservedSnapshot.QuestId != m_active->QuestId)
    {
        m_active->LastObservedDigest = 0;
        m_active->StableCanonicalSamples = 0;
        return PartyQuestRuntimeVerificationStatus::Diverged;
    }

    const uint64_t digest = aObservedSnapshot.ComputeDigest();
    const auto observedEnvelope = PartyQuestVerificationPolicy::BuildExpected(
        m_active->Actions,
        digest,
        m_active->ExpectedVerification.CompatibilityFingerprint);
    if (!observedEnvelope || *observedEnvelope != m_active->ExpectedVerification)
    {
        m_active->LastObservedDigest = digest;
        m_active->StableCanonicalSamples = 0;
        return PartyQuestRuntimeVerificationStatus::Diverged;
    }

    const uint64_t envelopeFingerprint = observedEnvelope->ComputeFingerprint();
    if (m_active->LastObservedDigest == envelopeFingerprint)
        ++m_active->StableCanonicalSamples;
    else
    {
        m_active->LastObservedDigest = envelopeFingerprint;
        m_active->StableCanonicalSamples = 1;
    }

    if (m_active->StableCanonicalSamples < 2)
        return PartyQuestRuntimeVerificationStatus::NeedsStableSample;

    m_active->State = PartyQuestRuntimeApplyState::ReadyToCommit;
    return PartyQuestRuntimeVerificationStatus::Stable;
}

bool PartyQuestRuntimeApplyCoordinator::Commit(uint64_t aTransactionId) noexcept
{
    if (!m_active ||
        m_active->TransactionId != aTransactionId ||
        m_active->State != PartyQuestRuntimeApplyState::ReadyToCommit ||
        !m_active->SaveGuardActive ||
        !m_active->CheckpointCreated ||
        !m_active->RuntimeMutationMayHaveOccurred)
    {
        return false;
    }

    m_committed.emplace(aTransactionId, FingerprintActive(*m_active));
    m_active.reset();
    return true;
}

bool PartyQuestRuntimeApplyCoordinator::Abort(uint64_t aTransactionId) noexcept
{
    if (!m_active || m_active->TransactionId != aTransactionId)
        return false;

    m_lastAbortRequiresCheckpointRestore =
        m_active->CheckpointCreated && m_active->RuntimeMutationMayHaveOccurred;
    m_active.reset();
    return true;
}

PartyQuestRuntimeRecoveryState PartyQuestRuntimeApplyCoordinator::ExportRecoveryState(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) const
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = acCampaignId;
    state.PlayerProfileId = acPlayerProfileId;
    state.Committed.reserve(m_committed.size());

    for (const auto& [transactionId, fingerprint] : m_committed)
    {
        state.Committed.push_back({
            transactionId,
            fingerprint.TargetWorldRevision,
            fingerprint.QuestId,
            fingerprint.CanonicalDigest,
            fingerprint.SidecarManifestFingerprint,
            fingerprint.Actions,
            fingerprint.ExpectedVerification});
    }

    std::sort(state.Committed.begin(), state.Committed.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.TransactionId < acRight.TransactionId;
    });

    if (m_recoveryRecord)
        state.Active = m_recoveryRecord;
    else if (m_active)
        state.Active = m_active;

    return state;
}

PartyQuestRuntimeRecoveryDisposition PartyQuestRuntimeApplyCoordinator::RestoreRecoveryState(
    const PartyQuestRuntimeRecoveryState& acState,
    const PartyQuestCampaignId& acExpectedCampaignId,
    const PartyQuestPlayerProfileId& acExpectedPlayerProfileId) noexcept
{
    if (m_active || m_recoveryRecord || !m_committed.empty() || m_recoveryBlocked)
        return PartyQuestRuntimeRecoveryDisposition::InvalidState;

    if (!acExpectedCampaignId.IsValid() ||
        !acState.CampaignId.IsValid() ||
        !acExpectedPlayerProfileId.IsValid() ||
        !acState.PlayerProfileId.IsValid())
    {
        return PartyQuestRuntimeRecoveryDisposition::InvalidState;
    }

    if (acState.CampaignId != acExpectedCampaignId)
        return PartyQuestRuntimeRecoveryDisposition::CampaignMismatch;

    if (acState.PlayerProfileId != acExpectedPlayerProfileId)
        return PartyQuestRuntimeRecoveryDisposition::PlayerProfileMismatch;

    std::unordered_set<uint64_t> transactionIds;
    transactionIds.reserve(acState.Committed.size() + (acState.Active ? 1 : 0));

    for (const PartyQuestRuntimeCommittedRecord& record : acState.Committed)
    {
        if (record.TransactionId == 0 ||
            record.TargetWorldRevision == 0 ||
            !record.QuestId ||
            record.CanonicalDigest == 0 ||
            record.SidecarManifestFingerprint == 0 ||
            record.Actions == PartyQuestApplyAction::None ||
            !PartyQuestVerificationPolicy::IsCompleteForActions(
                record.ExpectedVerification,
                record.Actions) ||
            record.ExpectedVerification.QuestSnapshotDigest != record.CanonicalDigest ||
            !transactionIds.emplace(record.TransactionId).second)
        {
            m_committed.clear();
            return PartyQuestRuntimeRecoveryDisposition::InvalidState;
        }

        Fingerprint fingerprint;
        fingerprint.QuestId = record.QuestId;
        fingerprint.TargetWorldRevision = record.TargetWorldRevision;
        fingerprint.CanonicalDigest = record.CanonicalDigest;
        fingerprint.SidecarManifestFingerprint = record.SidecarManifestFingerprint;
        fingerprint.Actions = record.Actions;
        fingerprint.ExpectedVerification = record.ExpectedVerification;
        m_committed.emplace(record.TransactionId, fingerprint);
    }

    if (!acState.Active)
        return PartyQuestRuntimeRecoveryDisposition::Clean;

    const PartyQuestRuntimeApplyEntry& active = *acState.Active;
    if (!ValidateRecoveryEntry(active) || !transactionIds.emplace(active.TransactionId).second)
    {
        m_committed.clear();
        return PartyQuestRuntimeRecoveryDisposition::InvalidState;
    }

    if (active.State == PartyQuestRuntimeApplyState::DeferredWorld)
    {
        m_active = active;
        return PartyQuestRuntimeRecoveryDisposition::DeferredRestored;
    }

    if (active.RuntimeMutationMayHaveOccurred)
    {
        // After process restart the old in-memory save guard no longer exists.
        // Fail closed and require the external checkpoint to be restored before
        // any new canonical mutation is considered.
        m_recoveryRecord = active;
        m_recoveryBlocked = true;
        return PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired;
    }

    // No runtime mutation was dispatched. Discard the stale pre-mutation entry
    // and let the server produce a fresh current-canonical plan.
    return PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired;
}

bool PartyQuestRuntimeApplyCoordinator::AcknowledgeCheckpointRestored(uint64_t aTransactionId) noexcept
{
    if (!m_recoveryBlocked ||
        !m_recoveryRecord ||
        m_recoveryRecord->TransactionId != aTransactionId)
    {
        return false;
    }

    m_recoveryRecord.reset();
    m_recoveryBlocked = false;
    return true;
}

const PartyQuestRuntimeApplyEntry* PartyQuestRuntimeApplyCoordinator::GetActive() const noexcept
{
    return m_active ? &*m_active : nullptr;
}

bool PartyQuestRuntimeApplyCoordinator::IsCommitted(uint64_t aTransactionId) const noexcept
{
    return m_committed.contains(aTransactionId);
}

bool PartyQuestRuntimeApplyCoordinator::IsSaveGuardActive() const noexcept
{
    return m_active && m_active->SaveGuardActive;
}
