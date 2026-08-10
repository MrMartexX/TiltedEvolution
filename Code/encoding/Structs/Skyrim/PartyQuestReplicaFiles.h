#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestReplicaFileKind : uint8_t
{
    SkyrimSave,
    SkseCosave,
    ExternalSidecar
};

struct PartyQuestReplicaFileSpec
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path SourcePath;
    std::filesystem::path RelativePath;
    uint64_t Size{};
    uint64_t Digest{};

    bool operator==(const PartyQuestReplicaFileSpec&) const = default;
};

struct PartyQuestReplicaCopyOperation
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path SourcePath;
    std::filesystem::path DestinationPath;
    uint64_t ExpectedSize{};
    uint64_t ExpectedDigest{};

    bool operator==(const PartyQuestReplicaCopyOperation&) const = default;
};

struct PartyQuestReplicaCopyPlan;

/**
 * Local resource policy for one immutable replica publication. These limits
 * are never supplied by a remote capability or checkpoint manifest.
 */
struct PartyQuestReplicaResourcePolicy
{
    static constexpr size_t MaxFiles = 128;
    static constexpr uint64_t MaxIndividualFileBytes = 512ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxTotalFileBytes = 2ull * 1024ull * 1024ull * 1024ull;
    static constexpr uint64_t RequiredFreeSpaceMultiplier = 3;
    static constexpr uint64_t MinimumFreeSpaceReserveBytes = 256ull * 1024ull * 1024ull;
    static constexpr uint64_t MaxExecutionNanoseconds =
        5ull * 60ull * 1000ull * 1000ull * 1000ull;

    [[nodiscard]] static std::optional<uint64_t> RequiredFreeBytes(
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    [[nodiscard]] static bool HasSufficientDiskSpace(
        const PartyQuestReplicaCopyPlan& acPlan,
        uint64_t aAvailableBytes) noexcept;
};

enum class PartyQuestReplicaCopyPlanStatus : uint8_t
{
    Ready,
    InvalidLayout,
    InvalidSource,
    MissingMainSave,
    MultipleMainSaves,
    InvalidRelativePath,
    InvalidExtension,
    DuplicateSource,
    DuplicateDestination,
    SourceDestinationCollision,
    DestinationEscapesPlayerRoot,
    MissingDigest,
    InvalidWorldRevision,
    ResourceFileCountExceeded,
    ResourceFileSizeExceeded,
    ResourceTotalSizeExceeded
};

struct PartyQuestReplicaCopyPlan
{
    PartyQuestReplicaCopyPlanStatus Status{PartyQuestReplicaCopyPlanStatus::InvalidSource};
    std::vector<PartyQuestReplicaCopyOperation> Operations;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestReplicaCopyPlanStatus::Ready;
    }
};

struct PartyQuestReplicaCheckpointManifest
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    PartyQuestCheckpointKind Kind{PartyQuestCheckpointKind::PreRepair};
    uint64_t CampaignWorldRevision{};
    std::vector<PartyQuestReplicaCopyOperation> Files;

    bool operator==(const PartyQuestReplicaCheckpointManifest&) const = default;
};

/**
 * Pure planner for importing a solo save into a separate co-op replica and for
 * checkpointing that replica. No filesystem reads/writes/copies are performed.
 *
 * Every destination is constrained to the player-specific co-op tree and every
 * operation carries an expected size+digest for post-copy verification.
 */
class PartyQuestReplicaFilePlanner final
{
public:
    [[nodiscard]] static PartyQuestReplicaCopyPlan BuildImportPlan(
        const PartyQuestCoopSavePaths& acPaths,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles);

    /** Legacy kind-root checkpoint planner retained for existing test/archive tooling. */
    [[nodiscard]] static PartyQuestReplicaCopyPlan BuildCheckpointPlan(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        const std::vector<PartyQuestReplicaFileSpec>& acReplicaFiles);

    /**
     * Production-oriented immutable checkpoint planner. A non-zero campaign
     * world revision gets its own Revision_<hex> snapshot directory so a newer
     * checkpoint never overwrites an older recovery point.
     */
    [[nodiscard]] static PartyQuestReplicaCopyPlan BuildRevisionCheckpointPlan(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acReplicaFiles);

    [[nodiscard]] static std::optional<PartyQuestReplicaCheckpointManifest> BuildCheckpointManifest(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan);

    [[nodiscard]] static bool IsSafeRelativePath(const std::filesystem::path& acPath) noexcept;
    [[nodiscard]] static bool IsContainedBy(
        const std::filesystem::path& acRoot,
        const std::filesystem::path& acCandidate) noexcept;
};
