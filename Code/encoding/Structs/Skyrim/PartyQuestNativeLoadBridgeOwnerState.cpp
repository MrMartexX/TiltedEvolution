#include <Structs/Skyrim/PartyQuestNativeLoadBridgeOwnerState.h>

#include <cstring>
#include <limits>

bool PartyQuestNativeLoadBridgeOwnerState::AreZero(
    const uint8_t* apBytes,
    size_t aSize) noexcept
{
    if (!apBytes && aSize != 0u)
        return false;

    for (size_t index = 0u; index < aSize; ++index)
    {
        if (apBytes[index] != 0u)
            return false;
    }
    return true;
}

bool PartyQuestNativeLoadBridgeOwnerState::IdentityEquals(
    const PartyQuestNativeLoadBridgeIdentityV1& acLeft,
    const PartyQuestNativeLoadBridgeIdentityV1& acRight) noexcept
{
    return acLeft.Length == acRight.Length &&
        acLeft.Length != 0u &&
        std::memcmp(
            acLeft.Bytes,
            acRight.Bytes,
            acLeft.Length) == 0;
}

PartyQuestNativeLoadBridgeIdentityV1
PartyQuestNativeLoadBridgeOwnerState::CanonicalizeIdentity(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    PartyQuestNativeLoadBridgeIdentityV1 result{};
    result.Length = acIdentity.Length;
    if (result.Length != 0u &&
        result.Length <= PartyQuestNativeLoadIdentity::kCapacity)
    {
        std::memcpy(result.Bytes, acIdentity.Bytes, result.Length);
    }
    return result;
}

bool PartyQuestNativeLoadBridgeOwnerState::IsZeroIdentity(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    return acIdentity.Length == 0u &&
        acIdentity.Reserved0 == 0u &&
        AreZero(acIdentity.Bytes, sizeof(acIdentity.Bytes));
}

bool PartyQuestNativeLoadBridgeOwnerState::IsZeroReserveRequest(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest) noexcept
{
    return acRequest.AbiVersion == 0u &&
        acRequest.StructSize == 0u &&
        IsZeroIdentity(acRequest.Identity);
}

bool PartyQuestNativeLoadBridgeOwnerState::IsZeroReservation(
    const PartyQuestNativeLoadBridgeReservationV1& acReservation) noexcept
{
    return acReservation.AbiVersion == 0u &&
        acReservation.StructSize == 0u &&
        acReservation.AttemptNonce == 0u &&
        IsZeroIdentity(acReservation.Identity);
}

bool PartyQuestNativeLoadBridgeOwnerState::IsZeroCompletion(
    const PartyQuestNativeLoadBridgeCompletionV1& acCompletion) noexcept
{
    return acCompletion.AbiVersion == 0u &&
        acCompletion.StructSize == 0u &&
        acCompletion.AttemptNonce == 0u &&
        acCompletion.EventSequence == 0u &&
        IsZeroIdentity(acCompletion.Identity) &&
        acCompletion.Result == 0u &&
        AreZero(acCompletion.Reserved, sizeof(acCompletion.Reserved));
}

