#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

namespace
{
constexpr uint64_t kVerificationFnvOffset = 14695981039346656037ull;
constexpr uint64_t kVerificationFnvPrime = 1099511628211ull;
constexpr uint32_t kKnownApplyActions =
    static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) |
    static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForWorldTargets) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
    static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify) |
    static_cast<uint32_t>(PartyQuestApplyAction::AdapterManaged);

void MixVerification(uint64_t& aHash, uint64_t aValue) noexcept
{
    for (size_t index = 0; index < sizeof(aValue); ++index)
    {
        aHash ^= static_cast<uint8_t>((aValue >> (index * 8)) & 0xFF);
        aHash *= kVerificationFnvPrime;
    }
}

PartyQuestVerificationComponent RequiredCoverage(PartyQuestApplyAction aActions) noexcept
{
    const uint32_t actions = static_cast<uint32_t>(aActions);
    if (actions == 0 || (actions & ~kKnownApplyActions) != 0)
        return PartyQuestVerificationComponent::None;

    // Every currently authorized mutation changes only state represented by
    // QuestSnapshot. Control actions add no canonical mutation surface. Any
    // future alias/inventory/world action must extend this mapping before it can
    // be admitted by the known-action mask.
    if (!HasPartyQuestApplyAction(aActions, PartyQuestApplyAction::StageTransition) &&
        !HasPartyQuestApplyAction(aActions, PartyQuestApplyAction::AdapterManaged))
    {
        return PartyQuestVerificationComponent::None;
    }

    return PartyQuestVerificationComponent::QuestSnapshot |
        PartyQuestVerificationComponent::Compatibility;
}
} // namespace

uint64_t PartyQuestVerificationEnvelopeV1::ComputeFingerprint() const noexcept
{
    uint64_t hash = kVerificationFnvOffset;
    MixVerification(hash, SchemaVersion);
    MixVerification(hash, static_cast<uint32_t>(Required));
    MixVerification(hash, QuestSnapshotDigest);
    MixVerification(hash, AliasDigest);
    MixVerification(hash, InventoryEffectsDigest);
    MixVerification(hash, WorldEffectsDigest);
    MixVerification(hash, AdapterStateDigest);
    MixVerification(hash, CompatibilityFingerprint);
    return hash != 0 ? hash : 1;
}

std::optional<PartyQuestVerificationEnvelopeV1> PartyQuestVerificationPolicy::BuildExpected(
    PartyQuestApplyAction aActions,
    uint64_t aQuestSnapshotDigest,
    uint64_t aCompatibilityFingerprint) noexcept
{
    const auto required = RequiredCoverage(aActions);
    if (required == PartyQuestVerificationComponent::None ||
        aQuestSnapshotDigest == 0 ||
        aCompatibilityFingerprint == 0)
    {
        return std::nullopt;
    }

    PartyQuestVerificationEnvelopeV1 envelope;
    envelope.Required = required;
    envelope.QuestSnapshotDigest = aQuestSnapshotDigest;
    envelope.CompatibilityFingerprint = aCompatibilityFingerprint;
    return envelope;
}

bool PartyQuestVerificationPolicy::IsCompleteForActions(
    const PartyQuestVerificationEnvelopeV1& acEnvelope,
    PartyQuestApplyAction aActions) noexcept
{
    const auto expected = BuildExpected(
        aActions,
        acEnvelope.QuestSnapshotDigest,
        acEnvelope.CompatibilityFingerprint);
    return expected && *expected == acEnvelope;
}

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
            acProfile.GetAdapterMutationComponents(),
            plan.Actions,
            plan.DryRunOnly);
    }

    return plan;
}
