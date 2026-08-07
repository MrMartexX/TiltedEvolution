#pragma once

#include <Structs/Skyrim/PartyQuestCheckpointCaptureEpoch.h>
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
 *
 * CaptureEpochId is mandatory for the production epoch-bound collector. A zero
 * value is accepted only by the legacy diagnostic overload, which cannot cross
 * the runtime PreRepair publication gate.
 *
 * For production use, Authorization must also carry an explicit coherent
 * provider contract: AtomicSnapshot or FrozenUntilEpochRelease.
 */
struct PartyQuestCheckpointSidecarCapture
{
    PartyQuestCheckpointSidecarAuthorization Authorization;
    uint64_t CaptureEpochId{};
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    std::vector<std::filesystem::path> MirrorRelativeFiles;
};

enum class PartyQuestCheckpointSidecarMirrorStatus : uint8_t
{
    Ready,
    InvalidContext,
    InvalidLayout,
    CaptureEpochMismatch,
    CaptureConsistencyUnavailable,
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
    SourceInspectionFailed,
    CapabilityLimitExceeded,
    FileLimitExceeded,
    PathLengthExceeded
};

/**
 * Encapsulation-backed proof that one exact sidecar manifest was fully
 * satisfied for one repair transaction/revision and one exact mirror file set.
 * Production authorization additionally binds the active capture epoch.
 */
class PartyQuestCheckpointSidecarMirrorAuthorization final
{
public:
    PartyQuestCheckpointSidecarMirrorAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept { return m_verified; }
    [[nodiscard]] uint64_t GetCaptureEpochId() const noexcept { return m_captureEpochId; }

    /** Legacy diagnostic match; does not prove temporal coherence. */
    [[nodiscard]] bool Matches(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) const noexcept;

    /** Production gate: exact manifest + active epoch + exact mirror files. */
    [[nodiscard]] bool Matches(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) const noexcept;

private:
    PartyQuestCheckpointSidecarMirrorAuthorization(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept;

    PartyQuestCheckpointSidecarMirrorAuthorization(
        const PartyQuestCheckpointSidecarManifest& acManifest,
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept;

    [[nodiscard]] static uint64_t ComputeManifestFingerprint(
        const PartyQuestCheckpointSidecarManifest& acManifest) noexcept;
    [[nodiscard]] static uint64_t ComputeFilesFingerprint(
        const std::vector<PartyQuestReplicaFileSpec>& acFiles) noexcept;

    uint64_t m_captureEpochId{};
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
 * The epoch-bound overload is the only form accepted by the runtime PreRepair
 * assembler. In addition to epoch identity it requires each present provider to
 * carry an explicit coherent-capture contract. The legacy tx/revision overload
 * remains for isolated diagnostics and produces an epochless authorization that
 * the assembler rejects.
 */
class PartyQuestCheckpointSidecarMirrorCollector final
{
public:
    [[nodiscard]] static std::string FormatCapabilityDirectory(
        uint64_t aCapabilityId);

    /** Legacy diagnostic collection; cannot satisfy production assembler. */
    [[nodiscard]] static PartyQuestCheckpointSidecarMirrorResult Collect(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCheckpointSidecarManifest& acManifest,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        const std::vector<PartyQuestCheckpointSidecarCapture>& acCaptures) noexcept;

    /** Production collection bound to one logical capture epoch. */
    [[nodiscard]] static PartyQuestCheckpointSidecarMirrorResult Collect(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCheckpointSidecarManifest& acManifest,
        const PartyQuestCheckpointCaptureEpoch& acEpoch,
        const std::vector<PartyQuestCheckpointSidecarCapture>& acCaptures) noexcept;
};
