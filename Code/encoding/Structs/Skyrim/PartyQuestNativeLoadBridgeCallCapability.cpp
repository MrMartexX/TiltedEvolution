#include <Structs/Skyrim/PartyQuestNativeLoadBridgeCallCapability.h>

#include <cstddef>

namespace
{
[[nodiscard]] bool AreZero(const uint8_t* apBytes, size_t aSize) noexcept
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

[[nodiscard]] bool IsZeroIdentity(
    const PartyQuestNativeLoadBridgeIdentityV1& acIdentity) noexcept
{
    return acIdentity.Length == 0u &&
        acIdentity.Reserved0 == 0u &&
        AreZero(acIdentity.Bytes, sizeof(acIdentity.Bytes));
}

[[nodiscard]] bool IsZeroReserveRequest(
    const PartyQuestNativeLoadBridgeReserveRequestV1& acRequest) noexcept
{
    return acRequest.AbiVersion == 0u &&
        acRequest.StructSize == 0u &&
        IsZeroIdentity(acRequest.Identity);
}

[[nodiscard]] bool IsZeroCompletion(
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

[[nodiscard]] bool IsEnvelopeValid(
    const PartyQuestNativeLoadBridgeOwnerResult& acPlan) noexcept
{
    return acPlan.Code ==
            PartyQuestNativeLoadBridgeOwnerResultCode::EffectRequired &&
        acPlan.HasEffect == 1u &&
        acPlan.ReleaseCapability == 0u &&
        acPlan.HasCompletion == 0u &&
        AreZero(acPlan.Reserved, sizeof(acPlan.Reserved)) &&
        acPlan.Effect.Kind !=
            PartyQuestNativeLoadBridgeOwnerEffectKind::None &&
        AreZero(acPlan.Effect.Reserved, sizeof(acPlan.Effect.Reserved)) &&
        IsZeroCompletion(acPlan.Completion);
}

[[nodiscard]] PartyQuestNativeLoadBridgeCallCapabilityResult Failure(
    PartyQuestNativeLoadBridgeCallCapabilityStatus aStatus) noexcept
{
    PartyQuestNativeLoadBridgeCallCapabilityResult result{};
    result.Status = aStatus;
    return result;
}

[[nodiscard]] bool IsRequestEffect(
    PartyQuestNativeLoadBridgeOwnerEffectKind aKind) noexcept
{
    return aKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Cancel ||
        aKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Poll ||
        aKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Retire;
}
} // namespace

PartyQuestNativeLoadBridgeCallCapabilityResult
PartyQuestNativeLoadBridgeCallCapabilityPolicy::Authorize(
    const PartyQuestNativeLoadBridgeOwnerSnapshot& acSnapshot,
    const PartyQuestNativeLoadBridgeOwnerResult& acPlan) noexcept
{
    if (acPlan.HasEffect == 0u)
    {
        return Failure(
            acPlan.Code ==
                    PartyQuestNativeLoadBridgeOwnerResultCode::EffectRequired ?
                PartyQuestNativeLoadBridgeCallCapabilityStatus::InvalidPlan :
                PartyQuestNativeLoadBridgeCallCapabilityStatus::
                    NoForeignEffect);
    }

    if (!IsEnvelopeValid(acPlan))
    {
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::InvalidPlan);
    }

