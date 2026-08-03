#pragma once

#include <Structs/Skyrim/PartyQuestReplicaManifest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

enum class PartyQuestReplicaRestorePlanStatus : uint8_t
{
    Ready,
    InvalidIdentity,
    NotCheckpointManifest,
    CheckpointVerificationFailed,
    InvalidCheckpointPath,
    InvalidDestinationPath,
    DuplicateDestination
};

struct PartyQuestReplicaRestoreOperation
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path CheckpointSourcePath;
    std::filesystem::path ReplicaDestinationPath;
    uint64_t ExpectedSize{};
    uint64_t ExpectedDigest{};

    bool operator==(const PartyQuestReplicaRestoreOperation&) const = default;
};

struct PartyQuestReplicaRestorePlan
{
    PartyQuestReplicaRestorePlanStatus Status{PartyQuestReplicaRestorePlanStatus::InvalidIdentity};
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    PartyQuestCheckpointKind CheckpointKind{PartyQuestCheckpointKind::PreRepair};
    uint64_t CampaignWorldRevision{};
    std::vector<PartyQuestReplicaRestoreOperation> Operations;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestReplicaRestorePlanStatus::Ready;
    }
};

/**
 * Builds a non-executing restore plan from a previously verified checkpoint.
 *
 * Sources are constrained to the selected checkpoint tree and destinations are
 * constrained to the current co-op replica's saves/ and external sidecars/.
 * Solo-save paths, metadata, and the runtime-apply journal are never restore
 * destinations here. The planner performs no writes, deletes, or renames.
 */
class PartyQuestReplicaRestorePlanner final
{
public:
    [[nodiscard]] static PartyQuestReplicaRestorePlan Build(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const PartyQuestReplicaManifest& acCheckpointManifest) noexcept;
};
