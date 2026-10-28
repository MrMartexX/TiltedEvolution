#pragma once

#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

class PartyQuestRuntimeApplySession;

/**
 * Process-local binding between one hydrated runtime session and the exact
 * workspace-publication capability issued by its RuntimeSessionOwner lease.
 *
 * This is deliberately keyed by the concrete session object, not merely by
 * campaign/profile identity, so unrelated standalone snapshot managers cannot
 * borrow an owner's lease. Acquire() returns a proof copy that pins the native
 * lease state for the caller even if the owner begins releasing afterwards.
 */
class PartyQuestRuntimeWorkspacePublicationAuthority final
{
public:
    [[nodiscard]] static bool Bind(
        const PartyQuestRuntimeApplySession& acSession,
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestReplicaWorkspacePublicationCapability aCapability) noexcept;

    static void Unbind(
        const PartyQuestRuntimeApplySession& acSession) noexcept;

    [[nodiscard]] static PartyQuestReplicaWorkspacePublicationCapability Acquire(
        const PartyQuestRuntimeApplySession& acSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;
};
