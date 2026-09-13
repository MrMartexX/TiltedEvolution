#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>

#include <algorithm>

namespace
{
uint64_t Mix(uint64_t hash, uint64_t value) noexcept
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

bool IsSortedUnique(const std::vector<uint16_t>& values) noexcept
{
    return std::adjacent_find(values.begin(), values.end(),
        [](uint16_t left, uint16_t right) { return left >= right; }) == values.end();
}

bool SameIdentity(const PartyQuestTransitionIdentity& left,
    const PartyQuestTransitionIdentity& right) noexcept
{
    return left == right;
}

PartyQuestTransitionAdmissionDecision Reject(
    PartyQuestTransitionAdmissionStatus status, const char* reason,
    bool structural = false, bool environment = false) noexcept
{
    return {status, structural, environment, false, 0, reason};
}

uint64_t Fingerprint(const PartyQuestReviewedTransition& reviewed) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    constexpr uint64_t domain = 0x5452414E5341444Dull; // "TRANSADM"
    hash = Mix(hash, domain);
    hash = Mix(hash, reviewed.Identity.QuestId.ModId);
    hash = Mix(hash, reviewed.Identity.QuestId.BaseId);
    hash = Mix(hash, reviewed.Identity.SourceStage);
    hash = Mix(hash, reviewed.Identity.TargetStage);
    hash = Mix(hash, reviewed.Identity.PolicyVersion);
    hash = Mix(hash, reviewed.Identity.EnvironmentFingerprint);
    hash = Mix(hash, reviewed.Identity.TopologyFingerprint);
    hash = Mix(hash, reviewed.Identity.WinningOverrideFingerprint);
    hash = Mix(hash, reviewed.Identity.ScriptFingerprint);
    hash = Mix(hash, reviewed.AnalyzerSchemaVersion);
    hash = Mix(hash, reviewed.AnalyzerVersion);
    hash = Mix(hash, reviewed.AnalyzerProvenanceFingerprint);
    hash = Mix(hash, reviewed.ReviewerFingerprint);
    return hash == 0 ? 1 : hash;
}
}

PartyQuestTransitionAdmissionDecision PartyQuestCompatibilityAdmissionPolicy::Evaluate(
    const PartyQuestReviewedTransition& acReviewed,
    const PartyQuestTransitionEvidence& acEvidence,
    uint64_t aLastAcceptedRevision,
    uint64_t aLastOperationId) noexcept
{
    try
    {
        const auto& identity = acReviewed.Identity;
        if (!identity.QuestId || identity.SourceStage >= identity.TargetStage ||
            identity.PolicyVersion == 0 || identity.EnvironmentFingerprint == 0 ||
            identity.TopologyFingerprint == 0 || identity.WinningOverrideFingerprint == 0 ||
            identity.ScriptFingerprint == 0 || acReviewed.AnalyzerSchemaVersion == 0 ||
            acReviewed.AnalyzerVersion == 0 || acReviewed.AnalyzerProvenanceFingerprint == 0 ||
            acReviewed.ReviewerFingerprint == 0 ||
            acReviewed.RunningState == PartyQuestExpectedRunningState::Unknown ||
            acReviewed.RecoveryClass == PartyQuestRecoveryClass::Unknown ||
            !IsSortedUnique(acReviewed.RequiredPriorStages) ||
            !IsSortedUnique(acReviewed.RequiredObjectives) ||
            acReviewed.RequiredPriorStages.size() > PartyQuestTransitionEvidence::MaxCollectionEntries ||
            acReviewed.RequiredObjectives.size() > PartyQuestTransitionEvidence::MaxCollectionEntries)
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported, "invalid-reviewed-contract");

        const auto& evidenceIdentity = acEvidence.Identity;
        if (!SameIdentity(identity, evidenceIdentity))
        {
            const bool environment = identity.QuestId == evidenceIdentity.QuestId &&
                identity.SourceStage == evidenceIdentity.SourceStage &&
                identity.TargetStage == evidenceIdentity.TargetStage;
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
                "exact-transition-identity-mismatch", false, environment);
        }
        if (acEvidence.AnalyzerSchemaVersion != acReviewed.AnalyzerSchemaVersion ||
            acEvidence.AnalyzerVersion != acReviewed.AnalyzerVersion ||
            acEvidence.AnalyzerProvenanceFingerprint != acReviewed.AnalyzerProvenanceFingerprint)
            return Reject(PartyQuestTransitionAdmissionStatus::NeedsReview,
                "analyzer-provenance-mismatch", false, true);

        if (acEvidence.AuthoritativeRevision == 0 || acEvidence.OperationId == 0)
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
                "missing-authoritative-identity", false, true);
        if (acEvidence.AuthoritativeRevision < aLastAcceptedRevision)
            return Reject(PartyQuestTransitionAdmissionStatus::StaleRevision,
                "stale-authoritative-revision", false, true);
        if (acEvidence.AuthoritativeRevision == aLastAcceptedRevision &&
            aLastAcceptedRevision != 0)
            return Reject(acEvidence.OperationId == aLastOperationId
                    ? PartyQuestTransitionAdmissionStatus::Duplicate
                    : PartyQuestTransitionAdmissionStatus::Retired,
                acEvidence.OperationId == aLastOperationId
                    ? "duplicate-operation" : "revision-already-retired", false, true);
        if (acEvidence.HasIntermediateStage)
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
                "implicit-multi-stage-jump", false, true);
        if (acEvidence.SceneCount != 0 || acEvidence.AliasCount != 0 ||
            acEvidence.CreatedReferenceCount != 0 || acEvidence.HasPlayerAlias ||
            acEvidence.HasInventoryMutation || acEvidence.HasQuestObjectMutation ||
            acEvidence.HasWorldMutation || acEvidence.HasAliasMutation ||
            acEvidence.HasUnboundedScriptEffects)
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
                "forbidden-or-unbounded-side-effect", false, true);
        if (!acEvidence.SideEffectSurfaceKnown ||
            !acEvidence.ReadinessEvidenceKnown || !acEvidence.QuiescencePolicyKnown ||
            !acEvidence.PostconditionsKnown ||
            acEvidence.RunningState == PartyQuestExpectedRunningState::Unknown ||
            acEvidence.RecoveryClass == PartyQuestRecoveryClass::Unknown)
            return Reject(PartyQuestTransitionAdmissionStatus::NeedsReview,
                "unknown-required-evidence", false, true);
        if (acEvidence.RunningState != acReviewed.RunningState ||
            acEvidence.RequiresReadiness != acReviewed.RequiresReadiness ||
            acEvidence.RequiresQuiescence != acReviewed.RequiresQuiescence ||
            acEvidence.RecoveryClass != acReviewed.RecoveryClass ||
            acEvidence.HasPlayerSpecificPreconditions != acReviewed.PlayerSpecific ||
            acEvidence.HasPartySpecificPreconditions != acReviewed.PartySpecific ||
            acEvidence.PriorStages != acReviewed.RequiredPriorStages ||
            acEvidence.Objectives != acReviewed.RequiredObjectives)
            return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
                "reviewed-precondition-mismatch", false, true);

        PartyQuestTransitionAdmissionDecision result;
        result.Status = PartyQuestTransitionAdmissionStatus::Eligible;
        result.StructuralEligibility = true;
        result.EnvironmentCompatible = true;
        result.AuthorizationGranted = false;
        result.TransitionFingerprint = Fingerprint(acReviewed);
        result.Reason = "exact-reviewed-transition-eligible-not-authorized";
        return result;
    }
    catch (...)
    {
        return Reject(PartyQuestTransitionAdmissionStatus::Unsupported,
            "admission-exception");
    }
}

