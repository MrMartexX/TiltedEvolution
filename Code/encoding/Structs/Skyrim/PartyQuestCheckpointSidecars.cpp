#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>

#include <algorithm>
#include <type_traits>

namespace
{
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& aHash, const void* apData, size_t aSize) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(apData);
    for (size_t i = 0; i < aSize; ++i)
    {
        aHash ^= bytes[i];
        aHash *= kFnvPrime;
    }
}

template <class T>
void HashValue(uint64_t& aHash, const T& acValue) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    HashBytes(aHash, &acValue, sizeof(T));
}

bool IsValidMode(PartyQuestCheckpointSidecarRequirementMode aMode) noexcept
{
    return aMode == PartyQuestCheckpointSidecarRequirementMode::Required ||
        aMode == PartyQuestCheckpointSidecarRequirementMode::Optional;
}
} // namespace

bool PartyQuestCheckpointSidecarPolicy::IsValidRequirement(
    const PartyQuestCheckpointSidecarRequirement& acRequirement) noexcept
{
    return acRequirement.CapabilityId != 0 &&
        acRequirement.SchemaVersion != 0 &&
        acRequirement.ProviderFingerprint != 0 &&
        acRequirement.RestoreAdapterFingerprint != 0 &&
        IsValidMode(acRequirement.Mode);
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

uint64_t PartyQuestCheckpointSidecarManifest::ComputeFingerprint() const noexcept
{
    try
    {
        uint64_t hash = kFnvOffset;
        const auto requirements = GetRequirements();
        const uint64_t count = static_cast<uint64_t>(requirements.size());
        HashValue(hash, count);

        for (const auto& requirement : requirements)
        {
            if (!PartyQuestCheckpointSidecarPolicy::IsValidRequirement(requirement))
                return 0;

            HashValue(hash, requirement.CapabilityId);
            HashValue(hash, requirement.SchemaVersion);
            HashValue(hash, requirement.ProviderFingerprint);
            HashValue(hash, requirement.RestoreAdapterFingerprint);
            const auto mode = static_cast<uint8_t>(requirement.Mode);
            HashValue(hash, mode);
        }

        // FNV is not an authentication primitive; this value is only an
        // internal deterministic contract identity. Reserve zero as invalid.
        return hash != 0 ? hash : 1;
    }
    catch (...)
    {
        return 0;
    }
}
