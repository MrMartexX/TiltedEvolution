#include <Structs/Skyrim/PartyQuestRuntimeMutationDispatch.h>

namespace
{
bool MatchesActiveIdentity(
    const PartyQuestRuntimeApplyIdentity& acIdentity,
    const PartyQuestRuntimeApplyEntry& acActive) noexcept
{
    return acIdentity.QuestId == acActive.QuestId &&
        acIdentity.TargetWorldRevision == acActive.TargetWorldRevision &&
        acIdentity.CanonicalDigest == acActive.CanonicalDigest &&
        acIdentity.SidecarManifestFingerprint == acActive.SidecarManifestFingerprint &&
        acIdentity.Actions == acActive.Actions &&
        acIdentity.ExpectedVerification == acActive.ExpectedVerification;
}

bool HasPhysicalGuard(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    uint64_t aTransactionId) noexcept
{
    const auto& guard = aGuardedSession.GetSaveGuard();
    return aTransactionId != 0 &&
        guard.IsActive() &&
        guard.GetTransactionId() == aTransactionId;
}

bool HasDispatchContext(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestRuntimeApplyIdentity& acIdentity,
    uint64_t aTransactionId,
    PartyQuestRuntimeApplyState aExpectedState,
    bool aMutationBarrierArmed) noexcept
{
    const auto* active = aGuardedSession.GetRuntimeSession().GetCoordinator().GetActive();
    return active &&
        active->TransactionId == aTransactionId &&
        active->State == aExpectedState &&
        active->SaveGuardActive &&
        active->CheckpointCreated &&
        active->RuntimeMutationMayHaveOccurred == aMutationBarrierArmed &&
        MatchesActiveIdentity(acIdentity, *active) &&
        HasPhysicalGuard(aGuardedSession, aTransactionId);
}

bool MatchesCompatibilityAuthority(
    const PartyQuestRuntimeApplyRequest& acRequest,
    const PartyQuestRuntimeApplyEntry& acActive,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    const PartyQuestRuntimeCompatibilityDecision& acDecision) noexcept
{
    if (!acDecision.IsAuthorized() ||
        acRequirement.QuestId != acActive.QuestId ||
        acRequirement.QuestId != acRequest.CanonicalSnapshot.QuestId)
    {
        return false;
    }

    const auto& profile = acDecision.SafetyProfile;
    const uint64_t compatibilityFingerprint = profile.GetCompatibilityFingerprint();
    return compatibilityFingerprint != 0 &&
        compatibilityFingerprint ==
            acRequest.Plan.MutationAuthorization.GetCompatibilityFingerprint() &&
        compatibilityFingerprint == acActive.ExpectedVerification.CompatibilityFingerprint &&
        profile.GetAdapterMutationComponents() ==
            acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() &&
        profile.GetAdapterMutationComponents() == PartyQuestVerificationComponent::QuestSnapshot;
}
} // namespace

PartyQuestRuntimeMutationDispatchResult PartyQuestRuntimeMutationDispatchGate::Dispatch(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestRuntimeApplyRequest& acCurrentRequest,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    const CompatibilityObserver& acObserver,
    const MutationExecutor& acExecutor)
{
    PartyQuestRuntimeMutationDispatchResult result;
    result.ArmResult.TransactionId = acCurrentRequest.TransactionId;

    if (!acObserver || !acExecutor)
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::InvalidRequest;
        return result;
    }

    const auto identity =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(acCurrentRequest);
    if (!identity || acCurrentRequest.Plan.DryRunOnly)
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::InvalidRequest;
        return result;
    }

    if (!HasDispatchContext(
            aGuardedSession,
            *identity,
            acCurrentRequest.TransactionId,
            PartyQuestRuntimeApplyState::ReadyToApply,
            false))
    {
        result.Status = HasPhysicalGuard(
            aGuardedSession,
            acCurrentRequest.TransactionId)
            ? PartyQuestRuntimeMutationDispatchStatus::InvalidRuntimeState
            : PartyQuestRuntimeMutationDispatchStatus::GuardMismatch;
        return result;
    }

    auto facts = acObserver(identity->QuestId);
    if (!facts)
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::ObservationUnavailable;
        return result;
    }

    auto compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        acRequirement,
        *facts);
    result.CompatibilityStatus = compatibility.Status;
    if (!compatibility.IsAuthorized())
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::CompatibilityRejected;
        return result;
    }

    const auto* active = aGuardedSession.GetRuntimeSession().GetCoordinator().GetActive();
    if (!active || !MatchesCompatibilityAuthority(
            acCurrentRequest,
            *active,
            acRequirement,
            compatibility))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::CompatibilityAuthorityMismatch;
        return result;
    }

    result.ArmResult = aGuardedSession.ArmRuntimeMutation(acCurrentRequest.TransactionId);
    result.MutationBarrierArmed =
        result.ArmResult.Status == PartyQuestRuntimeGuardStatus::Ready;
    if (!result.MutationBarrierArmed)
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::ArmFailed;
        return result;
    }

    // Observe again after the durability write. A runtime/load-order change that
    // happened while arming must never inherit the earlier compatibility result.
    facts = acObserver(identity->QuestId);
    if (!facts)
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::ObservationUnavailable;
        return result;
    }

    compatibility = PartyQuestRuntimeCompatibilityPolicy::Evaluate(
        acRequirement,
        *facts);
    result.CompatibilityStatus = compatibility.Status;
    if (!compatibility.IsAuthorized())
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::CompatibilityRejected;
        return result;
    }

    active = aGuardedSession.GetRuntimeSession().GetCoordinator().GetActive();
    if (!active || !MatchesCompatibilityAuthority(
            acCurrentRequest,
            *active,
            acRequirement,
            compatibility))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::CompatibilityAuthorityMismatch;
        return result;
    }

    // Final structural/physical guard recheck is immediately adjacent to the
    // callback. No bearer dispatch capability survives this function call.
    if (!HasDispatchContext(
            aGuardedSession,
            *identity,
            acCurrentRequest.TransactionId,
            PartyQuestRuntimeApplyState::WaitingForPapyrus,
            true))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::DispatchContextLost;
        return result;
    }

    result.MutationInvoked = true;
    if (!acExecutor(acCurrentRequest))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::ExecutorRejected;
        return result;
    }

    result.Status = PartyQuestRuntimeMutationDispatchStatus::Dispatched;
    return result;
}
