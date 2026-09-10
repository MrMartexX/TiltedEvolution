#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

namespace
{
uint64_t MixCompatibilityFingerprint(uint64_t aHash, uint64_t aValue) noexcept
{
    aHash ^= aValue + 0x9E3779B97F4A7C15ull + (aHash << 6) + (aHash >> 2);
    return aHash;
}

uint64_t ComputeCompatibilityFingerprint(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement) noexcept
{
    uint64_t hash = 0xA24BAED4963EE407ull;
    hash = MixCompatibilityFingerprint(hash, acRequirement.QuestId.ModId);
    hash = MixCompatibilityFingerprint(hash, acRequirement.QuestId.BaseId);
    hash = MixCompatibilityFingerprint(hash, acRequirement.ProfileVersion);
    hash = MixCompatibilityFingerprint(hash, acRequirement.ResolvedRecordFingerprint);
    hash = MixCompatibilityFingerprint(hash, acRequirement.WinningOverrideFingerprint);
    hash = MixCompatibilityFingerprint(hash, acRequirement.ScriptFingerprint);
    hash = MixCompatibilityFingerprint(hash, acRequirement.NativeAdapterFingerprint);
    hash = MixCompatibilityFingerprint(
        hash,
        static_cast<uint32_t>(acRequirement.AdapterMutationComponents));
    return hash != 0 ? hash : 1;
}
} // namespace

bool PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement) noexcept
{
    return acRequirement.QuestId &&
        acRequirement.ProfileVersion != 0 &&
        acRequirement.ResolvedRecordFingerprint != 0 &&
        acRequirement.WinningOverrideFingerprint != 0 &&
        acRequirement.ScriptFingerprint != 0 &&
        acRequirement.NativeAdapterFingerprint != 0 &&
        acRequirement.AdapterMutationComponents ==
            PartyQuestVerificationComponent::QuestSnapshot;
}

PartyQuestRuntimeCompatibilityDecision PartyQuestRuntimeCompatibilityPolicy::Evaluate(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
    const PartyQuestRuntimeCompatibilityFacts& acFacts) noexcept
{
    PartyQuestRuntimeCompatibilityDecision decision;

    if (!IsValidRequirement(acRequirement))
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::InvalidRequirement;
        return decision;
    }

    if (acFacts.ProfileVersion == 0 ||
        acFacts.ResolvedRecordFingerprint == 0 ||
        acFacts.WinningOverrideFingerprint == 0 ||
        acFacts.ScriptFingerprint == 0 ||
        acFacts.NativeAdapterFingerprint == 0 ||
        acFacts.AdapterMutationComponents == PartyQuestVerificationComponent::None)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::InvalidClientFacts;
        return decision;
    }

    if (acFacts.ProfileVersion != acRequirement.ProfileVersion)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::ProfileVersionMismatch;
        return decision;
    }

    if (acFacts.ResolvedRecordFingerprint != acRequirement.ResolvedRecordFingerprint)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::ResolvedRecordMismatch;
        return decision;
    }

    if (acFacts.WinningOverrideFingerprint != acRequirement.WinningOverrideFingerprint)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::WinningOverrideMismatch;
        return decision;
    }

    if (acFacts.ScriptFingerprint != acRequirement.ScriptFingerprint)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::ScriptMismatch;
        return decision;
    }

    if (acFacts.NativeAdapterFingerprint != acRequirement.NativeAdapterFingerprint)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::NativeAdapterMismatch;
        return decision;
    }

    if (acFacts.AdapterMutationComponents != acRequirement.AdapterMutationComponents)
    {
        decision.Status =
            PartyQuestRuntimeCompatibilityStatus::AdapterMutationCoverageMismatch;
        return decision;
    }

    const uint64_t compatibilityFingerprint =
        ComputeCompatibilityFingerprint(acRequirement);
    if (compatibilityFingerprint == 0)
    {
        decision.Status = PartyQuestRuntimeCompatibilityStatus::InvalidRequirement;
        return decision;
    }

    decision.Status = PartyQuestRuntimeCompatibilityStatus::Authorized;
    decision.SafetyProfile = PartyQuestRuntimeSafetyProfile(
        acRequirement.QuestId,
        compatibilityFingerprint,
        acRequirement.AdapterMutationComponents);
    return decision;
}

bool PartyQuestRuntimeCompatibilityManifest::AddRequirement(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    if (!PartyQuestRuntimeCompatibilityPolicy::IsValidRequirement(acRequirement))
        return false;

    return m_requirements.emplace(acRequirement.QuestId, acRequirement).second;
}

const PartyQuestRuntimeCompatibilityRequirement*
PartyQuestRuntimeCompatibilityManifest::FindRequirement(
    const GameId& acQuestId) const noexcept
{
    const auto it = m_requirements.find(acQuestId);
    return it != m_requirements.end() ? &it->second : nullptr;
}

PartyQuestRuntimeCompatibilityDecision PartyQuestRuntimeCompatibilityManifest::Evaluate(
    const GameId& acQuestId,
    const PartyQuestRuntimeCompatibilityFacts& acFacts) const noexcept
{
    const PartyQuestRuntimeCompatibilityRequirement* pRequirement = FindRequirement(acQuestId);
    if (!pRequirement)
        return {};

    return PartyQuestRuntimeCompatibilityPolicy::Evaluate(*pRequirement, acFacts);
}
