#include <EncodingPch.h>

#include <Structs/Skyrim/PartyQuestStageMutationAdapter.h>

#include <mutex>
#include <unordered_set>

namespace
{
constexpr uint32_t kAllowedStageOnlyActions = static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) | static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
                                              static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
                                              static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify);

std::mutex s_txMutex;
std::unordered_set<uint64_t> s_executedTransactions;

void RecordTransactionExecuted(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return;

    std::lock_guard lock(s_txMutex);
    s_executedTransactions.insert(aTransactionId);
}

bool IsNarrowStagePlan(const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    const uint32_t actions = static_cast<uint32_t>(acRequest.Plan.Actions);
    return acRequest.TransactionId != 0 && !acRequest.Plan.DryRunOnly && acRequest.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe &&
           acRequest.Plan.MutationAuthorization.IsVerified() && acRequest.Plan.MutationAuthorization.Matches(acRequest.CanonicalSnapshot, acRequest.Plan.Actions, false) &&
           acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() == PartyQuestVerificationComponent::QuestSnapshot &&
           HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::StageTransition) && (actions & ~kAllowedStageOnlyActions) == 0 &&
           PartyQuestStageMutationAdapter::HasOnlyStageMutationSurface(acRequest.CanonicalSnapshot);
}
} // namespace

bool PartyQuestStageMutationAdapter::HasOnlyStageMutationSurface(const QuestSnapshot& acSnapshot) noexcept
{
    return acSnapshot.Status == QuestSnapshotStatus::Running && acSnapshot.ReferenceAliases.empty() && acSnapshot.LocationAliases.empty() && acSnapshot.CreatedReferences.empty() &&
           !acSnapshot.SceneParticipantPlayerId.has_value();
}

void PartyQuestStageMutationAdapter::SetControlledActivationEnabled(bool aEnabled) noexcept
{
    PartyQuestPersistenceDurabilityPolicy::SetControlledActivationEnabled(aEnabled);
}

bool PartyQuestStageMutationAdapter::IsControlledActivationEnabled() noexcept
{
    return PartyQuestPersistenceDurabilityPolicy::IsControlledActivationEnabled();
}

void PartyQuestStageMutationAdapter::ResetExecutedTransactionsForTest() noexcept
{
    std::lock_guard lock(s_txMutex);
    s_executedTransactions.clear();
}

bool PartyQuestStageMutationAdapter::IsTransactionExecuted(uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return false;

    std::lock_guard lock(s_txMutex);
    return s_executedTransactions.contains(aTransactionId);
}