size_t PartyQuestReviewedTransitionManifest::KeyHash::operator()(
    const PartyQuestTransitionIdentity& value) const noexcept
{
    return static_cast<size_t>(Mix(Mix(Mix(value.EnvironmentFingerprint,
        value.QuestId.LogFormat()), value.SourceStage), value.TargetStage));
}

bool PartyQuestReviewedTransitionManifest::Publish(
    std::span<const PartyQuestReviewedTransition> acReviewed) noexcept
{
    try
    {
        decltype(m_entries) candidate;
        candidate.reserve(acReviewed.size());
        for (const auto& entry : acReviewed)
        {
            PartyQuestTransitionEvidence proof;
            proof.Identity = entry.Identity;
            proof.AnalyzerSchemaVersion = entry.AnalyzerSchemaVersion;
            proof.AnalyzerVersion = entry.AnalyzerVersion;
            proof.AnalyzerProvenanceFingerprint = entry.AnalyzerProvenanceFingerprint;
            proof.AuthoritativeRevision = 1;
            proof.OperationId = 1;
            proof.RunningState = entry.RunningState;
            proof.PriorStages = entry.RequiredPriorStages;
            proof.Objectives = entry.RequiredObjectives;
            proof.HasPlayerSpecificPreconditions = entry.PlayerSpecific;
            proof.HasPartySpecificPreconditions = entry.PartySpecific;
            proof.SideEffectSurfaceKnown = true;
            proof.ReadinessEvidenceKnown = true;
            proof.RequiresReadiness = entry.RequiresReadiness;
            proof.QuiescencePolicyKnown = true;
            proof.RequiresQuiescence = entry.RequiresQuiescence;
            proof.PostconditionsKnown = true;
            proof.RecoveryClass = entry.RecoveryClass;
            if (!PartyQuestCompatibilityAdmissionPolicy::Evaluate(entry, proof).IsEligible() ||
                !candidate.emplace(entry.Identity, entry).second)
                return false;
        }
        m_entries.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

const PartyQuestReviewedTransition* PartyQuestReviewedTransitionManifest::Find(
    const PartyQuestTransitionIdentity& acIdentity) const noexcept
{
    const auto it = m_entries.find(acIdentity);
    return it == m_entries.end() ? nullptr : &it->second;
}
