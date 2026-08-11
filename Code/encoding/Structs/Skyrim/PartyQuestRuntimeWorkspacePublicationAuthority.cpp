#include <Structs/Skyrim/PartyQuestRuntimeWorkspacePublicationAuthority.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace
{
using PublicationCapability =
    PartyQuestReplicaWorkspacePublicationCapability;

std::mutex& GetPublicationAuthorityMutex() noexcept
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<const PartyQuestRuntimeApplySession*, PublicationCapability>&
GetPublicationAuthorities()
{
    static std::unordered_map<
        const PartyQuestRuntimeApplySession*,
        PublicationCapability> authorities;
    return authorities;
}
} // namespace

bool PartyQuestRuntimeWorkspacePublicationAuthority::Bind(
    const PartyQuestRuntimeApplySession& acSession,
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaWorkspacePublicationCapability aCapability) noexcept
{
    try
    {
        if (!acSession.GetCampaignId().IsValid() ||
            !acSession.GetPlayerProfileId().IsValid() ||
            !PartyQuestCoopSaveLayout::Matches(
                acPaths,
                acSession.GetCampaignId(),
                acSession.GetPlayerProfileId()) ||
            !aCapability.Protects(
                acPaths,
                acSession.GetCampaignId(),
                acSession.GetPlayerProfileId()))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(GetPublicationAuthorityMutex());
        const auto [iterator, inserted] = GetPublicationAuthorities().emplace(
            &acSession,
            std::move(aCapability));
        return inserted && iterator->second.IsVerified();
    }
    catch (...)
    {
        return false;
    }
}

void PartyQuestRuntimeWorkspacePublicationAuthority::Unbind(
    const PartyQuestRuntimeApplySession& acSession) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(GetPublicationAuthorityMutex());
        GetPublicationAuthorities().erase(&acSession);
    }
    catch (...)
    {
    }
}

PartyQuestReplicaWorkspacePublicationCapability
PartyQuestRuntimeWorkspacePublicationAuthority::Acquire(
    const PartyQuestRuntimeApplySession& acSession,
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        PublicationCapability capability;
        {
            std::lock_guard<std::mutex> lock(GetPublicationAuthorityMutex());
            const auto iterator = GetPublicationAuthorities().find(&acSession);
            if (iterator == GetPublicationAuthorities().end())
                return {};
            capability = iterator->second;
        }

        if (!capability.Protects(
                acPaths,
                acSession.GetCampaignId(),
                acSession.GetPlayerProfileId()))
        {
            return {};
        }
        return capability;
    }
    catch (...)
    {
        return {};
    }
}
