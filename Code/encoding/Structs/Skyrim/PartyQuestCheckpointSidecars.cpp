#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>

#include <algorithm>

bool PartyQuestCheckpointSidecarPolicy::IsValidRequirement(
    const PartyQuestCheckpointSidecarRequirement& acRequirement) noexcept
{
    return acRequirement.CapabilityId != 0 &&
        acRequirement.SchemaVersion != 0 &&
        acRequirement.ProviderFingerprint != 0 &&
        acRequirement.RestoreAdapterFingerprint != 0;
}

bool PartyQuestCheckpointSidecarPolicy::IsValidFacts(
    const PartyQuestCheckpointSidecarFacts& acFacts) noexcept
{
    return acFacts.CapabilityId != 0 &&
        acFacts.SchemaVersion != 0 &&
        acFacts.ProviderFingerprint != 0 &&
        acFacts.RestoreAdapterFingerprint != 0;
}

PartyQuestCheckpointSidecarDecision PartyQuestCheckpointSidecarPolicy::Evaluate(
    const PartyQuestCheckpointSidecarRequirement& acRequirement,
    const PartyQuestCheckpointSidecarFacts* apFacts) noexcept
{
    PartyQuestCheckpointSidecarDecision decision;

    if (!IsValidRequirement(acRequirement))
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::InvalidRequirement;
        return decision;
    }

    if (!apFacts)
    {
        decision.Status =
            acRequirement.Mode == PartyQuestCheckpointSidecarRequirementMode::Optional
            ? PartyQuestCheckpointSidecarStatus::OptionalUnavailable
            : PartyQuestCheckpointSidecarStatus::RequiredUnavailable;
        return decision;
    }

    if (!IsValidFacts(*apFacts))
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::InvalidFacts;
        return decision;
    }

    if (apFacts->CapabilityId != acRequirement.CapabilityId)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::CapabilityMismatch;
        return decision;
    }

    if (apFacts->SchemaVersion != acRequirement.SchemaVersion)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::SchemaVersionMismatch;
        return decision;
    }

    if (apFacts->ProviderFingerprint != acRequirement.ProviderFingerprint)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::ProviderMismatch;
        return decision;
    }

    if (apFacts->RestoreAdapterFingerprint != acRequirement.RestoreAdapterFingerprint)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::RestoreAdapterMismatch;
        return decision;
    }

    if (!apFacts->CaptureAvailable)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::CaptureUnavailable;
        return decision;
    }

    if (!apFacts->RestoreAvailable)
    {
        decision.Status = PartyQuestCheckpointSidecarStatus::RestoreUnavailable;
        return decision;
    }

    decision.Status = PartyQuestCheckpointSidecarStatus::Authorized;
    decision.Authorization = PartyQuestCheckpointSidecarAuthorization(
        acRequirement.CapabilityId,
        acRequirement.SchemaVersion,
        acRequirement.ProviderFingerprint,
        acRequirement.RestoreAdapterFingerprint);
    return decision;
}

bool PartyQuestCheckpointSidecarManifest::AddRequirement(
    const PartyQuestCheckpointSidecarRequirement& acRequirement)
{
    if (!PartyQuestCheckpointSidecarPolicy::IsValidRequirement(acRequirement))
        return false;

    return m_requirements.emplace(
        acRequirement.CapabilityId,
        acRequirement).second;
}

const PartyQuestCheckpointSidecarRequirement*
PartyQuestCheckpointSidecarManifest::FindRequirement(
    uint64_t aCapabilityId) const noexcept
{
    const auto it = m_requirements.find(aCapabilityId);
    return it != m_requirements.end() ? &it->second : nullptr;
}

std::vector<PartyQuestCheckpointSidecarRequirement>
PartyQuestCheckpointSidecarManifest::GetRequirements() const
{
    std::vector<PartyQuestCheckpointSidecarRequirement> requirements;
    requirements.reserve(m_requirements.size());
    for (const auto& [capabilityId, requirement] : m_requirements)
    {
        (void)capabilityId;
        requirements.push_back(requirement);
    }

    std::sort(requirements.begin(), requirements.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.CapabilityId < acRight.CapabilityId;
    });
    return requirements;
}
