#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestore.h>
#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestReplicaRestoreJournalPhase : uint8_t
{
    Prepared,
    BackupsReady,
    MutationStarted,
    Restored,
    Committed
};

enum class PartyQuestReplicaRestoreJournalStatus : uint8_t
{
    Ready,
    InvalidPlan,
    InvalidIdentity,
    InvalidRestoreId,
    InvalidWorldRevision,
    InvalidPath,
    DestinationUnsafe,
    BackupVerificationFailed,
    RestoredVerificationFailed,
    InvalidTransition
};

enum class PartyQuestReplicaRestoreRecoveryDisposition : uint8_t
{
    ResumeBeforeMutation,
    RollbackRequired,
    VerifyThenCommit,
    Clean,
    InvalidState
};

struct PartyQuestReplicaRestoreJournalOperation
{
    PartyQuestReplicaFileKind Kind{PartyQuestReplicaFileKind::ExternalSidecar};
    std::filesystem::path CheckpointSourcePath;
    std::filesystem::path ReplicaDestinationPath;
    std::filesystem::path RollbackPath;
    uint64_t ExpectedRestoredSize{};
    uint64_t ExpectedRestoredDigest{};
    bool DestinationExisted{};
    uint64_t OriginalSize{};
    uint64_t OriginalDigest{};

    bool operator==(const PartyQuestReplicaRestoreJournalOperation&) const = default;
};

struct PartyQuestReplicaRestoreJournalState
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t RestoreId{};
    PartyQuestCheckpointKind CheckpointKind{PartyQuestCheckpointKind::PreRepair};
    uint64_t CampaignWorldRevision{};
    PartyQuestReplicaRestoreJournalPhase Phase{PartyQuestReplicaRestoreJournalPhase::Prepared};
    std::filesystem::path TransactionDirectory;
    std::vector<PartyQuestReplicaRestoreJournalOperation> Operations;

    bool operator==(const PartyQuestReplicaRestoreJournalState&) const = default;
};

struct PartyQuestReplicaRestoreJournalResult
{
    PartyQuestReplicaRestoreJournalStatus Status{PartyQuestReplicaRestoreJournalStatus::InvalidPlan};
    std::optional<PartyQuestReplicaRestoreJournalState> State;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestReplicaRestoreJournalStatus::Ready && State.has_value();
    }
};

/**
 * Crash-safety state machine for a future destructive restore of an isolated
 * co-op replica. This type deliberately does not replace or delete live save
 * files. It records/verifies the prerequisites that an executor must durably
 * persist before crossing the mutation barrier.
 */
class PartyQuestReplicaRestoreJournal final
{
public:
    [[nodiscard]] static PartyQuestReplicaRestoreJournalResult Prepare(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestReplicaRestorePlan& acPlan,
        uint64_t aRestoreId) noexcept;

    [[nodiscard]] static std::filesystem::path GetJournalPath(
        const PartyQuestReplicaRestoreJournalState& acState);

    [[nodiscard]] static bool VerifyRollbackBackups(
        const PartyQuestReplicaRestoreJournalState& acState) noexcept;

    [[nodiscard]] static bool VerifyRestoredTargets(
        const PartyQuestReplicaRestoreJournalState& acState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkBackupsReady(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkMutationStarted(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkRestored(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkCommitted(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreRecoveryDisposition GetRecoveryDisposition(
        const PartyQuestReplicaRestoreJournalState& acState) noexcept;
};

enum class PartyQuestReplicaRestoreJournalPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,
    BackupRecoveryRequired,
    ResourceLimitExceeded
};

struct PartyQuestReplicaRestoreJournalPersistenceResult
{
    PartyQuestReplicaRestoreJournalPersistenceStatus Status{
        PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData};
    std::optional<PartyQuestReplicaRestoreJournalState> State;
    bool UsedTemporary{};
};

/**
 * Durable restore journal. A valid .tmp may be promoted because it represents
 * a completely written newer state. A stale .bak is never silently accepted:
 * forgetting that the mutation barrier was crossed could make crash recovery
 * overwrite the wrong side of the transaction.
 */
class PartyQuestReplicaRestoreJournalPersistence final
{
public:
    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestReplicaRestoreJournalState& acState);

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestReplicaRestoreJournalState& acState);

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceResult Load(
        const std::filesystem::path& acPath);
};
