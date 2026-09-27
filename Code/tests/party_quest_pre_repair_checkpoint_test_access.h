#pragma once

#include <Structs/Skyrim/PartyQuestRuntimePreRepairCheckpoint.h>

class PartyQuestRuntimePreRepairCheckpointTestAccess final
{
public:
    [[nodiscard]] static PartyQuestCheckpointCaptureEpoch MakeExpiredEpoch(
        uint64_t aEpochId,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        uint64_t aSidecarManifestFingerprint) noexcept
    {
        return PartyQuestCheckpointCaptureEpoch(
            aEpochId,
            aTransactionId,
            aTargetWorldRevision,
            aSidecarManifestFingerprint,
            1);
    }

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
