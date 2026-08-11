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
};