PartyQuestStageMutationResult PartyQuestStageMutationAdapter::Execute(
    const PartyQuestRuntimeApplyRequest& acRequest, PartyQuestStageMutationContext& aContext,
    std::optional<std::reference_wrapper<PartyQuestRuntimeGuardedSession>> aGuardedSession,
    std::optional<std::reference_wrapper<PartyQuestRuntimeGenerationFence>> aGenerationFence) noexcept
{
    // Gate 1: Controlled activation capability (shipping/default is OFF)
    if (!IsControlledActivationEnabled())
    {
        return {PartyQuestStageMutationStatus::ControlledActivationDisabled, false};
    }

    // Gate 2: Thread affinity (client main update thread only)
    if (aContext.IsCurrentUpdateThread && !aContext.IsCurrentUpdateThread())
    {
        return {PartyQuestStageMutationStatus::InvalidThread, false};
    }

    // Gate 3: Structural narrow plan validation
    if (!IsNarrowStagePlan(acRequest))
    {
        return {PartyQuestStageMutationStatus::InvalidPlan, false};
    }

    // Gate 4: Deduplication / Replay protection (prevent double call to engine)
    if (IsTransactionExecuted(acRequest.TransactionId))
    {
        return {PartyQuestStageMutationStatus::DuplicateTransactionReplayed, false};
    }

    // Gate 5: Precondition live snapshot collection
    if (!aContext.CollectSnapshot)
    {
        return {PartyQuestStageMutationStatus::PreconditionSnapshotFailed, false};
    }

    std::optional<QuestSnapshot> preSnapshot;
    try
    {
        preSnapshot = aContext.CollectSnapshot();
    }
    catch (...)
    {
        return {PartyQuestStageMutationStatus::PreconditionSnapshotFailed, false};
    }

    if (!preSnapshot || preSnapshot->QuestId != acRequest.CanonicalSnapshot.QuestId)
    {
        return {PartyQuestStageMutationStatus::PreconditionSnapshotFailed, false};
    }

    if (!HasOnlyStageMutationSurface(*preSnapshot))
    {
        return {PartyQuestStageMutationStatus::PreconditionSurfaceRejected, false};
    }

    // Gate 6: Stage existence and monotonicity
    const uint16_t targetStage = acRequest.CanonicalSnapshot.CurrentStage;
    if (aContext.ContainsStage && !aContext.ContainsStage(targetStage))
    {
        return {PartyQuestStageMutationStatus::StageNotFound, false};
    }

    if (targetStage < preSnapshot->CurrentStage)
    {
        return {PartyQuestStageMutationStatus::BackwardTransitionRejected, false};
    }

    // Gate 7: Equal target stage is an idempotent no-op (no engine call)
    if (targetStage == preSnapshot->CurrentStage)
    {
        return {PartyQuestStageMutationStatus::IdempotentNoOp, true};
    }

    // Gate 8: Exact reviewed quest profile and reviewed transition authorization
    if (!PartyQuestReviewedQuestProfiles::HasReviewedProfile(acRequest.CanonicalSnapshot.QuestId))
    {
        return {PartyQuestStageMutationStatus::UnreviewedQuest, false};
    }

    const auto* pReviewedTransition = PartyQuestReviewedQuestProfiles::FindReviewedTransition(acRequest.CanonicalSnapshot.QuestId, preSnapshot->CurrentStage, targetStage);
    if (!pReviewedTransition)
    {
        return {PartyQuestStageMutationStatus::UnreviewedTransition, false};
    }

    // Gate 9: Guarded session / SaveGuard / committed checkpoint authorization
    if (aGuardedSession.has_value())
    {
        auto& guarded = aGuardedSession->get();
        const auto& guard = guarded.GetSaveGuard();
        if (!guard.IsActive() || guard.GetTransactionId() != acRequest.TransactionId)
        {
            return {PartyQuestStageMutationStatus::SaveGuardInactive, false};
        }

        const auto* active = guarded.GetRuntimeSession().GetCoordinator().GetActive();
        if (!active)
        {
            return {PartyQuestStageMutationStatus::CoordinatorInactive, false};
        }

        if (active->TransactionId != acRequest.TransactionId)
        {
            return {PartyQuestStageMutationStatus::TransactionMismatch, false};
        }

        if (active->TargetWorldRevision != acRequest.TargetWorldRevision)
        {
            return {PartyQuestStageMutationStatus::RevisionMismatch, false};
        }

        if (!active->CheckpointCreated)
        {
            return {PartyQuestStageMutationStatus::CheckpointNotCommitted, false};
        }

        if (!active->RuntimeMutationMayHaveOccurred)
        {
            return {PartyQuestStageMutationStatus::DurableBarrierUnarmed, false};
        }
    }

    // Gate 10: Generation fence validity
    if (aGenerationFence.has_value())
    {
        auto& fence = aGenerationFence->get();
        if (fence.IsPoisoned() || fence.IsLifecycleTransitionPending() || fence.GetGeneration() == 0)
        {
            return {PartyQuestStageMutationStatus::GenerationChanged, false};
        }
    }

    // Gate 11: Papyrus quiescence
    if (aContext.IsPapyrusQuiescent && !aContext.IsPapyrusQuiescent(acRequest.TransactionId))
    {
        return {PartyQuestStageMutationStatus::PapyrusBusy, false};
    }

    // Gate 12: Atomic execution boundary & native attempt evidence
    bool nativeResult = false;
    try
    {
        if (aContext.NativeSetStage)
        {
            nativeResult = aContext.NativeSetStage(targetStage);
        }
    }
    catch (...)
    {
        nativeResult = false;
    }

    if (!nativeResult)
    {
        // Native failure: enter deterministic recovery; never retry blindly
        if (aContext.OnRecoveryRequired)
        {
            aContext.OnRecoveryRequired(acRequest.TransactionId, PartyQuestStageMutationStatus::NativeInvocationFailed);
        }
        return {PartyQuestStageMutationStatus::NativeInvocationFailed, false};
    }

    // Gate 13: Immediate postcondition re-snapshotting and verification
    std::optional<QuestSnapshot> postSnapshot;
    try
    {
        postSnapshot = aContext.CollectSnapshot();
    }
    catch (...)
    {
        postSnapshot.reset();
    }

    if (!postSnapshot || postSnapshot->QuestId != acRequest.CanonicalSnapshot.QuestId || postSnapshot->CurrentStage != targetStage || !HasOnlyStageMutationSurface(*postSnapshot))
    {
        // Postcondition mismatch: enter deterministic recovery
        if (aContext.OnRecoveryRequired)
        {
            aContext.OnRecoveryRequired(acRequest.TransactionId, PartyQuestStageMutationStatus::PostconditionMismatch);
        }
        return {PartyQuestStageMutationStatus::PostconditionMismatch, false};
    }

    // Gate 14: Commit transaction execution to prevent double calls
    RecordTransactionExecuted(acRequest.TransactionId);

    return {PartyQuestStageMutationStatus::Applied, true};
}
