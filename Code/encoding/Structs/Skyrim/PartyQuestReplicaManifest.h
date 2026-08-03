#pragma once

#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestReplicaSnapshotType : uint8_t
{
    ImportedReplica,
    Checkpoint,
    RevisionCheckpoint
};

struct PartyQuestReplicaPublishedFile
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path RelativePath;
    uint64_t Size{};
    uint64_t Digest{};

    bool operator==(const PartyQuestReplicaPublishedFile&) const = default;
};

struct PartyQuestReplicaManifest
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    PartyQuestReplicaSnapshotType SnapshotType{PartyQuestReplicaSnapshotType::ImportedReplica};
    PartyQuestCheckpointKind CheckpointKind{PartyQuestCheckpointKind::PreJoin};
    uint64_t CampaignWorldRevision{};
    std::vector<PartyQuestReplicaPublishedFile> Files;

    bool operator==(const PartyQuestReplicaManifest&) const = default;
};

enum class PartyQuestReplicaManifestPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,
    BackupRecoveryRequired
};

struct PartyQuestReplicaManifestPersistenceResult
{
    PartyQuestReplicaManifestPersistenceStatus Status{PartyQuestReplicaManifestPersistenceStatus::InvalidData};
    std::optional<PartyQuestReplicaManifest> Manifest;
    bool UsedTemporary{};
    bool UsedBackup{};
};

enum class PartyQuestReplicaManifestVerificationStatus : uint8_t
{
    Verified,
    InvalidIdentity,
    InvalidManifest,
    PathEscape,
    MissingOrChangedFile
};

/**
 * Durable completion marker for an imported co-op replica or checkpoint.
 *
 * Copy execution alone is not considered durable evidence that a multi-file
 * snapshot completed: a process can die between per-file renames. The manifest
 * is written only after all final files verify, and future use must verify the
 * manifest again. A valid .tmp is preferred after an interrupted manifest
 * replacement; an older .bak is never silently treated as current truth.
 */
class PartyQuestReplicaManifestStore final
{
public:
    [[nodiscard]] static std::optional<PartyQuestReplicaManifest> BuildImportManifest(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan);

    /** Legacy kind-root checkpoint manifest retained for existing tooling. */
    [[nodiscard]] static std::optional<PartyQuestReplicaManifest> BuildCheckpointManifest(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan);

    /** Immutable revision-scoped checkpoint completion manifest. */
    [[nodiscard]] static std::optional<PartyQuestReplicaManifest> BuildRevisionCheckpointManifest(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan);

    [[nodiscard]] static std::filesystem::path GetImportManifestPath(
        const PartyQuestCoopSavePaths& acPaths);

    [[nodiscard]] static std::filesystem::path GetCheckpointManifestPath(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind);

    [[nodiscard]] static std::filesystem::path GetRevisionCheckpointManifestPath(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision);

    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestReplicaManifest& acManifest);

    [[nodiscard]] static PartyQuestReplicaManifestPersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestReplicaManifestPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestReplicaManifest& acManifest);

    [[nodiscard]] static PartyQuestReplicaManifestPersistenceResult Load(
        const std::filesystem::path& acPath);

    [[nodiscard]] static PartyQuestReplicaManifestVerificationStatus VerifyPublishedFiles(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId,
        const PartyQuestReplicaManifest& acManifest) noexcept;
};