bool PartyQuestNativeLoadBridgeOwnerState::IsCommandShapeValid(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand,
    PartyQuestNativeLoadBridgeOwnerResultCode& aFailure) const noexcept
{
    aFailure = PartyQuestNativeLoadBridgeOwnerResultCode::InvalidCommand;

    if (!AreZero(acCommand.Reserved, sizeof(acCommand.Reserved)))
        return false;

    switch (acCommand.Kind)
    {
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Bind:
        if (acCommand.AttemptNonce != 0u ||
            !IsZeroIdentity(acCommand.Identity))
        {
            return false;
        }

        switch (acCommand.BindDisposition)
        {
        case PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected:
            return acCommand.RuntimeGeneration == 0u &&
                acCommand.RuntimeFingerprint == 0u;

        case PartyQuestNativeLoadBridgeOwnerBindDisposition::Authenticated:
            return acCommand.RuntimeGeneration != 0u &&
                acCommand.RuntimeFingerprint != 0u;
        }
        return false;

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve:
        if (acCommand.BindDisposition !=
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected ||
            acCommand.RuntimeGeneration != 0u ||
            acCommand.RuntimeFingerprint != 0u ||
            acCommand.AttemptNonce != 0u)
        {
            return false;
        }

        if (!PartyQuestNativeLoadBridgePolicy::IsValidIdentity(
                acCommand.Identity))
        {
            aFailure =
                PartyQuestNativeLoadBridgeOwnerResultCode::InvalidIdentity;
            return false;
        }
        return true;

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Cancel:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Poll:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Retire:
        return acCommand.BindDisposition ==
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected &&
            acCommand.RuntimeGeneration == 0u &&
            acCommand.RuntimeFingerprint == 0u &&
            IsZeroIdentity(acCommand.Identity);

    case PartyQuestNativeLoadBridgeOwnerCommandKind::GenerationTransition:
        return acCommand.BindDisposition ==
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected &&
            acCommand.RuntimeGeneration != 0u &&
            acCommand.RuntimeFingerprint == 0u &&
            acCommand.AttemptNonce == 0u &&
            IsZeroIdentity(acCommand.Identity);

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Shutdown:
        return acCommand.BindDisposition ==
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected &&
            acCommand.RuntimeGeneration == 0u &&
            acCommand.RuntimeFingerprint == 0u &&
            acCommand.AttemptNonce == 0u &&
            IsZeroIdentity(acCommand.Identity);

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Invalid:
        return false;
    }

    return false;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::MakeResult(
    PartyQuestNativeLoadBridgeOwnerResultCode aCode) const noexcept
{
    PartyQuestNativeLoadBridgeOwnerResult result{};
    result.Code = aCode;
    return result;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PublishEffect(
    PartyQuestNativeLoadBridgeOwnerEffect aEffect) noexcept
{
    if (m_nextEffectSequence == std::numeric_limits<uint64_t>::max())
        return Poison();

    aEffect.Sequence = ++m_nextEffectSequence;
    m_pendingEffect = aEffect;

    auto result = MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::EffectRequired);
    result.HasEffect = 1u;
    result.Effect = aEffect;
    return result;
}

void PartyQuestNativeLoadBridgeOwnerState::ClearPendingEffect() noexcept
{
    m_pendingEffect = {};
}

void PartyQuestNativeLoadBridgeOwnerState::ClearRequest() noexcept
{
    m_requestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase::None;
    m_activeAttemptNonce = 0u;
    m_activeIdentity = {};
    m_cachedCompletion = {};
}

void PartyQuestNativeLoadBridgeOwnerState::ClearBinding() noexcept
{
    m_capabilityRetained = 0u;
    m_boundGeneration = 0u;
    m_runtimeFingerprint = 0u;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::Poison() noexcept
{
    ClearPendingEffect();
    m_phase =
        PartyQuestNativeLoadBridgeOwnerPhase::PoisonedUnsafeToUnload;
    auto result = MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::
            PoisonedUnsafeToUnload);
    result.ReleaseCapability = 0u;
    return result;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::FinishExactRequest(
    PartyQuestNativeLoadBridgeOwnerResultCode aCode) noexcept
{
    if (m_phase != PartyQuestNativeLoadBridgeOwnerPhase::Bound &&
        m_phase != PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly &&
        m_phase != PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain)
    {
        return Poison();
    }

    const auto phase = m_phase;
    ClearRequest();

    auto result = MakeResult(aCode);
    if (phase == PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly)
    {
        ClearBinding();
        m_phase = PartyQuestNativeLoadBridgeOwnerPhase::Unbound;
        result.ReleaseCapability = 1u;
    }
    else if (phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain)
    {
        ClearBinding();
        m_phase = PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete;
        result.ReleaseCapability = 1u;
    }

    return result;
}

PartyQuestNativeLoadBridgeOwnerNonceClass
PartyQuestNativeLoadBridgeOwnerState::ClassifyNonce(
    uint64_t aAttemptNonce) const noexcept
{
    if (aAttemptNonce == 0u)
        return PartyQuestNativeLoadBridgeOwnerNonceClass::Mismatch;

    if (m_activeAttemptNonce != 0u)
    {
        if (aAttemptNonce == m_activeAttemptNonce)
            return PartyQuestNativeLoadBridgeOwnerNonceClass::ExactActive;
        if (aAttemptNonce < m_activeAttemptNonce)
            return PartyQuestNativeLoadBridgeOwnerNonceClass::Stale;
        return PartyQuestNativeLoadBridgeOwnerNonceClass::Mismatch;
    }

    if (m_lastAttemptNonce != 0u)
    {
        if (aAttemptNonce == m_lastAttemptNonce)
            return PartyQuestNativeLoadBridgeOwnerNonceClass::ExactRetired;
        if (aAttemptNonce < m_lastAttemptNonce)
            return PartyQuestNativeLoadBridgeOwnerNonceClass::Stale;
    }

    return PartyQuestNativeLoadBridgeOwnerNonceClass::Mismatch;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PlanBind(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
{
    if (m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete ||
        m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain ||
        m_phase == PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::AdmissionClosed);
    }

    if (m_phase == PartyQuestNativeLoadBridgeOwnerPhase::Bound)
    {
        if (acCommand.BindDisposition ==
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Authenticated &&
            m_capabilityRetained != 0u &&
            acCommand.RuntimeGeneration == m_boundGeneration &&
            acCommand.RuntimeGeneration == m_currentGeneration &&
            acCommand.RuntimeFingerprint == m_runtimeFingerprint)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::AlreadyBound);
        }

        if (acCommand.BindDisposition ==
                PartyQuestNativeLoadBridgeOwnerBindDisposition::Authenticated &&
            acCommand.RuntimeGeneration != m_currentGeneration)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::StaleGeneration);
        }

        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
    }

    if (m_phase != PartyQuestNativeLoadBridgeOwnerPhase::Unbound)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
    }

    if (acCommand.BindDisposition ==
        PartyQuestNativeLoadBridgeOwnerBindDisposition::Rejected)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::BindRejected);
    }

    if (m_currentGeneration != 0u &&
        acCommand.RuntimeGeneration != m_currentGeneration)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::StaleGeneration);
    }

    if (m_currentGeneration == 0u)
        m_currentGeneration = acCommand.RuntimeGeneration;

    m_boundGeneration = acCommand.RuntimeGeneration;
    m_runtimeFingerprint = acCommand.RuntimeFingerprint;
    m_capabilityRetained = 1u;
    m_phase = PartyQuestNativeLoadBridgeOwnerPhase::Bound;
    return MakeResult(PartyQuestNativeLoadBridgeOwnerResultCode::Bound);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PlanReserve(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
{
    if (m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete ||
        m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain ||
        m_phase == PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::AdmissionClosed);
    }

    if (m_phase != PartyQuestNativeLoadBridgeOwnerPhase::Bound ||
        m_capabilityRetained == 0u)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::NotBound);
    }

    if (m_boundGeneration == 0u ||
        m_boundGeneration != m_currentGeneration)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::StaleGeneration);
    }

    if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::None ||
        m_activeAttemptNonce != 0u)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
    }

    PartyQuestNativeLoadBridgeOwnerEffect effect{};
    effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve;
    effect.ReserveRequest.AbiVersion =
        kPartyQuestNativeLoadBridgePayloadAbi;
    effect.ReserveRequest.StructSize =
        sizeof(PartyQuestNativeLoadBridgeReserveRequestV1);
    effect.ReserveRequest.Identity =
        CanonicalizeIdentity(acCommand.Identity);
    return PublishEffect(effect);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PlanRequestCommand(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
{
    const auto nonceClass = ClassifyNonce(acCommand.AttemptNonce);
    switch (nonceClass)
    {
    case PartyQuestNativeLoadBridgeOwnerNonceClass::ExactRetired:
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::Duplicate);
    case PartyQuestNativeLoadBridgeOwnerNonceClass::Stale:
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::StaleNonce);
    case PartyQuestNativeLoadBridgeOwnerNonceClass::Mismatch:
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::NonceMismatch);
    case PartyQuestNativeLoadBridgeOwnerNonceClass::ExactActive:
        break;
    }

    if (m_phase != PartyQuestNativeLoadBridgeOwnerPhase::Bound &&
        m_phase != PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly &&
        m_phase != PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
    }

    PartyQuestNativeLoadBridgeOwnerEffect effect{};
    effect.AttemptNonce = m_activeAttemptNonce;

    switch (acCommand.Kind)
    {
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Cancel:
        if (m_requestPhase ==
            PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
        }
        if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::Active)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
        }
        effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel;
        return PublishEffect(effect);

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Poll:
        if (m_requestPhase ==
            PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached)
        {
            auto result = MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::
                    CompletionAvailable);
            result.HasCompletion = 1u;
            result.Completion = m_cachedCompletion;
            return result;
        }
        if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::Active)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
        }
        effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Poll;
        return PublishEffect(effect);

    case PartyQuestNativeLoadBridgeOwnerCommandKind::Retire:
        if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached)
        {
            return MakeResult(
                PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
        }
        effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Retire;
        return PublishEffect(effect);

    default:
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidCommand);
    }
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PlanGenerationTransition(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
{
    if (m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::AdmissionClosed);
    }

    if (m_currentGeneration != 0u &&
        acCommand.RuntimeGeneration <= m_currentGeneration)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidCommand);
    }

    m_currentGeneration = acCommand.RuntimeGeneration;

    if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::None ||
        m_activeAttemptNonce != 0u)
    {
        if (m_capabilityRetained == 0u)
            return Poison();

        if (m_phase !=
            PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain)
        {
            m_phase = PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly;
        }

        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::DrainPending);
    }

    auto result = MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::Unbound);
    if (m_capabilityRetained != 0u)
    {
        ClearBinding();
        result.ReleaseCapability = 1u;
    }
    m_phase = PartyQuestNativeLoadBridgeOwnerPhase::Unbound;
    return result;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::PlanShutdown() noexcept
{
    if (m_phase == PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::ShutdownComplete);
    }

    if (m_requestPhase ==
            PartyQuestNativeLoadBridgeOwnerRequestPhase::None &&
        m_activeAttemptNonce == 0u)
    {
        auto result = MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::ShutdownComplete);
        if (m_capabilityRetained != 0u)
        {
            ClearBinding();
            result.ReleaseCapability = 1u;
        }
        m_phase = PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete;
        return result;
    }

    if (m_capabilityRetained == 0u || m_activeAttemptNonce == 0u)
        return Poison();

    m_phase = PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain;

    PartyQuestNativeLoadBridgeOwnerEffect effect{};
    effect.AttemptNonce = m_activeAttemptNonce;
    if (m_requestPhase ==
        PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached)
    {
        effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Retire;
    }
    else if (m_requestPhase ==
             PartyQuestNativeLoadBridgeOwnerRequestPhase::Active)
    {
        effect.Kind = PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel;
    }
    else
    {
        return Poison();
    }

    return PublishEffect(effect);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::Plan(
    const PartyQuestNativeLoadBridgeOwnerCommand& acCommand) noexcept
{
    if (m_phase ==
        PartyQuestNativeLoadBridgeOwnerPhase::PoisonedUnsafeToUnload)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::
                PoisonedUnsafeToUnload);
    }

    if (m_pendingEffect.Kind !=
        PartyQuestNativeLoadBridgeOwnerEffectKind::None)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::OperationInFlight);
    }

    PartyQuestNativeLoadBridgeOwnerResultCode shapeFailure{};
    if (!IsCommandShapeValid(acCommand, shapeFailure))
        return MakeResult(shapeFailure);

    switch (acCommand.Kind)
    {
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Bind:
        return PlanBind(acCommand);
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Reserve:
        return PlanReserve(acCommand);
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Cancel:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Poll:
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Retire:
        return PlanRequestCommand(acCommand);
    case PartyQuestNativeLoadBridgeOwnerCommandKind::GenerationTransition:
        return PlanGenerationTransition(acCommand);
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Shutdown:
        return PlanShutdown();
    case PartyQuestNativeLoadBridgeOwnerCommandKind::Invalid:
        break;
    }

    return MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::InvalidCommand);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::ApplyReserve(
    const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
    PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    if (aStatus != PartyQuestNativeLoadBridgeStatus::Reserved ||
        acOutcome.HasReservation != 1u ||
        acOutcome.HasCompletion != 0u ||
        !PartyQuestNativeLoadBridgePolicy::IsValidReservation(
            acOutcome.Reservation) ||
        !IsZeroCompletion(acOutcome.Completion) ||
        m_phase != PartyQuestNativeLoadBridgeOwnerPhase::Bound ||
        m_capabilityRetained == 0u ||
        m_boundGeneration == 0u ||
        m_boundGeneration != m_currentGeneration ||
        m_requestPhase != PartyQuestNativeLoadBridgeOwnerRequestPhase::None ||
        m_activeAttemptNonce != 0u ||
        !IdentityEquals(
            acOutcome.Reservation.Identity,
            m_pendingEffect.ReserveRequest.Identity) ||
        acOutcome.Reservation.AttemptNonce <= m_lastAttemptNonce)
    {
        return Poison();
    }

    m_activeAttemptNonce = acOutcome.Reservation.AttemptNonce;
    m_lastAttemptNonce = acOutcome.Reservation.AttemptNonce;
    m_activeIdentity =
        CanonicalizeIdentity(m_pendingEffect.ReserveRequest.Identity);
    m_requestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase::Active;
    ClearPendingEffect();

    return MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::Reserved);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::ApplyCancel(
    const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
    PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    if (acOutcome.HasReservation != 0u ||
        acOutcome.HasCompletion != 0u ||
        !IsZeroReservation(acOutcome.Reservation) ||
        !IsZeroCompletion(acOutcome.Completion) ||
        m_requestPhase != PartyQuestNativeLoadBridgeOwnerRequestPhase::Active ||
        m_activeAttemptNonce == 0u ||
        m_pendingEffect.AttemptNonce != m_activeAttemptNonce)
    {
        return Poison();
    }

    if (aStatus == PartyQuestNativeLoadBridgeStatus::Cancelled)
    {
        ClearPendingEffect();
        return FinishExactRequest(
            PartyQuestNativeLoadBridgeOwnerResultCode::Cancelled);
    }

    if (aStatus == PartyQuestNativeLoadBridgeStatus::InvalidState)
    {
        ClearPendingEffect();
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::Pending);
    }

    return Poison();
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::ApplyPoll(
    const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
    PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    if (m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::Active ||
        m_activeAttemptNonce == 0u ||
        m_pendingEffect.AttemptNonce != m_activeAttemptNonce)
    {
        return Poison();
    }

    if (aStatus == PartyQuestNativeLoadBridgeStatus::Pending)
    {
        if (acOutcome.HasReservation != 0u ||
            acOutcome.HasCompletion != 0u ||
            !IsZeroReservation(acOutcome.Reservation) ||
            !IsZeroCompletion(acOutcome.Completion))
        {
            return Poison();
        }

        ClearPendingEffect();
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::Pending);
    }

    if (aStatus != PartyQuestNativeLoadBridgeStatus::CompletionAvailable ||
        acOutcome.HasReservation != 0u ||
        acOutcome.HasCompletion != 1u ||
        !IsZeroReservation(acOutcome.Reservation) ||
        !PartyQuestNativeLoadBridgePolicy::IsValidCompletion(
            acOutcome.Completion) ||
        acOutcome.Completion.AttemptNonce != m_activeAttemptNonce ||
        !IdentityEquals(
            acOutcome.Completion.Identity,
            m_activeIdentity) ||
        acOutcome.Completion.EventSequence <= m_lastCompletionSequence)
    {
        return Poison();
    }

    m_cachedCompletion = acOutcome.Completion;
    m_cachedCompletion.Identity = m_activeIdentity;
    m_lastCompletionSequence = acOutcome.Completion.EventSequence;
    m_requestPhase =
        PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached;
    ClearPendingEffect();

    auto result = MakeResult(
        PartyQuestNativeLoadBridgeOwnerResultCode::CompletionAvailable);
    result.HasCompletion = 1u;
    result.Completion = m_cachedCompletion;
    return result;
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::ApplyRetire(
    const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome,
    PartyQuestNativeLoadBridgeStatus aStatus) noexcept
{
    if (aStatus != PartyQuestNativeLoadBridgeStatus::Retired ||
        acOutcome.HasReservation != 0u ||
        acOutcome.HasCompletion != 0u ||
        !IsZeroReservation(acOutcome.Reservation) ||
        !IsZeroCompletion(acOutcome.Completion) ||
        m_requestPhase !=
            PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached ||
        m_activeAttemptNonce == 0u ||
        m_pendingEffect.AttemptNonce != m_activeAttemptNonce)
    {
        return Poison();
    }

    ClearPendingEffect();
    return FinishExactRequest(
        PartyQuestNativeLoadBridgeOwnerResultCode::Retired);
}

