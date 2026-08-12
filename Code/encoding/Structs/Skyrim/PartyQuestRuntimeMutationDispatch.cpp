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

bool GenerationChanged(
    const PartyQuestRuntimeGenerationFence& acGenerationFence,
    uint64_t aExpectedGeneration) noexcept
{
    return aExpectedGeneration == 0 ||
        acGenerationFence.GetGeneration() != aExpectedGeneration;
}
} // namespace

PartyQuestRuntimeMutationDispatchResult PartyQuestRuntimeMutationDispatchGate::Dispatch(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestRuntimeApplyRequest& acCurrentRequest,
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    PartyQuestRuntimeGenerationFence& aGenerationFence,
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

    const uint64_t expectedGeneration = aGenerationFence.GetGeneration();
    auto facts = acObserver(identity->QuestId);
    if (GenerationChanged(aGenerationFence, expectedGeneration))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::RuntimeGenerationChanged;
        return result;
    }

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

    // A lifecycle/resolver transition while the durable arm was being written
    // invalidates the earlier observation. Stay behind the recovery barrier.
    if (GenerationChanged(aGenerationFence, expectedGeneration))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::RuntimeGenerationChanged;
        return result;
    }

    // Observe again after the durability write. A runtime/load-order change that
    // happens during this sample must never inherit the earlier compatibility result.
    facts = acObserver(identity->QuestId);
    if (GenerationChanged(aGenerationFence, expectedGeneration))
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::RuntimeGenerationChanged;
        return result;
    }

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

    // Pin the exact observed generation across the final context validation and
    // synchronous executor callback. Invalidation takes the exclusive side of
    // the same process-local fence and therefore cannot cross this boundary.
    auto generationLease = aGenerationFence.TryAcquire(expectedGeneration);
    if (!generationLease || !generationLease->IsValid())
    {
        result.Status = PartyQuestRuntimeMutationDispatchStatus::RuntimeGenerationChanged;
        return result;
    }

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
