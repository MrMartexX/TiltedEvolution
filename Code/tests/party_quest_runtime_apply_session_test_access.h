#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>

class PartyQuestRuntimeApplySessionTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeDurableTransitionStatus MarkCheckpointCreated(
        PartyQuestRuntimeApplySession& aSession,
        uint64_t aTransactionId)
    {
        return aSession.MarkCheckpointCreatedInternal(aTransactionId);
    }

    [[nodiscard]] static PartyQuestRuntimeDurableTransitionStatus ArmRuntimeMutation(
        PartyQuestRuntimeApplySession& aSession,
        uint64_t aTransactionId)
    {
        return aSession.ArmRuntimeMutationInternal(aTransactionId);
    }

    [[nodiscard]] static PartyQuestRuntimeDurableTransitionStatus CompleteLiveCheckpointRestore(
        PartyQuestRuntimeApplySession& aSession,
        uint64_t aTransactionId)
    {
        return aSession.CompleteLiveCheckpointRestoreInternal(aTransactionId);
    }

    [[nodiscard]] static PartyQuestRuntimeDurableTransitionStatus CompleteCrashCheckpointRestore(
        PartyQuestRuntimeApplySession& aSession,
        uint64_t aTransactionId)
    {
        return aSession.CompleteCrashCheckpointRestoreInternal(aTransactionId);
    }
};
