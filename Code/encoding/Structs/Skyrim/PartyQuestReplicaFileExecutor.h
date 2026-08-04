#pragma once

#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

enum class PartyQuestReplicaExecutionStatus : uint8_t
{
    Success,
    InvalidPlan,
    InvalidLayout,
    InvalidSourcePath,
    SourceMissing,
    SourceSymlink,
    SourceNotRegularFile,
    ImportSourceInsidePlayerRoot,
    CheckpointSourceOutsidePlayerRoot,
    CheckpointSourceInsideCheckpointTree,
    SourceChanged,
    InvalidDestination,
    DestinationExists,
    DestinationSymlinkEscape,
    IoError,
    VerificationFailed,
    RollbackFailed
};

struct PartyQuestReplicaFileObservation
{
    uint64_t Size{};
    uint64_t Digest{};

    bool operator==(const PartyQuestReplicaFileObservation&) const noexcept = default;
};

struct PartyQuestReplicaExecutionReport
{
    PartyQuestReplicaExecutionStatus Status{PartyQuestReplicaExecutionStatus::InvalidPlan};
    size_t CompletedOperations{};
    size_t FailedOperation{};
    std::filesystem::path FailedPath;

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Status == PartyQuestReplicaExecutionStatus::Success;
    }
};

/**
 * Verified filesystem executor for isolated co-op replica files.
 *
 * The executor never deletes or modifies a source file and never overwrites an
 * existing destination. Every source is re-read and checked against the
 * planner's expected size+digest before publication. Files are first copied to
 * temporary siblings, verified, and only then renamed into place. If a normal
 * in-process publication error occurs, newly published files are rolled back.
 *
 * Missing destination paths are a normal create-only condition on every
 * supported STL implementation. ENOENT/ENOTDIR returned by symlink_status are
 * therefore normalized as "missing", while all other metadata errors remain
 * fail-closed. Destination parent confinement is checked again before publish.
 *
 * This is intentionally not wired to Skyrim's save APIs yet. The digest is a
 * deterministic transport-integrity checksum, not a cryptographic trust proof;
 * mod compatibility/authentication uses separate fingerprint policy.
 */
class PartyQuestReplicaFileExecutor final
{
public:
    [[nodiscard]] static std::optional<PartyQuestReplicaFileObservation> ObserveRegularFile(
        const std::filesystem::path& acPath) noexcept;

    /** Builds a planner-ready file spec from the bytes that exist right now. */
    [[nodiscard]] static std::optional<PartyQuestReplicaFileSpec> InspectSource(
        PartyQuestReplicaFileKind aKind,
        const std::filesystem::path& acSourcePath,
        const std::filesystem::path& acRelativePath) noexcept;

    /**
     * Imports immutable source files into saves/sidecars. Sources must be
     * outside PlayerDirectory and all final destinations must not exist.
     */
    [[nodiscard]] static PartyQuestReplicaExecutionReport ExecuteImport(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    /** Legacy kind-root checkpoint execution retained for existing tooling. */
    [[nodiscard]] static PartyQuestReplicaExecutionReport ExecuteCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    /** Executes an immutable revision-scoped checkpoint plan. */
    [[nodiscard]] static PartyQuestReplicaExecutionReport ExecuteRevisionCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    [[nodiscard]] static PartyQuestReplicaExecutionReport VerifyImport(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    [[nodiscard]] static PartyQuestReplicaExecutionReport VerifyCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;

    [[nodiscard]] static PartyQuestReplicaExecutionReport VerifyRevisionCheckpoint(
        const PartyQuestCoopSavePaths& acPaths,
        PartyQuestCheckpointKind aKind,
        uint64_t aCampaignWorldRevision,
        const PartyQuestReplicaCopyPlan& acPlan) noexcept;
};