    if (acSnapshot.CapabilityRetained != 1u ||
        acSnapshot.BoundGeneration == 0u ||
        acSnapshot.CurrentGeneration == 0u ||
        acSnapshot.RuntimeFingerprint == 0u)
    {
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::
                CapabilityUnavailable);
    }

    if (acSnapshot.PendingEffect != acPlan.Effect.Kind)
    {
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
    }

    PartyQuestNativeLoadBridgeCallAuthority authority{
        PartyQuestNativeLoadBridgeCallAuthority::None};

    switch (acSnapshot.Phase)
    {
    case PartyQuestNativeLoadBridgeOwnerPhase::Bound:
        if (acSnapshot.BoundGeneration != acSnapshot.CurrentGeneration)
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
        }
        authority =
            PartyQuestNativeLoadBridgeCallAuthority::CurrentGeneration;
        break;

    case PartyQuestNativeLoadBridgeOwnerPhase::DrainOnly:
        if (acSnapshot.CurrentGeneration <= acSnapshot.BoundGeneration)
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
        }
        authority = PartyQuestNativeLoadBridgeCallAuthority::RequestDrain;
        break;

    case PartyQuestNativeLoadBridgeOwnerPhase::ShutdownDrain:
        if (acSnapshot.CurrentGeneration < acSnapshot.BoundGeneration)
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
        }
        authority = PartyQuestNativeLoadBridgeCallAuthority::RequestDrain;
        break;

    case PartyQuestNativeLoadBridgeOwnerPhase::Unbound:
    case PartyQuestNativeLoadBridgeOwnerPhase::ShutdownComplete:
    case PartyQuestNativeLoadBridgeOwnerPhase::PoisonedUnsafeToUnload:
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
    }

    const auto effectKind = acPlan.Effect.Kind;
    uint64_t attemptNonce = acPlan.Effect.AttemptNonce;

    if (effectKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve)
    {
        if (authority !=
                PartyQuestNativeLoadBridgeCallAuthority::CurrentGeneration ||
            acSnapshot.RequestPhase !=
                PartyQuestNativeLoadBridgeOwnerRequestPhase::None ||
            acSnapshot.ActiveAttemptNonce != 0u ||
            attemptNonce != 0u ||
            !PartyQuestNativeLoadBridgePolicy::IsValidReserveRequest(
                acPlan.Effect.ReserveRequest))
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
        }
    }
    else
    {
        if (!IsRequestEffect(effectKind) ||
            !IsZeroReserveRequest(acPlan.Effect.ReserveRequest) ||
            acSnapshot.ActiveAttemptNonce == 0u ||
            attemptNonce != acSnapshot.ActiveAttemptNonce)
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::NonceMismatch);
        }

        if (effectKind ==
            PartyQuestNativeLoadBridgeOwnerEffectKind::Retire)
        {
            if (acSnapshot.RequestPhase !=
                    PartyQuestNativeLoadBridgeOwnerRequestPhase::
                        CompletionCached ||
                acSnapshot.HasCachedCompletion != 1u)
            {
                return Failure(
                    PartyQuestNativeLoadBridgeCallCapabilityStatus::
                        StateMismatch);
            }
        }
        else if (acSnapshot.RequestPhase !=
                     PartyQuestNativeLoadBridgeOwnerRequestPhase::Active ||
                 acSnapshot.HasCachedCompletion != 0u)
        {
            return Failure(
                PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
        }
    }

    if (authority == PartyQuestNativeLoadBridgeCallAuthority::RequestDrain &&
        effectKind == PartyQuestNativeLoadBridgeOwnerEffectKind::Reserve)
    {
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::StateMismatch);
    }

    PartyQuestNativeLoadBridgeCallCapabilityResult result{};
    result.Status =
        PartyQuestNativeLoadBridgeCallCapabilityStatus::Authorized;
    result.HasCapability = 1u;
    result.Capability.EffectKind = effectKind;
    result.Capability.Authority = authority;
    result.Capability.PinnedModuleRequired = 1u;
    result.Capability.BoundGeneration = acSnapshot.BoundGeneration;
    result.Capability.ObservedGeneration = acSnapshot.CurrentGeneration;
    result.Capability.RuntimeFingerprint = acSnapshot.RuntimeFingerprint;
    result.Capability.AttemptNonce = attemptNonce;

    if (!result.Capability.IsAuthorized())
    {
        return Failure(
            PartyQuestNativeLoadBridgeCallCapabilityStatus::InvalidPlan);
    }

    return result;
}
