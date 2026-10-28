#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimStageMutationExecutor.h>
#include <PartyQuestSkyrimRuntimeThread.h>

#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReviewedQuestProfiles.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <PartyQuestSkyrimPapyrusRuntimeObserver.h>
#include <Structs/Skyrim/PartyQuestStageMutationAdapter.h>

#include <Forms/TESQuest.h>
#include <Services/QuestSnapshotCollector.h>
#include <Systems/ModSystem.h>

namespace
{
constexpr uint32_t kAllowedStageOnlyActions = static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) | static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
                                              static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
                                              static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify);

bool HasOnlyStageMutationSurface(const QuestSnapshot& acSnapshot) noexcept
{
    return PartyQuestStageMutationAdapter::HasOnlyStageMutationSurface(acSnapshot);
}

bool IsNarrowStagePlan(const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    const uint32_t actions = static_cast<uint32_t>(acRequest.Plan.Actions);
    return acRequest.TransactionId != 0 && !acRequest.Plan.DryRunOnly && acRequest.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe &&
           acRequest.Plan.MutationAuthorization.IsVerified() && acRequest.Plan.MutationAuthorization.Matches(acRequest.CanonicalSnapshot, acRequest.Plan.Actions, false) &&
           acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() == PartyQuestVerificationComponent::QuestSnapshot &&
           HasPartyQuestApplyAction(acRequest.Plan.Actions, PartyQuestApplyAction::StageTransition) && (actions & ~kAllowedStageOnlyActions) == 0 &&
           HasOnlyStageMutationSurface(acRequest.CanonicalSnapshot);
}

bool ContainsStage(TESQuest& aQuest, uint16_t aStage) noexcept
{
    for (TESQuest::Stage* pStage : aQuest.stages)
    {
        if (pStage && pStage->stageIndex == aStage)
            return true;
    }

    return false;
}
} // namespace

void PartyQuestSkyrimStageMutationExecutor::SetControlledActivationEnabled(bool aEnabled) noexcept
{
    PartyQuestStageMutationAdapter::SetControlledActivationEnabled(aEnabled);
}

bool PartyQuestSkyrimStageMutationExecutor::IsControlledActivationEnabled() noexcept
{
    return PartyQuestStageMutationAdapter::IsControlledActivationEnabled();
}

bool PartyQuestSkyrimStageMutationExecutor::Execute(const PartyQuestRuntimeApplyRequest& acRequest, ModSystem& aModSystem) noexcept
{
    if (!PartyQuestSkyrimRuntimeThread::IsCurrentUpdateThread() || !IsNarrowStagePlan(acRequest))
    {
        return false;
    }

    const uint32_t formId = aModSystem.GetGameId(acRequest.CanonicalSnapshot.QuestId);
    if (formId == 0)
        return false;

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest || pQuest->formID != formId)
        return false;

    GameId roundTripId;
    if (!aModSystem.GetServerModId(pQuest->formID, roundTripId) || roundTripId != acRequest.CanonicalSnapshot.QuestId)
    {
        return false;
    }

    const auto current = QuestSnapshotCollector::Collect(pQuest, aModSystem);
    if (!current || current->QuestId != acRequest.CanonicalSnapshot.QuestId || !HasOnlyStageMutationSurface(*current))
    {
        return false;
    }

    const uint16_t targetStage = acRequest.CanonicalSnapshot.CurrentStage;
    if (!ContainsStage(*pQuest, targetStage))
        return false;

    if (targetStage < pQuest->currentStage)
        return false;

    // Returning true for the already-equal stage is safe and idempotent because
    // no native mutation or persistent side effect occurs.
    if (targetStage == pQuest->currentStage)
        return true;

    // Durability policy and controlled-activation check
    if (!PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation())
        return false;

    PartyQuestStageMutationContext context;
    context.CollectSnapshot = [&]() -> std::optional<QuestSnapshot>
    {
        return QuestSnapshotCollector::Collect(pQuest, aModSystem);
    };
    context.ContainsStage = [&](uint16_t aStage) noexcept -> bool
    {
        return ContainsStage(*pQuest, aStage);
    };
    context.NativeSetStage = [&](uint16_t aStage) noexcept -> bool
    {
        return pQuest->SetStage(aStage);
    };
    context.IsPapyrusQuiescent = [](uint64_t aTransactionId) noexcept -> bool
    {
        const auto obs = PartyQuestSkyrimPapyrusRuntimeObserver::GetProcessObserver().Observe(aTransactionId);
        return obs.Status == PartyQuestPapyrusRuntimeObservationStatus::Idle;
    };
    context.IsCurrentUpdateThread = []() noexcept -> bool
    {
        return PartyQuestSkyrimRuntimeThread::IsCurrentUpdateThread();
    };
    context.OnRecoveryRequired = [](uint64_t aTransactionId, PartyQuestStageMutationStatus aReason) noexcept
    {
        spdlog::error(
            "PartyQuestSkyrimStageMutationExecutor entering recovery: "
            "transaction={} reason={}",
            aTransactionId, PartyQuestStageMutationStatusName(aReason));
    };

    auto& sessionOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    auto* pGuarded = sessionOwner.IsBound() ? sessionOwner.GetGuardedSession() : nullptr;
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();

    std::optional<std::reference_wrapper<PartyQuestRuntimeGuardedSession>> guardedOpt;
    if (pGuarded)
        guardedOpt = *pGuarded;

    const auto result = PartyQuestStageMutationAdapter::Execute(acRequest, context, guardedOpt, fence);

    if (result.WasApplied())
    {
        spdlog::info(
            "PartyQuestSkyrimStageMutationExecutor: stage mutation completed: "
            "quest={:016X} targetStage={} status={}",
            acRequest.CanonicalSnapshot.QuestId.LogFormat(), targetStage, PartyQuestStageMutationStatusName(result.Status));
        return true;
    }

    spdlog::warn(
        "PartyQuestSkyrimStageMutationExecutor: mutation rejected fail-closed: "
        "quest={:016X} targetStage={} status={}",
        acRequest.CanonicalSnapshot.QuestId.LogFormat(), targetStage, PartyQuestStageMutationStatusName(result.Status));
    return false;
}
