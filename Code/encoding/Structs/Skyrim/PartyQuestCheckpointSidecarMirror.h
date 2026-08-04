#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class PartyQuestCheckpointSidecarMirrorCollector;

/**
 * Receipt produced after a verified local provider has captured its live state
 * into the player replica mirror. Paths are relative to sidecars/external and
 * are never supplied by the campaign/server requirement.
 */
struct PartyQuestCheckpointSidecarCapture
{
    PartyQuestCheckpointSidecarAuthorization Authorization;
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    std::vector<std::filesystem::path> MirrorRelativeFiles;
};

enum class PartyQuestCheckpointSidecarMirrorStatus : uint8_t
{
    Ready,
    InvalidContext,
    InvalidLayout,
    UnexpectedCapability,
    DuplicateCapabilityCapture,
    MissingRequiredCapture,
    InvalidAuthorization,
    AuthorizationMismatch,
    TransactionMismatch,
    WorldRevisionMismatch,
    EmptyCapture,
    InvalidRelativePath,
    CapabilityPathMismatch,
    MirrorRootUnavailable,
    MirrorEscape,
    DuplicateFile,
    SourceInspectionFailed
};

/**
 * Unforgeable proof that one exact sidecar manifest was fully satisfied for one
 * repair transaction/revision and one exact mirror file set.
 */
class PartyQuestCheckpointSidecarMirrorAuthorization final
{
public:
    PartyQuestCheckpointSidecarMirrorAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }

    [[nodiscard]] bool Matches(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) const noexcept;

private:
    PartyQuestCheckpointSidecarMirrorAuthorization(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept;

    [[nodiscard]] static uint64_t ComputeManifestFingerprint(
        const PartyQuestCheckpointSidecarManifest& acManifest) noexcept;
    [[nodiscard]] static uint64_t ComputeFilesFingerprint(
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept;

    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    uint64_t m_manifestFingerprint{};
    uint64_t m_filesFingerprint{};
    size_t m_fileCount{};
    bool m_verified{};

    friend class PartyQuestCheckpointSidecarMirrorCollector;
};

struct PartyQuestCheckpointSidecarMirrorResult
{
    PartyQuestCheckpointSidecarMirrorStatus Status{
        PartyQuestCheckpointSidecarMirrorStatus::InvalidContext};
    uint64_t FailedCapabilityId{};
    std::filesystem::path FailedPath;
    std::vector<PartyQuestReplicaFileSpec> Files;
    PartyQuestCheckpointSidecarMirrorAuthorization Authorization;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestCheckpointSidecarMirrorStatus::Ready &&
            Authorization.IsVerified();
    }
};

/**
 * Verifies transaction-scoped provider captures already materialized inside the
 * co-op replica. This layer never reads arbitrary external plugin paths.
 *
 * Live external I/O belongs to a verified native adapter/provider:
 *
 *   live state -> provider capture -> replica sidecars/external mirror
 *              -> this collector -> immutable checkpoint
 *
 * Recovery performs the inverse after the generic checkpoint restore has put
 * bytes back into the mirror.
 */
class PartyQuestCheckpointSidecarMirrorCollector final
{
public:
    [[nodiscard]] static std::string FormatCapabilityDirectory(
        uint64_t aCapabilityId);

    [[nodiscard]] static PartyQuestCheckpointSidecarMirrorResult Collect(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestCheckpointSidecarCapture>& acCaptures) noexcept;
};
