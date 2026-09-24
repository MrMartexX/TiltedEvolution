#include <Games/Skyrim/PartyQuestSkyrimNativeLoadBridgeCoordinator.h>

#include <cstring>

namespace
{
using CoordinatorStatus =
    PartyQuestSkyrimNativeLoadBridgeCoordinatorStatus;
using OwnerStatus = PartyQuestSkyrimNativeLoadBridgeOwnerStatus;
using StateCode = PartyQuestNativeLoadBridgeOwnerResultCode;
using OwnerPhase = PartyQuestNativeLoadBridgeOwnerPhase;
using RequestPhase = PartyQuestNativeLoadBridgeOwnerRequestPhase;

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult MakeResult(
    CoordinatorStatus aStatus,
    PartyQuestNativeLoadBridgeStatus aProviderStatus =
        PartyQuestNativeLoadBridgeStatus::BridgeUnavailable) noexcept
{
    PartyQuestSkyrimNativeLoadBridgeCoordinatorResult result{};
    result.Status = aStatus;
    result.ProviderStatus = aProviderStatus;
    return result;
}
} // namespace

PartyQuestSkyrimNativeLoadBridgeCoordinator&
PartyQuestSkyrimNativeLoadBridgeCoordinator::GetProcessCoordinator() noexcept
{
    static PartyQuestSkyrimNativeLoadBridgeCoordinator s_coordinator(
        PartyQuestSkyrimNativeLoadBridgeOwner::GetProcessOwner(),
        PartyQuestSkyrimNativeLoadBridgeProvider::GetProcessProvider());
    return s_coordinator;
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::Begin(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
try
{
    const auto providerState = m_provider.GetState();
    if (providerState ==
        PartyQuestNativeLoadBridgeAdapterState::NotReady)
    {
        const auto inactiveOwner = m_owner.Snapshot();
        if (inactiveOwner.Phase == OwnerPhase::Unbound &&
            inactiveOwner.RequestPhase == RequestPhase::None &&
            inactiveOwner.CapabilityRetained == 0u &&
            inactiveOwner.ActiveAttemptNonce == 0u)
        {
            return MakeResult(CoordinatorStatus::Bypassed);
        }

        return MakeResult(CoordinatorStatus::Rejected);
    }

    if (providerState ==
        PartyQuestNativeLoadBridgeAdapterState::Poisoned)
    {
        return MakeResult(
            CoordinatorStatus::PoisonedUnsafeToUnload,
            PartyQuestNativeLoadBridgeStatus::Poisoned);
    }

    const auto identity = ToBridgeIdentity(acIdentity);
    if (!PartyQuestNativeLoadBridgePolicy::IsValidIdentity(identity))
    {
        return MakeResult(
            CoordinatorStatus::Rejected,
            PartyQuestNativeLoadBridgeStatus::InvalidIdentity);
    }

    const auto before = m_owner.Snapshot();
    if (before.Phase != OwnerPhase::Bound ||
        before.RequestPhase != RequestPhase::None ||
        before.CapabilityRetained == 0u ||
        before.BoundGeneration == 0u ||
        before.BoundGeneration != before.CurrentGeneration)
    {
        return MakeResult(CoordinatorStatus::Rejected);
    }

    auto result = MakeResult(CoordinatorStatus::Rejected);
    result.Owner = m_owner.Reserve(identity);
    if (result.Owner.Status != OwnerStatus::Applied ||
        result.Owner.State.Code != StateCode::Reserved)
    {
        if (result.Owner.State.Code ==
            StateCode::PoisonedUnsafeToUnload)
        {
            result.Status =
                CoordinatorStatus::PoisonedUnsafeToUnload;
            result.ProviderStatus =
                PartyQuestNativeLoadBridgeStatus::Poisoned;
        }
        return result;
    }

    const auto after = m_owner.Snapshot();
    if (after.Phase != OwnerPhase::Bound ||
        after.RequestPhase != RequestPhase::Active ||
        after.ActiveAttemptNonce == 0u ||
        after.BoundGeneration != before.BoundGeneration ||
        after.CurrentGeneration != before.CurrentGeneration ||
        m_provider.GetActiveAttemptNonce() != after.ActiveAttemptNonce)
    {
        PartyQuestSkyrimNativeLoadBridgeAttempt attempt{};
        attempt.AttemptNonce = after.ActiveAttemptNonce;
        attempt.ReservedGeneration = before.BoundGeneration;
        attempt.Identity = identity;
        return PropagateUnsafe(
            attempt,
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    result.Status = CoordinatorStatus::Reserved;
    result.ProviderStatus = PartyQuestNativeLoadBridgeStatus::Reserved;
    result.Attempt.AttemptNonce = after.ActiveAttemptNonce;
    result.Attempt.ReservedGeneration = before.BoundGeneration;
    result.Attempt.Identity = identity;
    return result;
}
catch (...)
{
    return MakeResult(CoordinatorStatus::Rejected);
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::ObserveGeneration(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
    uint64_t aGeneration) noexcept
try
{
    auto result = MakeResult(CoordinatorStatus::Rejected);
    result.Attempt = acAttempt;

    if (!acAttempt.IsValid() ||
        aGeneration == 0u ||
        !AttemptMatchesCurrent(acAttempt))
    {
        return result;
    }

    result.Owner = m_owner.ObserveGeneration(aGeneration);
    if (result.Owner.Status != OwnerStatus::Applied ||
        result.Owner.State.Code != StateCode::DrainPending)
    {
        if (result.Owner.State.Code ==
            StateCode::PoisonedUnsafeToUnload)
        {
            result.Status =
                CoordinatorStatus::PoisonedUnsafeToUnload;
            result.ProviderStatus =
                PartyQuestNativeLoadBridgeStatus::Poisoned;
        }
        return result;
    }

    const auto snapshot = m_owner.Snapshot();
    if (snapshot.Phase != OwnerPhase::DrainOnly ||
        snapshot.RequestPhase != RequestPhase::Active ||
        snapshot.ActiveAttemptNonce != acAttempt.AttemptNonce ||
        snapshot.BoundGeneration != acAttempt.ReservedGeneration ||
        snapshot.CurrentGeneration != aGeneration ||
        snapshot.CapabilityRetained == 0u)
    {
        return PropagateUnsafe(
            acAttempt,
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    result.Status = CoordinatorStatus::GenerationObserved;
    result.ProviderStatus = PartyQuestNativeLoadBridgeStatus::Pending;
    return result;
}
catch (...)
{
    return MakeResult(CoordinatorStatus::Rejected);
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::EnterTarget(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
    const PartyQuestNativeLoadIdentity& acActualIdentity) noexcept
try
{
    auto result = MakeResult(CoordinatorStatus::Rejected);
    result.Attempt = acAttempt;
    if (!acAttempt.IsValid() || !AttemptMatchesCurrent(acAttempt))
        return result;

    const auto claim = m_provider.ClaimReserved(acActualIdentity);
    result.ProviderStatus = claim.Status;

    if (claim.Status == PartyQuestNativeLoadBridgeStatus::IdentityMismatch ||
        claim.Status == PartyQuestNativeLoadBridgeStatus::InvalidIdentity)
    {
        const auto cancelled = CancelBeforeTarget(acAttempt);
        if (cancelled.Status == CoordinatorStatus::Cancelled)
            return cancelled;
        return PropagateUnsafe(acAttempt, claim.Status);
    }

    if (!claim.IsClaimed() ||
        claim.AttemptNonce != acAttempt.AttemptNonce)
    {
        return PropagateUnsafe(acAttempt, claim.Status);
    }

    const auto entered =
        m_provider.MarkTargetEntered(acAttempt.AttemptNonce);
    if (entered != PartyQuestNativeLoadBridgeStatus::Pending)
        return PropagateUnsafe(acAttempt, entered);

    result.Status = CoordinatorStatus::TargetEntered;
    result.ProviderStatus = entered;
    return result;
}
catch (...)
{
    return PropagateUnsafe(
        acAttempt,
        PartyQuestNativeLoadBridgeStatus::InternalFailure);
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::CompleteTarget(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
    bool aResult) noexcept
try
{
    auto result = MakeResult(CoordinatorStatus::Rejected);
    result.Attempt = acAttempt;
    if (!acAttempt.IsValid() || !AttemptMatchesCurrent(acAttempt))
        return result;

    const auto completed =
        m_provider.CompleteTarget(
            acAttempt.AttemptNonce,
            aResult);
    result.ProviderStatus = completed;
    if (completed != PartyQuestNativeLoadBridgeStatus::Pending)
        return PropagateUnsafe(acAttempt, completed);

    const auto polled = m_owner.Poll(acAttempt.AttemptNonce);
    result.Owner = polled;
    if (polled.Status != OwnerStatus::Applied ||
        polled.State.Code != StateCode::CompletionAvailable ||
        polled.State.HasCompletion != 1u ||
        polled.State.Completion.AttemptNonce != acAttempt.AttemptNonce ||
        polled.State.Completion.Result != (aResult ? 1u : 0u))
    {
        if (polled.State.Code ==
            StateCode::PoisonedUnsafeToUnload)
        {
            result.Status =
                CoordinatorStatus::PoisonedUnsafeToUnload;
            result.ProviderStatus =
                PartyQuestNativeLoadBridgeStatus::Poisoned;
            return result;
        }

        return PropagateUnsafe(
            acAttempt,
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    const auto completion = polled.State.Completion;
    const auto retired = m_owner.Retire(acAttempt.AttemptNonce);
    result.Owner = retired;
    if (retired.Status != OwnerStatus::Applied ||
        retired.State.Code != StateCode::Retired ||
        retired.State.ReleaseCapability != 1u ||
        m_provider.GetActiveAttemptNonce() != 0u)
    {
        if (retired.State.Code ==
            StateCode::PoisonedUnsafeToUnload)
        {
            result.Status =
                CoordinatorStatus::PoisonedUnsafeToUnload;
            result.ProviderStatus =
                PartyQuestNativeLoadBridgeStatus::Poisoned;
            return result;
        }

        return PropagateUnsafe(
            acAttempt,
            PartyQuestNativeLoadBridgeStatus::InternalFailure);
    }

    result.Status = CoordinatorStatus::Completed;
    result.ProviderStatus = PartyQuestNativeLoadBridgeStatus::Retired;
    result.HasCompletion = 1u;
    result.Completion = completion;
    return result;
}
catch (...)
{
    return PropagateUnsafe(
        acAttempt,
        PartyQuestNativeLoadBridgeStatus::InternalFailure);
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::CancelBeforeTarget(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt) noexcept
try
{
    auto result = MakeResult(CoordinatorStatus::Rejected);
    result.Attempt = acAttempt;
    if (!acAttempt.IsValid() || !AttemptMatchesCurrent(acAttempt))
        return result;

    result.Owner = m_owner.Cancel(acAttempt.AttemptNonce);
    if (result.Owner.Status == OwnerStatus::Applied &&
        result.Owner.State.Code == StateCode::Cancelled &&
        result.Owner.State.ReleaseCapability == 1u &&
        m_provider.GetActiveAttemptNonce() == 0u)
    {
        result.Status = CoordinatorStatus::Cancelled;
        result.ProviderStatus =
            PartyQuestNativeLoadBridgeStatus::Cancelled;
        return result;
    }

    if (result.Owner.State.Code ==
        StateCode::PoisonedUnsafeToUnload)
    {
        result.Status = CoordinatorStatus::PoisonedUnsafeToUnload;
        result.ProviderStatus =
            PartyQuestNativeLoadBridgeStatus::Poisoned;
    }
    return result;
}
catch (...)
{
    return PropagateUnsafe(
        acAttempt,
        PartyQuestNativeLoadBridgeStatus::InternalFailure);
}

PartyQuestNativeLoadBridgeIdentityV1
PartyQuestSkyrimNativeLoadBridgeCoordinator::ToBridgeIdentity(
    const PartyQuestNativeLoadIdentity& acIdentity) noexcept
{
    PartyQuestNativeLoadBridgeIdentityV1 identity{};
    if (acIdentity.Length == 0u ||
        acIdentity.Length > PartyQuestNativeLoadIdentity::kCapacity)
    {
        return identity;
    }

    identity.Length = acIdentity.Length;
    std::memcpy(
        identity.Bytes,
        acIdentity.Bytes,
        identity.Length);
    return identity;
}

bool PartyQuestSkyrimNativeLoadBridgeCoordinator::AttemptMatchesCurrent(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt) noexcept
{
    const auto snapshot = m_owner.Snapshot();
    return snapshot.RequestPhase == RequestPhase::Active &&
        snapshot.ActiveAttemptNonce == acAttempt.AttemptNonce &&
        snapshot.BoundGeneration == acAttempt.ReservedGeneration &&
        snapshot.CapabilityRetained != 0u &&
        m_provider.GetActiveAttemptNonce() == acAttempt.AttemptNonce;
}

PartyQuestSkyrimNativeLoadBridgeCoordinatorResult
PartyQuestSkyrimNativeLoadBridgeCoordinator::PropagateUnsafe(
    const PartyQuestSkyrimNativeLoadBridgeAttempt& acAttempt,
    PartyQuestNativeLoadBridgeStatus aProviderStatus) noexcept
{
    auto result = MakeResult(
        CoordinatorStatus::PoisonedUnsafeToUnload,
        aProviderStatus);
    result.Attempt = acAttempt;

    m_provider.Poison();
    if (acAttempt.AttemptNonce != 0u)
        result.Owner = m_owner.Poll(acAttempt.AttemptNonce);

    return result;
}
