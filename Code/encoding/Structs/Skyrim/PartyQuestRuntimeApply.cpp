#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

namespace
{
bool RequiresWorldTargets(const PartyQuestApplyPlan& acPlan) noexcept
{
    return HasPartyQuestApplyAction(acPlan.Actions, PartyQuestApplyAction::WaitForWorldTargets);
}
} // namespace

std::optional<PartyQuestRuntimeApplyCoordinator::Fingerprint>
PartyQuestRuntimeApplyCoordinator::ValidateAndFingerprint(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    if (acRequest.TransactionId == 0 ||
        acRequest.TargetWorldRevision == 0 ||
        !acRequest.CanonicalSnapshot.QuestId ||
        acRequest.CanonicalSnapshot.Revision == 0)
    {
        return std::nullopt;
    }

    if (acRequest.Plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence) ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::ResnapshotAndVerify))
    {
        return std::nullopt;
    }

    QuestSnapshot canonical = acRequest.CanonicalSnapshot;
    canonical.Canonicalize();

    Fingerprint fingerprint;
    fingerprint.QuestId = canonical.QuestId;
    fingerprint.TargetWorldRevision = acRequest.TargetWorldRevision;
    fingerprint.CanonicalDigest = canonical.ComputeDigest();
    fingerprint.Actions = acRequest.Plan.Actions;
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
    fingerprint.Actions = acEntry.Actions;
    return fingerprint;
}

PartyQuestRuntimeApplyBeginStatus PartyQuestRuntimeApplyCoordinator::Begin(
    const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    m_lastAbortRequiresCheckpointRestore = false;

    if (acRequest.TransactionId == 0 ||
        acRequest.TargetWorldRevision == 0 ||
        !acRequest.CanonicalSnapshot.QuestId ||
        acRequest.CanonicalSnapshot.Revision == 0)
    {
        return PartyQuestRuntimeApplyBeginStatus::InvalidRequest;
    }

    if (acRequest.Plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::WaitForPapyrusQuiescence) ||
        !HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::ResnapshotAndVerify))
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
    entry.Actions = fingerprint->Actions;

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
    if (digest != m_active->CanonicalDigest)
    {
        m_active->LastObservedDigest = digest;
        m_active->StableCanonicalSamples = 0;
        return PartyQuestRuntimeVerificationStatus::Diverged;
    }

    if (m_active->LastObservedDigest == digest)
        ++m_active->StableCanonicalSamples;
    else
    {
        m_active->LastObservedDigest = digest;
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
