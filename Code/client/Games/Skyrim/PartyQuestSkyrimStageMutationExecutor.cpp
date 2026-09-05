#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimStageMutationExecutor.h>
#include <PartyQuestSkyrimRuntimeThread.h>

#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>

#include <Forms/TESQuest.h>
#include <Services/QuestSnapshotCollector.h>
#include <Systems/ModSystem.h>

namespace
{
constexpr uint32_t kAllowedStageOnlyActions =
    static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) |
    static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
    static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify);

bool HasOnlyStageMutationSurface(const QuestSnapshot& acSnapshot) noexcept
{
    return acSnapshot.Status == QuestSnapshotStatus::Running &&
        acSnapshot.ReferenceAliases.empty() &&
        acSnapshot.LocationAliases.empty() &&
        acSnapshot.CreatedReferences.empty() &&
        !acSnapshot.SceneParticipantPlayerId.has_value();
}

bool IsNarrowStagePlan(const PartyQuestRuntimeApplyRequest& acRequest) noexcept
{
    const uint32_t actions = static_cast<uint32_t>(acRequest.Plan.Actions);
    return acRequest.TransactionId != 0 &&
        !acRequest.Plan.DryRunOnly &&
        acRequest.Plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe &&
        acRequest.Plan.MutationAuthorization.IsVerified() &&
        acRequest.Plan.MutationAuthorization.Matches(
            acRequest.CanonicalSnapshot,
            acRequest.Plan.Actions,
            false) &&
        acRequest.Plan.MutationAuthorization.GetAdapterMutationComponents() ==
            PartyQuestVerificationComponent::QuestSnapshot &&
        HasPartyQuestApplyAction(
            acRequest.Plan.Actions,
            PartyQuestApplyAction::StageTransition) &&
        (actions & ~kAllowedStageOnlyActions) == 0 &&
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

bool PartyQuestSkyrimStageMutationExecutor::Execute(
    const PartyQuestRuntimeApplyRequest& acRequest,
    ModSystem& aModSystem) noexcept
{
    if (!PartyQuestSkyrimRuntimeThread::IsCurrentUpdateThread() ||
        !IsNarrowStagePlan(acRequest))
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
    if (!aModSystem.GetServerModId(pQuest->formID, roundTripId) ||
        roundTripId != acRequest.CanonicalSnapshot.QuestId)
    {
        return false;
    }

    const auto current = QuestSnapshotCollector::Collect(pQuest, aModSystem);
    if (!current ||
        current->QuestId != acRequest.CanonicalSnapshot.QuestId ||
        !HasOnlyStageMutationSurface(*current))
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

    // INV-DURABILITY-001: a future native SetStage side effect may not execute
    // merely because process-crash persistence succeeded. Production mutation
    // remains physically fenced until file/device and parent-directory stable
    // storage semantics raise CurrentLocalGuarantee to PowerLossDurable.
    if (!PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation())
        return false;

    // P0 safety boundary: the adapter may prove that a request is structurally
    // eligible, but it must not execute canonical Skyrim mutation yet. Keep the
    // native SetStage call physically absent until P0-A..H are closed on one
    // final GREEN SHA and the mutation milestone explicitly re-enables it with
    // its own live proof.
    return false;
}
