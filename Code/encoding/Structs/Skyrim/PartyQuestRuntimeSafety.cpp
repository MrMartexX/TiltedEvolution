#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

namespace
{
bool IsTerminalState(QuestSnapshotStatus aStatus) noexcept
{
    return aStatus == QuestSnapshotStatus::Stopped ||
        aStatus == QuestSnapshotStatus::Completed ||
        aStatus == QuestSnapshotStatus::Failed;
}
} // namespace

PartyQuestRuntimeSafetyFacts PartyQuestRuntimeSafetyPolicy::Inspect(
    const QuestSnapshot& acSnapshot) noexcept
{
    PartyQuestRuntimeSafetyFacts facts;
    facts.ReferenceAliasCount = acSnapshot.ReferenceAliases.size();
    facts.LocationAliasCount = acSnapshot.LocationAliases.size();
    facts.CreatedReferenceCount = acSnapshot.CreatedReferences.size();
    facts.ObjectiveCount = acSnapshot.Objectives.size();
    facts.HasSceneParticipant = acSnapshot.SceneParticipantPlayerId.has_value();
    facts.IsInactiveState = acSnapshot.Status == QuestSnapshotStatus::Inactive;
    facts.IsTerminalState = IsTerminalState(acSnapshot.Status);

    for (const QuestReferenceAliasSnapshot& alias : acSnapshot.ReferenceAliases)
    {
        if (!alias.ReferenceId)
            ++facts.UnresolvedReferenceAliasCount;
        if (alias.IsQuestObject)
            ++facts.QuestObjectAliasCount;
    }

    return facts;
}

PartyQuestRuntimeSafetyDecision PartyQuestRuntimeSafetyPolicy::Evaluate(
    const PartyQuestAdmissionDecision& acAdmission,
    const QuestSnapshot& acSnapshot,
    const PartyQuestRuntimeSafetyProfile& acProfile) noexcept
{
    PartyQuestRuntimeSafetyDecision decision;
    decision.Facts = Inspect(acSnapshot);

    if (!acAdmission.IsAdmitted())
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::Blocked;
        decision.Reason = PartyQuestRuntimeSafetyReason::AdmissionBlocked;
        return decision;
    }

    // A compatibility-authorized native adapter is the only generic escape
    // hatch to RuntimeSafe. The profile is quest-scoped and cannot be reused
    // for a different canonical QuestId.
    if (acProfile.IsVerifiedFor(acSnapshot.QuestId))
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
        decision.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
        return decision;
    }

    // Replaying an inactive target would imply undo/reset semantics. Generic
    // SetStage cannot safely roll a running local quest back to inactive.
    if (decision.Facts.IsInactiveState)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::InactiveQuestState;
        return decision;
    }

    if (decision.Facts.IsTerminalState)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::TerminalQuestState;
        return decision;
    }

    if (decision.Facts.CreatedReferenceCount != 0)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::CreatedReferences;
        return decision;
    }

    if (decision.Facts.LocationAliasCount != 0)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::LocationAliases;
        return decision;
    }

    if (decision.Facts.QuestObjectAliasCount != 0)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::QuestObjectAliases;
        return decision;
    }

    if (decision.Facts.UnresolvedReferenceAliasCount != 0)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::UnresolvedReferenceAliases;
        return decision;
    }

    if (decision.Facts.ReferenceAliasCount >= kComplexAliasThreshold)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::RequiresAdapter;
        decision.Reason = PartyQuestRuntimeSafetyReason::ComplexAliasTopology;
        return decision;
    }

    if (decision.Facts.HasSceneParticipant)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::Deferred;
        decision.Reason = PartyQuestRuntimeSafetyReason::SceneParticipantActive;
        return decision;
    }

    if (decision.Facts.ReferenceAliasCount != 0)
    {
        decision.Status = PartyQuestRuntimeSafetyStatus::Deferred;
        decision.Reason = PartyQuestRuntimeSafetyReason::ReferenceAliasesNeedWorld;
        return decision;
    }

    decision.Status = PartyQuestRuntimeSafetyStatus::StageOnly;
    decision.Reason = PartyQuestRuntimeSafetyReason::SimpleStageTransition;
    return decision;
}

PartyQuestApplyPlan PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
    const PartyQuestAdmissionDecision& acAdmission,
    const QuestSnapshot& acSnapshot,
    const PartyQuestRuntimeSafetyProfile& acProfile) noexcept
{
    PartyQuestApplyPlan plan;
    plan.Safety = Evaluate(acAdmission, acSnapshot, acProfile);

    switch (plan.Safety.Status)
    {
    case PartyQuestRuntimeSafetyStatus::Blocked:
        plan.Actions = PartyQuestApplyAction::None;
        break;

    case PartyQuestRuntimeSafetyStatus::RequiresAdapter:
        plan.Actions = PartyQuestApplyAction::ResnapshotAndVerify;
        if (plan.Safety.Facts.ObjectiveCount != 0)
            plan.Actions |= PartyQuestApplyAction::VerifyObjectives;
        break;

    case PartyQuestRuntimeSafetyStatus::Deferred:
        plan.Actions = PartyQuestApplyAction::StageTransition |
            PartyQuestApplyAction::WaitForWorldTargets |
            PartyQuestApplyAction::WaitForPapyrusQuiescence |
            PartyQuestApplyAction::ResnapshotAndVerify;
        if (plan.Safety.Facts.ObjectiveCount != 0)
            plan.Actions |= PartyQuestApplyAction::VerifyObjectives;
        break;

    case PartyQuestRuntimeSafetyStatus::StageOnly:
        plan.Actions = PartyQuestApplyAction::StageTransition |
            PartyQuestApplyAction::WaitForPapyrusQuiescence |
            PartyQuestApplyAction::ResnapshotAndVerify;
        if (plan.Safety.Facts.ObjectiveCount != 0)
            plan.Actions |= PartyQuestApplyAction::VerifyObjectives;
        break;

    case PartyQuestRuntimeSafetyStatus::RuntimeSafe:
        plan.Actions = PartyQuestApplyAction::AdapterManaged |
            PartyQuestApplyAction::WaitForPapyrusQuiescence |
            PartyQuestApplyAction::ResnapshotAndVerify;
        if (plan.Safety.Facts.ReferenceAliasCount != 0 ||
            plan.Safety.Facts.LocationAliasCount != 0 ||
            plan.Safety.Facts.CreatedReferenceCount != 0 ||
            plan.Safety.Facts.HasSceneParticipant)
        {
            plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
        }
        if (plan.Safety.Facts.ObjectiveCount != 0)
            plan.Actions |= PartyQuestApplyAction::VerifyObjectives;
        break;
    }

    // The executor is intentionally absent. This milestone only produces and
    // validates plans; it never mutates Skyrim runtime state.
    plan.DryRunOnly = true;

    if (plan.Safety.Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe &&
        acProfile.IsVerifiedFor(acSnapshot.QuestId))
    {
        const uint64_t canonicalDigest = acSnapshot.ComputeDigest();
        plan.MutationAuthorization = PartyQuestRuntimeMutationAuthorization(
            acSnapshot.QuestId,
            canonicalDigest,
            acProfile.GetCompatibilityFingerprint(),
            plan.Actions,
            plan.DryRunOnly);
    }

    return plan;
}
