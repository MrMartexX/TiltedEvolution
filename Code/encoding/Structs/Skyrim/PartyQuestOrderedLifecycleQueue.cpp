#include <Structs/Skyrim/PartyQuestOrderedLifecycleQueue.h>

#include <limits>
#include <utility>

namespace
{
[[nodiscard]] PartyQuestOrderedLifecycleClaim ToClaim(
    const PartyQuestOrderedLifecycleEpoch& acEpoch) noexcept
{
    return {
        acEpoch.Sequence,
        acEpoch.Revision,
        acEpoch.Evidence,
        acEpoch.Action};
}
}

PartyQuestOrderedLifecycleEnqueueResult
PartyQuestOrderedLifecycleQueue::Enqueue(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    PartyQuestOrderedLifecycleEnqueueResult result;

    if (!IsValidReason(aReason))
    {
        result.Status = PartyQuestOrderedLifecycleEnqueueStatus::InvalidReason;
        return result;
    }
    if (m_terminal)
    {
        result.Status = PartyQuestOrderedLifecycleEnqueueStatus::TerminalClosed;
        return result;
    }
    if (m_exhausted || m_nextSequence == 0u || m_nextRevision == 0u)
    {
        m_exhausted = true;
        result.Status =
            PartyQuestOrderedLifecycleEnqueueStatus::CounterExhausted;
        return result;
    }

    if (aReason == PartyQuestOrderedLifecycleReason::Shutdown)
    {
        PartyQuestOrderedLifecycleEpoch shutdown;
        shutdown.Sequence = m_nextSequence;
        shutdown.Revision = m_nextRevision;
        shutdown.Evidence = static_cast<PartyQuestOrderedLifecycleEvidenceMask>(
            AccumulatedEvidence() |
            PartyQuestOrderedLifecycleEvidenceFor(aReason));
        shutdown.Action = PartyQuestOrderedLifecycleAction::ApplyShutdown;

        try
        {
            std::vector<PartyQuestOrderedLifecycleEpoch> replacement;
            replacement.reserve(1u);
            replacement.push_back(shutdown);
            m_epochs.swap(replacement);
        }
        catch (...)
        {
            result.Status =
                PartyQuestOrderedLifecycleEnqueueStatus::AllocationFailed;
            return result;
        }

        CommitNewEpochCounters();
        m_activeClaim.reset();
        m_terminal = true;
        result.Status =
            PartyQuestOrderedLifecycleEnqueueStatus::TerminalQueued;
        result.Epoch = shutdown;
        return result;
    }

    const auto evidence = PartyQuestOrderedLifecycleEvidenceFor(aReason);
    const bool tailClaimed = m_activeClaim && !m_epochs.empty() &&
        ClaimMatchesEpoch(*m_activeClaim, m_epochs.back());

    if (IsReleaseReason(aReason) && !m_epochs.empty() && !tailClaimed &&
        IsReleaseAction(m_epochs.back().Action))
    {
        auto& tail = m_epochs.back();
        const auto mergedEvidence =
            static_cast<PartyQuestOrderedLifecycleEvidenceMask>(
                tail.Evidence | evidence);
        const auto mergedAction = MergeReleaseAction(tail.Action, aReason);

        if (mergedEvidence == tail.Evidence && mergedAction == tail.Action)
        {
            result.Status =
                PartyQuestOrderedLifecycleEnqueueStatus::Duplicate;
            result.Epoch = tail;
            return result;
        }

        if (m_nextRevision == 0u)
        {
            m_exhausted = true;
            result.Status =
                PartyQuestOrderedLifecycleEnqueueStatus::CounterExhausted;
            return result;
        }

        tail.Revision = m_nextRevision;
        tail.Evidence = mergedEvidence;
        tail.Action = mergedAction;
        CommitRevisionCounter();

        result.Status =
            PartyQuestOrderedLifecycleEnqueueStatus::Coalesced;
        result.Epoch = tail;
        return result;
    }

    PartyQuestOrderedLifecycleEpoch epoch;
    epoch.Sequence = m_nextSequence;
    epoch.Revision = m_nextRevision;
    epoch.Evidence = evidence;
    epoch.Action = ActionForReason(aReason);

    try
    {
        m_epochs.push_back(epoch);
    }
    catch (...)
    {
        result.Status =
            PartyQuestOrderedLifecycleEnqueueStatus::AllocationFailed;
        return result;
    }

    CommitNewEpochCounters();
    result.Status = PartyQuestOrderedLifecycleEnqueueStatus::Queued;
    result.Epoch = epoch;
    return result;
}

std::optional<PartyQuestOrderedLifecycleClaim>
PartyQuestOrderedLifecycleQueue::TryClaimFront() noexcept
{
    if (m_activeClaim || m_epochs.empty())
        return std::nullopt;

    m_activeClaim = ToClaim(m_epochs.front());
    return m_activeClaim;
}

bool PartyQuestOrderedLifecycleQueue::IsCurrent(
    const PartyQuestOrderedLifecycleClaim& acClaim) const noexcept
{
    return m_activeClaim && !m_epochs.empty() &&
        SameClaim(*m_activeClaim, acClaim) &&
        ClaimMatchesEpoch(acClaim, m_epochs.front());
}

bool PartyQuestOrderedLifecycleQueue::Acknowledge(
    const PartyQuestOrderedLifecycleClaim& acClaim) noexcept
{
    if (!IsCurrent(acClaim))
        return false;

    m_epochs.erase(m_epochs.begin());
    m_activeClaim.reset();
    return true;
}

bool PartyQuestOrderedLifecycleQueue::Retry(
    const PartyQuestOrderedLifecycleClaim& acClaim) noexcept
{
    if (!IsCurrent(acClaim))
        return false;

    m_activeClaim.reset();
    return true;
}

