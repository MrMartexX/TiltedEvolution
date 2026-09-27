#pragma once

#include <Structs/Skyrim/PartyQuestReplicaManifest.h>
#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

/**
 * Local-only immutable proof that one exact runtime repair authorization was
 * bound to one exact PowerLossDurable PreRepair revision checkpoint.
 *
 * This record is not a network message and grants no mutation authority by
 * itself. RuntimeCheckpoint/RuntimeRecovery integration is intentionally owned
 * by a separate slice.
 */
struct PartyQuestPreRepairAuthorizationCommitFile
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path RelativePath;
    uint64_t Size{};
    uint64_t Digest{};

    bool operator==(const PartyQuestPreRepairAuthorizationCommitFile&) const = default;
};

struct PartyQuestPreRepairAuthorizationCommit
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t TransactionId{};
    uint64_t RuntimeGeneration{};
    uint64_t CaptureEpochId{};
    uint64_t TargetWorldRevision{};
    GameId QuestId{};
    uint64_t CanonicalDigest{};
    uint64_t SidecarManifestFingerprint{};
    PartyQuestApplyAction Actions{PartyQuestApplyAction::None};
    PartyQuestVerificationEnvelopeV1 ExpectedVerification;
    std::vector<PartyQuestPreRepairAuthorizationCommitFile> Files;

    bool operator==(const PartyQuestPreRepairAuthorizationCommit&) const = default;
};

enum class PartyQuestPreRepairAuthorizationCommitPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,
    ResourceLimitExceeded
};

struct PartyQuestPreRepairAuthorizationCommitLoadResult
{
    PartyQuestPreRepairAuthorizationCommitPersistenceStatus Status{
        PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData};
    std::optional<PartyQuestPreRepairAuthorizationCommit> Record;
};

enum class PartyQuestPreRepairAuthorizationCommitPublishStatus : uint8_t
{
    Published,
    AlreadyCommitted,
    Conflict,
    InvalidCommit,
    ResourceLimitExceeded,
    StableStorageUnsupported,
    StableStorageFailure,
    Interrupted
};

enum class PartyQuestPreRepairAuthorizationCommitBoundary : uint8_t
{
    DirectoryDurable,
    TemporaryDurablyWritten,
    TemporaryVerified,
    FinalPublished,
    FinalVerified
};

enum class PartyQuestPreRepairAuthorizationCommitDirective : uint8_t
{
    Continue,
    FailClosed
};

/**
 * Ephemeral fault-injection observer. It carries no filesystem or runtime
 * authority and is deliberately excluded from the persisted schema.
 */
struct PartyQuestPreRepairAuthorizationCommitHooks
{
    using Callback = PartyQuestPreRepairAuthorizationCommitDirective (*)(
        PartyQuestPreRepairAuthorizationCommitBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestPreRepairAuthorizationCommitDirective Invoke(
        PartyQuestPreRepairAuthorizationCommitBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestPreRepairAuthorizationCommitDirective::Continue;
    }
};

/**
 * Immutable write-once local authorization record store.
 *
 * Authority is only the exact final pre_repair_commit.bin. .tmp and .bak are
 * never adopted by Load. Publication writes and verifies a durable .tmp, then
 * performs a same-directory create-only durable rename and verifies the final
 * record. An exact pre-existing final is re-established as durable and reported
 * AlreadyCommitted without rewriting it. A different or corrupt final is a
 * hard Conflict.
 */
class PartyQuestPreRepairAuthorizationCommitStore final
{
public:
    [[nodiscard]] static std::filesystem::path GetCommitPath(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aTargetWorldRevision);

    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestPreRepairAuthorizationCommit& acCommit);

    [[nodiscard]] static PartyQuestPreRepairAuthorizationCommitLoadResult Decode(
        const std::vector<uint8_t>& acBytes);

    /**
     * Loads only the supplied final path. Sibling .tmp/.bak files are never
     * inspected or adopted as authorization authority.
     */
    [[nodiscard]] static PartyQuestPreRepairAuthorizationCommitLoadResult Load(
        const std::filesystem::path& acFinalPath);

    [[nodiscard]] static PartyQuestPreRepairAuthorizationCommitPublishStatus PublishDurably(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestPreRepairAuthorizationCommit& acCommit,
        PartyQuestPreRepairAuthorizationCommitHooks aHooks = {}) noexcept;
};
