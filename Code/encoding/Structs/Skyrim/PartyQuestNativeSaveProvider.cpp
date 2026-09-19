#include <Structs/Skyrim/PartyQuestNativeSaveProvider.h>

#include <utility>

PartyQuestNativeSaveProviderToken::PartyQuestNativeSaveProviderToken(
    uint64_t aRegistrationId,
    uint64_t aRuntimeGeneration) noexcept
    : m_registrationId(aRegistrationId)
    , m_runtimeGeneration(aRuntimeGeneration)
    , m_providerFingerprint(kPartyQuestNativeSaveProviderFingerprint)
{
}

PartyQuestNativeSaveProviderToken::PartyQuestNativeSaveProviderToken(
    PartyQuestNativeSaveProviderToken&& aOther) noexcept
    : m_registrationId(aOther.m_registrationId)
    , m_runtimeGeneration(aOther.m_runtimeGeneration)
    , m_providerFingerprint(aOther.m_providerFingerprint)
{
    aOther.Reset();
}

PartyQuestNativeSaveProviderToken&
PartyQuestNativeSaveProviderToken::operator=(
    PartyQuestNativeSaveProviderToken&& aOther) noexcept
{
    if (this != &aOther)
    {
        m_registrationId = aOther.m_registrationId;
        m_runtimeGeneration = aOther.m_runtimeGeneration;
        m_providerFingerprint = aOther.m_providerFingerprint;
        aOther.Reset();
    }
    return *this;
}

bool PartyQuestNativeSaveProviderToken::IsValid() const noexcept
{
    return m_registrationId != 0 && m_runtimeGeneration != 0 &&
        m_providerFingerprint == kPartyQuestNativeSaveProviderFingerprint;
}

void PartyQuestNativeSaveProviderToken::Reset() noexcept
{
    m_registrationId = 0;
    m_runtimeGeneration = 0;
    m_providerFingerprint = 0;
}

PartyQuestNativeSaveProviderRegistrationResult
PartyQuestNativeSaveProviderRegistration::RegisterAuthenticated(
    const PartyQuestNativeSaveProviderDescriptor& acDescriptor,
    uint64_t aRuntimeGeneration) noexcept
{
    PartyQuestNativeSaveProviderRegistrationResult result;
    if (!PartyQuestNativeSaveProviderPolicy::IsApprovedDescriptor(acDescriptor))
    {
        result.Status =
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidDescriptor;
        return result;
    }
    if (aRuntimeGeneration == 0)
    {
        result.Status =
            PartyQuestNativeSaveProviderRegistrationStatus::InvalidGeneration;
        return result;
    }
    if (m_active)
    {
        result.Status = PartyQuestNativeSaveProviderRegistrationStatus::Busy;
        return result;
    }
    if (m_nextRegistrationId == 0)
    {
        result.Status = PartyQuestNativeSaveProviderRegistrationStatus::Exhausted;
        return result;
    }

    m_registrationId = m_nextRegistrationId++;
    m_runtimeGeneration = aRuntimeGeneration;
    m_active = true;
    PartyQuestNativeSaveProviderToken token(
        m_registrationId, m_runtimeGeneration);
    result.Token.emplace(std::move(token));
    result.Status = PartyQuestNativeSaveProviderRegistrationStatus::Registered;
    return result;
}

PartyQuestNativeSaveProviderRegistrationStatus
PartyQuestNativeSaveProviderRegistration::Validate(
    const PartyQuestNativeSaveProviderToken& acToken,
    uint64_t aRuntimeGeneration) const noexcept
{
    if (!m_active)
        return PartyQuestNativeSaveProviderRegistrationStatus::Invalidated;
    if (!acToken.IsValid() || acToken.m_registrationId != m_registrationId ||
        acToken.m_runtimeGeneration != m_runtimeGeneration)
        return PartyQuestNativeSaveProviderRegistrationStatus::Stale;
    if (aRuntimeGeneration == 0 || aRuntimeGeneration != m_runtimeGeneration)
        return PartyQuestNativeSaveProviderRegistrationStatus::InvalidGeneration;
    return PartyQuestNativeSaveProviderRegistrationStatus::Current;
}

PartyQuestNativeSaveProviderRegistrationStatus
PartyQuestNativeSaveProviderRegistration::Invalidate(
    const PartyQuestNativeSaveProviderToken& acToken) noexcept
{
    if (!m_active)
        return PartyQuestNativeSaveProviderRegistrationStatus::Invalidated;
    if (!acToken.IsValid() || acToken.m_registrationId != m_registrationId ||
        acToken.m_runtimeGeneration != m_runtimeGeneration)
        return PartyQuestNativeSaveProviderRegistrationStatus::Stale;

    m_active = false;
    m_registrationId = 0;
    m_runtimeGeneration = 0;
    return PartyQuestNativeSaveProviderRegistrationStatus::Invalidated;
}
