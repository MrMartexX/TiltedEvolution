#pragma once

#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

class PartyQuestRuntimePreRepairCheckpointTestAccess final
{
public:
    /** Legacy diagnostic authorization; production assembler rejects epochless proof. */
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

    [[nodiscard]] static PartyQuestRuntimePreRepairCoreAuthorization MakeCoreAuthorization(
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestReplicaFileSpec>& acCoreFiles) noexcept
    {
        return PartyQuestRuntimePreRepairCoreAuthorization(acEpoch, acCoreFiles);
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
