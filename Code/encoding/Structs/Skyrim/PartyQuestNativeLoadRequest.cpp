#include <Structs/Skyrim/PartyQuestNativeLoadRequest.h>

#include <cstring>
#include <limits>

PartyQuestNativeLoadBeginResult PartyQuestNativeLoadRequest::Begin(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
{
    PartyQuestNativeLoadBeginResult result;

    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
    {
        result.Status = PartyQuestNativeLoadRequestStatus::Poisoned;
        return result;
    }

    if (!IsValidIdentity(acIdentity))
    {
        result.Status = PartyQuestNativeLoadRequestStatus::InvalidIdentity;
        return result;
    }

    if (m_state != PartyQuestNativeLoadRequestState::Idle &&
        m_state != PartyQuestNativeLoadRequestState::Retired)
    {
        result.Status = PartyQuestNativeLoadRequestStatus::InvalidState;
        return result;
    }

    if (m_lastAttemptNonce == std::numeric_limits<uint64_t>::max())
    {
        Poison();
        result.Status = PartyQuestNativeLoadRequestStatus::CounterExhausted;
        return result;
    }

    ++m_lastAttemptNonce;
    m_currentAttemptNonce = m_lastAttemptNonce;
    StoreIdentity(acIdentity);
    m_completion = {};
    m_targetEntered = 0u;
    m_state = PartyQuestNativeLoadRequestState::Reserved;

    result.Status = PartyQuestNativeLoadRequestStatus::Begun;
    result.HasReservation = 1u;
    result.Reservation.AttemptNonce = m_currentAttemptNonce;
    result.Reservation.Identity = m_identity;
    return result;
}

PartyQuestNativeLoadRequestStatus PartyQuestNativeLoadRequest::Cancel(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
        return PartyQuestNativeLoadRequestStatus::Poisoned;

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
        return nonceStatus;

    if (m_state == PartyQuestNativeLoadRequestState::Reserved)
    {
        m_targetEntered = 0u;
        m_state = PartyQuestNativeLoadRequestState::Retired;
        return PartyQuestNativeLoadRequestStatus::Cancelled;
    }

    if (m_state == PartyQuestNativeLoadRequestState::Retired)
        return PartyQuestNativeLoadRequestStatus::Duplicate;

    return PartyQuestNativeLoadRequestStatus::InvalidState;
}

PartyQuestNativeLoadRequestStatus PartyQuestNativeLoadRequest::Claim(
    uint64_t aAttemptNonce,
    const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept
{
    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
        return PartyQuestNativeLoadRequestStatus::Poisoned;

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
        return nonceStatus;

    if (!IsValidIdentity(acActualIdentity))
        return PartyQuestNativeLoadRequestStatus::InvalidIdentity;

    if (!IdentityEquals(m_identity, acActualIdentity))
        return PartyQuestNativeLoadRequestStatus::IdentityMismatch;

    if (m_state == PartyQuestNativeLoadRequestState::Reserved)
    {
        m_targetEntered = 0u;
        m_state = PartyQuestNativeLoadRequestState::Claimed;
        return PartyQuestNativeLoadRequestStatus::Claimed;
    }

    if (m_state == PartyQuestNativeLoadRequestState::Claimed)
        return PartyQuestNativeLoadRequestStatus::Duplicate;

    return PartyQuestNativeLoadRequestStatus::InvalidState;
}

PartyQuestNativeLoadRequestStatus
PartyQuestNativeLoadRequest::MarkTargetEntered(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
        return PartyQuestNativeLoadRequestStatus::Poisoned;

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
        return nonceStatus;

    if (m_state == PartyQuestNativeLoadRequestState::Claimed)
    {
        if (m_targetEntered != 0u)
            return PartyQuestNativeLoadRequestStatus::Duplicate;
        m_targetEntered = 1u;
        return PartyQuestNativeLoadRequestStatus::TargetEntered;
    }

    if (m_state == PartyQuestNativeLoadRequestState::Completed)
        return PartyQuestNativeLoadRequestStatus::Duplicate;

    if (m_state == PartyQuestNativeLoadRequestState::Retired &&
        m_completion.EventSequence != 0u)
    {
        return PartyQuestNativeLoadRequestStatus::Duplicate;
    }

    return PartyQuestNativeLoadRequestStatus::InvalidState;
}

PartyQuestNativeLoadRequestStatus PartyQuestNativeLoadRequest::Complete(
    uint64_t aAttemptNonce,
    bool aResult) noexcept
{
    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
        return PartyQuestNativeLoadRequestStatus::Poisoned;

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
        return nonceStatus;

    if (m_state == PartyQuestNativeLoadRequestState::Completed)
        return PartyQuestNativeLoadRequestStatus::Duplicate;

    if (m_state == PartyQuestNativeLoadRequestState::Retired &&
        m_completion.EventSequence != 0u)
    {
        return PartyQuestNativeLoadRequestStatus::Duplicate;
    }

    if (m_state != PartyQuestNativeLoadRequestState::Claimed ||
        m_targetEntered == 0u)
    {
        return PartyQuestNativeLoadRequestStatus::InvalidState;
    }

    if (m_lastEventSequence == std::numeric_limits<uint64_t>::max())
    {
        Poison();
        return PartyQuestNativeLoadRequestStatus::CounterExhausted;
    }

    ++m_lastEventSequence;
    m_completion = {};
    m_completion.AttemptNonce = m_currentAttemptNonce;
    m_completion.EventSequence = m_lastEventSequence;
    m_completion.Identity = m_identity;
    m_completion.Result = aResult ? 1u : 0u;
    m_state = PartyQuestNativeLoadRequestState::Completed;
    return PartyQuestNativeLoadRequestStatus::Completed;
}

PartyQuestNativeLoadPollResult PartyQuestNativeLoadRequest::Poll(
    uint64_t aAttemptNonce) const noexcept
{
    PartyQuestNativeLoadPollResult result;

    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
    {
        result.Status = PartyQuestNativeLoadRequestStatus::Poisoned;
        return result;
    }

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
    {
        result.Status = nonceStatus;
        return result;
    }

    if (m_state != PartyQuestNativeLoadRequestState::Completed)
    {
        result.Status = PartyQuestNativeLoadRequestStatus::InvalidState;
        return result;
    }

    result.Status = PartyQuestNativeLoadRequestStatus::CompletionAvailable;
    result.HasCompletion = 1u;
    result.Completion = m_completion;
    return result;
}

PartyQuestNativeLoadRequestStatus PartyQuestNativeLoadRequest::Retire(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state == PartyQuestNativeLoadRequestState::Poisoned)
        return PartyQuestNativeLoadRequestStatus::Poisoned;

    const auto nonceStatus = ClassifyNonce(aAttemptNonce);
    if (nonceStatus != PartyQuestNativeLoadRequestStatus::Begun)
        return nonceStatus;

    if (m_state == PartyQuestNativeLoadRequestState::Completed)
    {
        m_targetEntered = 0u;
        m_state = PartyQuestNativeLoadRequestState::Retired;
        return PartyQuestNativeLoadRequestStatus::Retired;
    }

    if (m_state == PartyQuestNativeLoadRequestState::Retired)
        return PartyQuestNativeLoadRequestStatus::Duplicate;

    return PartyQuestNativeLoadRequestStatus::InvalidState;
}

bool PartyQuestNativeLoadRequest::IsValidIdentity(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
{
    return acIdentity.Length != 0u &&
        acIdentity.Length <= PartyQuestNativeLoadIdentity::kCapacity;
}

bool PartyQuestNativeLoadRequest::IdentityEquals(
    const PartyQuestNativeLoadIdentity& acLeft,
    const PartyQuestNativeLoadIdentity& acRight) noexcept
{
    return acLeft.Length == acRight.Length &&
        acLeft.Length != 0u &&
        std::memcmp(
            acLeft.Bytes,
            acRight.Bytes,
            acLeft.Length) == 0;
}

PartyQuestNativeLoadRequestStatus PartyQuestNativeLoadRequest::ClassifyNonce(
    uint64_t aAttemptNonce) const noexcept
{
    if (aAttemptNonce == m_currentAttemptNonce &&
        m_currentAttemptNonce != 0u)
    {
        return PartyQuestNativeLoadRequestStatus::Begun;
    }

    if (aAttemptNonce != 0u &&
        m_currentAttemptNonce != 0u &&
        aAttemptNonce < m_currentAttemptNonce)
    {
        return PartyQuestNativeLoadRequestStatus::StaleNonce;
    }

    return PartyQuestNativeLoadRequestStatus::NonceMismatch;
}

void PartyQuestNativeLoadRequest::StoreIdentity(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
{
    m_identity = {};
    m_identity.Length = acIdentity.Length;
    std::memcpy(
        m_identity.Bytes,
        acIdentity.Bytes,
        acIdentity.Length);
}

void PartyQuestNativeLoadRequest::Poison() noexcept
{
    m_targetEntered = 0u;
    m_state = PartyQuestNativeLoadRequestState::Poisoned;
}