PartyQuestNativeLoadBridgeOwnerResult
PartyQuestNativeLoadBridgeOwnerState::ApplyForeignOutcome(
    const PartyQuestNativeLoadBridgeOwnerForeignOutcome& acOutcome) noexcept
{
    if (m_phase ==
        PartyQuestNativeLoadBridgeOwnerPhase::PoisonedUnsafeToUnload)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::
                PoisonedUnsafeToUnload);
    }

    if (m_pendingEffect.Kind ==
        PartyQuestNativeLoadBridgeOwnerEffectKind::None)
    {
        return MakeResult(
            PartyQuestNativeLoadBridgeOwnerResultCode::InvalidState);
    }

    if (acOutcome.EffectKind != m_pendingEffect.Kind ||
        !AreZero(acOutcome.Reserved0, sizeof(acOutcome.Reserved0)) ||
        acOutcome.Reserved1 != 0u)
    {
        return Poison();
    }

    if (acOutcome.Disposition ==
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Unknown)
    {
        return Poison();
    }

    if (acOutcome.Disposition !=
        PartyQuestNativeLoadBridgeOwnerForeignDisposition::Returned)
    {
        return Poison();
    }

    if (!PartyQuestNativeLoadBridgePolicy::IsKnownStatus(
            acOutcome.RawStatus))
    {
        return Poison();
    }

    const auto status =
        static_cast<PartyQuestNativeLoadBridgeStatus>(
            acOutcome.RawStatus);

    switch (m_pendingEffect.Kind)
    {
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve:
        return ApplyReserve(acOutcome, status);
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel:
        return ApplyCancel(acOutcome, status);
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Poll:
        return ApplyPoll(acOutcome, status);
    case PartyQuestNativeLoadBridgeOwnerEffectKind::Retire:
        return ApplyRetire(acOutcome, status);
    case PartyQuestNativeLoadBridgeOwnerEffectKind::None:
        break;
    }

    return Poison();
}

PartyQuestNativeLoadBridgeOwnerSnapshot
PartyQuestNativeLoadBridgeOwnerState::Snapshot() const noexcept
{
    PartyQuestNativeLoadBridgeOwnerSnapshot result{};
    result.Phase = m_phase;
    result.RequestPhase = m_requestPhase;
    result.PendingEffect = m_pendingEffect.Kind;
    result.CapabilityRetained = m_capabilityRetained;
    result.HasCachedCompletion =
        m_requestPhase ==
                PartyQuestNativeLoadBridgeOwnerRequestPhase::CompletionCached ?
            1u : 0u;

    result.CurrentGeneration = m_currentGeneration;
    result.BoundGeneration = m_boundGeneration;
    result.RuntimeFingerprint = m_runtimeFingerprint;
    result.PendingEffectSequence = m_pendingEffect.Sequence;

    result.ActiveAttemptNonce = m_activeAttemptNonce;
    result.LastAttemptNonce = m_lastAttemptNonce;
    result.LastCompletionSequence = m_lastCompletionSequence;

    result.ActiveIdentity = m_activeIdentity;
    result.CachedCompletion = m_cachedCompletion;
    return result;
}
