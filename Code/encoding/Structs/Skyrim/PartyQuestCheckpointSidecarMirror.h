#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

struct PartyQuestCheckpointSidecarMirrorResult
{
    PartyQuestCheckpointSidecarMirrorStatus Status{
        PartyQuestCheckpointSidecarMirrorStatus::InvalidContext};
    uint64_t FailedCapabilityId{};
    std::filesystem::path FailedPath;
    std::vector<PartyQuestReplicaFileSpec> Files;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestCheckpointSidecarMirrorStatus::Ready;
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
