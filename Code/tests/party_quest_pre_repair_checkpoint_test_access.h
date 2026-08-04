#pragma once

#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

class PartyQuestRuntimePreRepairCheckpointTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimePreRepairCoreAuthorization MakeCoreAuthorization(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept
    {
        return PartyQuestRuntimePreRepairCoreAuthorization(
            aTransactionId,
            aTargetWorldRevision,
            acCoreFiles);
    }

    [[nodiscard]] static PartyQuestRuntimeCheckpointCoverageAuthorization MakeCoverageAuthorization(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept
    {
        return PartyQuestRuntimeCheckpointCoverageAuthorization(
            aTransactionId,
            aTargetWorldRevision,
            acPlan);
    }
};
