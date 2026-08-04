#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/PartyQuestPlayerProfile.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

enum class PartyQuestCheckpointKind : uint8_t
{
    PreJoin,
    PreMigration,
    PreRepair,
    SessionStart,
    LastKnownGood
};

struct PartyQuestCoopSavePaths
{
    std::filesystem::path Root;
    std::filesystem::path CampaignDirectory;
    std::filesystem::path PlayerDirectory;
    std::filesystem::path CheckpointsDirectory;
    std::filesystem::path SavesDirectory;
    std::filesystem::path SidecarsDirectory;
    std::filesystem::path MetadataDirectory;
    std::filesystem::path RuntimeApplySidecar;

    bool operator==(const PartyQuestCoopSavePaths&) const = default;
};

/**
 * Pure path planner for isolated co-op save replicas.
 *
 * It performs no filesystem writes. Solo saves are intentionally outside this
 * tree, so later checkpoint/copy code can enforce the invariant that original
 * single-player saves are never overwritten by campaign repair.
 */
class PartyQuestCoopSaveLayout final
{
public:
    [[nodiscard]] static std::string FormatCampaignId(
        const PartyQuestCampaignId& acCampaignId);

    [[nodiscard]] static std::string FormatPlayerProfileId(
        const PartyQuestPlayerProfileId& acProfileId);

    /** Fixed-width checkpoint revision directory component. */
    [[nodiscard]] static std::string FormatWorldRevision(uint64_t aWorldRevision);

    [[nodiscard]] static std::optional<PartyQuestCoopSavePaths> Build(
        const std::filesystem::path& acRoot,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acProfileId);

    /**
     * Verifies that a supplied path bundle is exactly the deterministic layout
     * for the requested campaign/player under its declared Root. This is a
     * structural identity check only; it performs no filesystem I/O or symlink
     * resolution. Filesystem executors must still enforce resolved confinement.
     */
    [[nodiscard]] static bool Matches(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acProfileId) noexcept;

    /** Kind root, e.g. checkpoints/PreRepair. */
    [[nodiscard]] static std::filesystem::path GetCheckpointDirectory(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind);

    /**
     * Immutable revision snapshot root, e.g.
     * checkpoints/PreRepair/Revision_000000000000019A.
     */
    [[nodiscard]] static std::filesystem::path GetCheckpointRevisionDirectory(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        uint64_t aWorldRevision);

    [[nodiscard]] static const char* GetCheckpointName(
        PartyQuestCheckpointKind aKind) noexcept;
};
