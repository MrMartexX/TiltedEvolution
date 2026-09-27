#include <Structs/Skyrim/PartyQuestNativeLoadBridgeAdapter.h>

#include <cstring>

bool PartyQuestNativeLoadBridgeAdapter::PublishReady(
    uint64_t aRuntimeFingerprint) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::NotReady ||
        aRuntimeFingerprint == 0u)
    {
        return false;
    }

    m_runtimeFingerprint = aRuntimeFingerprint;
    m_state = PartyQuestNativeLoadBridgeAdapterState::Ready;
    return true;
}

void PartyQuestNativeLoadBridgeAdapter::Poison() noexcept
{
    m_state = PartyQuestNativeLoadBridgeAdapterState::Poisoned;
}

PartyQuestNativeLoadBridgeDescriptorResult
PartyQuestNativeLoadBridgeAdapter::GetDescriptor(
    PartyQuestNativeLoadBridgeDescriptorV1& aDescriptor) const noexcept
{
    aDescriptor = {};
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return PartyQuestNativeLoadBridgeDescriptorResult::Unavailable;

    aDescriptor.AbiVersion = kPartyQuestNativeLoadBridgeDescriptorAbi;
    aDescriptor.StructSize = sizeof(aDescriptor);
    aDescriptor.PayloadAbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    aDescriptor.ImplementationVersion =
        kPartyQuestNativeLoadBridgeImplementationVersion;
    aDescriptor.Capabilities =
        kPartyQuestRequiredNativeLoadBridgeCapabilities;
    aDescriptor.RuntimeMajor = 1u;
    aDescriptor.RuntimeMinor = 6u;
    aDescriptor.RuntimePatch = 1170u;
    aDescriptor.RuntimeBuild = 0u;
    aDescriptor.RuntimeFingerprint = m_runtimeFingerprint;
    aDescriptor.BridgeFingerprint = kPartyQuestNativeLoadBridgeFingerprint;
    return PartyQuestNativeLoadBridgeDescriptorResult::Available;
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Reserve(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
    PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept
{
    aReservation = {};
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    if (acRequest.AbiVersion != kPartyQuestNativeLoadBridgePayloadAbi)
        return PartyQuestNativeLoadBridgeStatus::UnsupportedAbi;
    if (acRequest.StructSize != sizeof(acRequest))
        return PartyQuestNativeLoadBridgeStatus::InvalidStructSize;
    if (!PartyQuestNativeLoadBridgePolicy::IsValidIdentity(acRequest.Identity))
        return PartyQuestNativeLoadBridgeStatus::InvalidIdentity;

    const auto result = m_request.Begin(ToInternalIdentity(acRequest.Identity));
    switch (result.Status)
    {
    case PartyQuestNativeLoadRequestStatus::Begun:
        return AcceptReservation(acRequest, result, aReservation);
    case PartyQuestNativeLoadRequestStatus::InvalidIdentity:
        return FailInternal();
    case PartyQuestNativeLoadRequestStatus::InvalidState:
        return PartyQuestNativeLoadBridgeStatus::InvalidState;
    case PartyQuestNativeLoadRequestStatus::CounterExhausted:
        Poison();
        return PartyQuestNativeLoadBridgeStatus::CounterExhausted;
    case PartyQuestNativeLoadRequestStatus::Poisoned:
        Poison();
        return PartyQuestNativeLoadBridgeStatus::Poisoned;
    default:
        Poison();
        return PartyQuestNativeLoadBridgeStatus::InternalFailure;
    }
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Cancel(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    return MapOperationStatus(
        Operation::Cancel, m_request.Cancel(aAttemptNonce));
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Poll(
    uint64_t aAttemptNonce,
    PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept
{
    aCompletion = {};
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();

    const auto result = m_request.Poll(aAttemptNonce);
    if (result.Status ==
        PartyQuestNativeLoadRequestStatus::CompletionAvailable)
        return AcceptCompletion(aAttemptNonce, result, aCompletion);
    if (result.Status == PartyQuestNativeLoadRequestStatus::InvalidState)
    {
        switch (m_request.GetState())
        {
        case PartyQuestNativeLoadRequestState::Reserved:
        case PartyQuestNativeLoadRequestState::Claimed:
            return PartyQuestNativeLoadBridgeStatus::Pending;
        case PartyQuestNativeLoadRequestState::Retired:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    }
    return MapOperationStatus(Operation::Poll, result.Status);
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Retire(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    return MapOperationStatus(
        Operation::Retire, m_request.Retire(aAttemptNonce));
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Claim(
    uint64_t aAttemptNonce,
    const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    return MapOperationStatus(
        Operation::Claim,
        m_request.Claim(aAttemptNonce, acActualIdentity));
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::MarkTargetEntered(
    uint64_t aAttemptNonce) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    return MapOperationStatus(
        Operation::MarkTargetEntered,
        m_request.MarkTargetEntered(aAttemptNonce));
}

PartyQuestNativeLoadBridgeStatus PartyQuestNativeLoadBridgeAdapter::Complete(
    uint64_t aAttemptNonce,
    bool aResult) noexcept
{
    if (m_state != PartyQuestNativeLoadBridgeAdapterState::Ready)
        return UnavailableStatus();
    return MapOperationStatus(
        Operation::Complete,
        m_request.Complete(aAttemptNonce, aResult));
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::MapOperationStatus(
    Operation aOperation,
    PartyQuestNativeLoadRequestStatus aStatus) noexcept
{
    if (aStatus == PartyQuestNativeLoadRequestStatus::Poisoned)
    {
        Poison();
        return PartyQuestNativeLoadBridgeStatus::Poisoned;
    }

    if (aStatus == PartyQuestNativeLoadRequestStatus::CounterExhausted)
    {
        Poison();
        return PartyQuestNativeLoadBridgeStatus::CounterExhausted;
    }

    switch (aOperation)
    {
    case Operation::Cancel:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::Cancelled:
            return PartyQuestNativeLoadBridgeStatus::Cancelled;
        case PartyQuestNativeLoadRequestStatus::Duplicate:
            return PartyQuestNativeLoadBridgeStatus::Duplicate;
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        case PartyQuestNativeLoadRequestStatus::InvalidState:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    case Operation::Poll:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        default:
            return FailInternal();
        }
    case Operation::Retire:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::Retired:
            return PartyQuestNativeLoadBridgeStatus::Retired;
        case PartyQuestNativeLoadRequestStatus::Duplicate:
            return PartyQuestNativeLoadBridgeStatus::Duplicate;
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        case PartyQuestNativeLoadRequestStatus::InvalidState:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    case Operation::Claim:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::Claimed:
            return PartyQuestNativeLoadBridgeStatus::Pending;
        case PartyQuestNativeLoadRequestStatus::Duplicate:
            return PartyQuestNativeLoadBridgeStatus::Duplicate;
        case PartyQuestNativeLoadRequestStatus::InvalidIdentity:
            return PartyQuestNativeLoadBridgeStatus::InvalidIdentity;
        case PartyQuestNativeLoadRequestStatus::IdentityMismatch:
            return PartyQuestNativeLoadBridgeStatus::IdentityMismatch;
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        case PartyQuestNativeLoadRequestStatus::InvalidState:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    case Operation::MarkTargetEntered:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::TargetEntered:
            return PartyQuestNativeLoadBridgeStatus::Pending;
        case PartyQuestNativeLoadRequestStatus::Duplicate:
            return PartyQuestNativeLoadBridgeStatus::Duplicate;
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        case PartyQuestNativeLoadRequestStatus::InvalidState:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    case Operation::Complete:
        switch (aStatus)
        {
        case PartyQuestNativeLoadRequestStatus::Completed:
            return PartyQuestNativeLoadBridgeStatus::Pending;
        case PartyQuestNativeLoadRequestStatus::Duplicate:
            return PartyQuestNativeLoadBridgeStatus::Duplicate;
        case PartyQuestNativeLoadRequestStatus::NonceMismatch:
            return PartyQuestNativeLoadBridgeStatus::NonceMismatch;
        case PartyQuestNativeLoadRequestStatus::StaleNonce:
            return PartyQuestNativeLoadBridgeStatus::StaleNonce;
        case PartyQuestNativeLoadRequestStatus::InvalidState:
            return PartyQuestNativeLoadBridgeStatus::InvalidState;
        default:
            return FailInternal();
        }
    }

    return FailInternal();
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::FailInternal() noexcept
{
    Poison();
    return PartyQuestNativeLoadBridgeStatus::InternalFailure;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::AcceptReservation(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest,
    const PartyQuestNativeLoadBeginResult& acResult,
    PartyQuestNativeLoadBridgeReservationV1& aReservation) noexcept
{
    const auto expectedIdentity = ToInternalIdentity(acRequest.Identity);
    if (acResult.HasReservation != 1u ||
        acResult.Reservation.AttemptNonce == 0u ||
        acResult.Reservation.AttemptNonce !=
            m_request.GetCurrentAttemptNonce() ||
        !IdentityEquals(acResult.Reservation.Identity, expectedIdentity))
    {
        aReservation = {};
        return FailInternal();
    }

    aReservation.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    aReservation.StructSize = sizeof(aReservation);
    aReservation.AttemptNonce = acResult.Reservation.AttemptNonce;
    aReservation.Identity = ToBridgeIdentity(acResult.Reservation.Identity);
    if (!PartyQuestNativeLoadBridgePolicy::IsValidReservation(aReservation))
    {
        aReservation = {};
        return FailInternal();
    }

    m_activeIdentity = expectedIdentity;
    return PartyQuestNativeLoadBridgeStatus::Reserved;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::AcceptCompletion(
    uint64_t aAttemptNonce,
    const PartyQuestNativeLoadPollResult& acResult,
    PartyQuestNativeLoadBridgeCompletionV1& aCompletion) noexcept
{
    if (acResult.HasCompletion != 1u ||
        acResult.Completion.AttemptNonce != aAttemptNonce ||
        acResult.Completion.EventSequence == 0u ||
        !IdentityEquals(acResult.Completion.Identity, m_activeIdentity))
    {
        aCompletion = {};
        return FailInternal();
    }

    aCompletion.AbiVersion = kPartyQuestNativeLoadBridgePayloadAbi;
    aCompletion.StructSize = sizeof(aCompletion);
    aCompletion.AttemptNonce = acResult.Completion.AttemptNonce;
    aCompletion.EventSequence = acResult.Completion.EventSequence;
    aCompletion.Identity = ToBridgeIdentity(acResult.Completion.Identity);
    aCompletion.Result = acResult.Completion.Result;
    if (!PartyQuestNativeLoadBridgePolicy::IsValidCompletion(aCompletion))
    {
        aCompletion = {};
        return FailInternal();
    }

    return PartyQuestNativeLoadBridgeStatus::CompletionAvailable;
}

bool PartyQuestNativeLoadBridgeAdapter::IdentityEquals(
    const PartyQuestNativeLoadIdentity& acLeft,
    const PartyQuestNativeLoadIdentity& acRight) noexcept
{
    return acLeft.Length == acRight.Length &&
        acLeft.Length != 0u &&
        acLeft.Length <= PartyQuestNativeLoadIdentity::kCapacity &&
        std::memcmp(acLeft.Bytes, acRight.Bytes, acLeft.Length) == 0;
}

PartyQuestNativeLoadIdentity
PartyQuestNativeLoadBridgeAdapter::ToInternalIdentity(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    PartyQuestNativeLoadIdentity identity{};
    identity.Length = acIdentity.Length;
    if (identity.Length <= PartyQuestNativeLoadIdentity::kCapacity)
        std::memcpy(identity.Bytes, acIdentity.Bytes, identity.Length);
    return identity;
}

PartyQuestNativeLoadBridgeIdentityV1
PartyQuestNativeLoadBridgeAdapter::ToBridgeIdentity(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
{
    PartyQuestNativeLoadBridgeIdentityV1 identity{};
    identity.Length = acIdentity.Length;
    if (identity.Length <= PartyQuestNativeLoadIdentity::kCapacity)
        std::memcpy(identity.Bytes, acIdentity.Bytes, identity.Length);
    return identity;
}

PartyQuestNativeLoadBridgeStatus
PartyQuestNativeLoadBridgeAdapter::UnavailableStatus() const noexcept
{
    return m_state == PartyQuestNativeLoadBridgeAdapterState::Poisoned
        ? PartyQuestNativeLoadBridgeStatus::Poisoned
        : PartyQuestNativeLoadBridgeStatus::BridgeUnavailable;
}
