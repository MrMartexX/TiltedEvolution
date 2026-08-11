#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreExecutor.h>

/** Test-only access to the capability-bearing restore entry points. */
class PartyQuestReplicaRestoreExecutorTestAccess final
{
public:
    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport ExecuteAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept
    {
        return PartyQuestReplicaRestoreExecutor::ExecuteAuthorized(
            acPaths,
            acPlan,
            aRestoreId,
            acWorkspaceCapability,
            aHooks);
    }

    [[nodiscard]] static PartyQuestReplicaRestoreExecutionReport RecoverAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const std::filesystem::path& acJournalPath,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability,
        PartyQuestReplicaRestoreExecutionHooks aHooks = {}) noexcept
    {
        return PartyQuestReplicaRestoreExecutor::RecoverAuthorized(
            acPaths,
            acExpectedCampaignId,
            acExpectedPlayerProfileId,
            acJournalPath,
            acWorkspaceCapability,
            aHooks);
    }
};