bool PartyQuestOrderedLifecycleQueue::IsValidReason(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    switch (aReason)
    {
    case PartyQuestOrderedLifecycleReason::Connected:
    case PartyQuestOrderedLifecycleReason::PartyJoined:
    case PartyQuestOrderedLifecycleReason::PartyLeft:
    case PartyQuestOrderedLifecycleReason::CampaignSwitch:
    case PartyQuestOrderedLifecycleReason::Disconnect:
    case PartyQuestOrderedLifecycleReason::LoadGame:
    case PartyQuestOrderedLifecycleReason::Shutdown:
        return true;
    }

    return false;
}

bool PartyQuestOrderedLifecycleQueue::IsReleaseReason(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    return aReason == PartyQuestOrderedLifecycleReason::PartyLeft ||
        aReason == PartyQuestOrderedLifecycleReason::CampaignSwitch ||
        aReason == PartyQuestOrderedLifecycleReason::Disconnect;
}

bool PartyQuestOrderedLifecycleQueue::IsReleaseAction(
    PartyQuestOrderedLifecycleAction aAction) noexcept
{
    return aAction == PartyQuestOrderedLifecycleAction::ReleasePartyLeft ||
        aAction ==
            PartyQuestOrderedLifecycleAction::ReleaseCampaignSwitch ||
        aAction == PartyQuestOrderedLifecycleAction::ReleaseDisconnect;
}

PartyQuestOrderedLifecycleAction
PartyQuestOrderedLifecycleQueue::ActionForReason(
    PartyQuestOrderedLifecycleReason aReason) noexcept
{
    switch (aReason)
    {
    case PartyQuestOrderedLifecycleReason::Connected:
        return PartyQuestOrderedLifecycleAction::ApplyConnectedBoundary;
    case PartyQuestOrderedLifecycleReason::PartyJoined:
        return PartyQuestOrderedLifecycleAction::ApplyPartyJoinedBoundary;
    case PartyQuestOrderedLifecycleReason::PartyLeft:
        return PartyQuestOrderedLifecycleAction::ReleasePartyLeft;
    case PartyQuestOrderedLifecycleReason::CampaignSwitch:
        return PartyQuestOrderedLifecycleAction::ReleaseCampaignSwitch;
    case PartyQuestOrderedLifecycleReason::Disconnect:
        return PartyQuestOrderedLifecycleAction::ReleaseDisconnect;
    case PartyQuestOrderedLifecycleReason::LoadGame:
        return PartyQuestOrderedLifecycleAction::RetireBlockedLoadAttempt;
    case PartyQuestOrderedLifecycleReason::Shutdown:
        return PartyQuestOrderedLifecycleAction::ApplyShutdown;
    }

    return PartyQuestOrderedLifecycleAction::ApplyShutdown;
}

PartyQuestOrderedLifecycleAction
PartyQuestOrderedLifecycleQueue::MergeReleaseAction(
    PartyQuestOrderedLifecycleAction aCurrent,
    PartyQuestOrderedLifecycleReason aIncoming) noexcept
{
    if (aCurrent == PartyQuestOrderedLifecycleAction::ReleaseDisconnect ||
        aIncoming == PartyQuestOrderedLifecycleReason::Disconnect)
    {
        return PartyQuestOrderedLifecycleAction::ReleaseDisconnect;
    }

    if (aCurrent ==
            PartyQuestOrderedLifecycleAction::ReleaseCampaignSwitch ||
        aIncoming == PartyQuestOrderedLifecycleReason::CampaignSwitch)
    {
        return PartyQuestOrderedLifecycleAction::ReleaseCampaignSwitch;
    }

    return PartyQuestOrderedLifecycleAction::ReleasePartyLeft;
}

bool PartyQuestOrderedLifecycleQueue::SameClaim(
    const PartyQuestOrderedLifecycleClaim& acLeft,
    const PartyQuestOrderedLifecycleClaim& acRight) noexcept
{
    return acLeft.Sequence == acRight.Sequence &&
        acLeft.Revision == acRight.Revision &&
        acLeft.Evidence == acRight.Evidence &&
        acLeft.Action == acRight.Action;
}

bool PartyQuestOrderedLifecycleQueue::ClaimMatchesEpoch(
    const PartyQuestOrderedLifecycleClaim& acClaim,
    const PartyQuestOrderedLifecycleEpoch& acEpoch) noexcept
{
    return acClaim.Sequence == acEpoch.Sequence &&
        acClaim.Revision == acEpoch.Revision &&
        acClaim.Evidence == acEpoch.Evidence &&
        acClaim.Action == acEpoch.Action;
}

void PartyQuestOrderedLifecycleQueue::CommitNewEpochCounters() noexcept
{
    if (m_nextSequence == std::numeric_limits<uint64_t>::max())
    {
        m_nextSequence = 0u;
        m_exhausted = true;
    }
    else
    {
        ++m_nextSequence;
    }

    CommitRevisionCounter();
}

void PartyQuestOrderedLifecycleQueue::CommitRevisionCounter() noexcept
{
    if (m_nextRevision == std::numeric_limits<uint64_t>::max())
    {
        m_nextRevision = 0u;
        m_exhausted = true;
    }
    else
    {
        ++m_nextRevision;
    }
}

PartyQuestOrderedLifecycleEvidenceMask
PartyQuestOrderedLifecycleQueue::AccumulatedEvidence() const noexcept
{
    PartyQuestOrderedLifecycleEvidenceMask evidence{};
    for (const auto& epoch : m_epochs)
        evidence = static_cast<PartyQuestOrderedLifecycleEvidenceMask>(
            evidence | epoch.Evidence);
    return evidence;
}
