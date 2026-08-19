#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestore.h>
#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestReplicaRestoreJournalPhase : uint8_t
{
    Prepared = 0,
    BackupsReady = 1,
    MutationStarted = 2,
    Restored = 3,
    Committed = 4,
    RolledBack = 5
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
    OriginalVerificationFailed,
    InvalidTransition
};

enum class PartyQuestReplicaRestoreRecoveryDisposition : uint8_t
{
    ResumeBeforeMutation,
    RollbackRequired,
    VerifyThenCommit,
    Clean,
    RolledBackClean,
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
 * Crash-safety state machine for destructive restore of an isolated co-op
 * replica. The journal is filesystem authority only. It does not authorize
 * Skyrim/Papyrus/native world mutation.
 *
 * Persisted phase numeric values 0..4 are frozen for compatibility with v1
 * archives. RolledBack is the v2 terminal rollback tombstone and is only
 * reachable after the exact pre-mutation destination state is verified.
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

    [[nodiscard]] static bool VerifyOriginalTargets(
        const PartyQuestReplicaRestoreJournalState& acState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkBackupsReady(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkMutationStarted(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkRestored(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkCommitted(
        PartyQuestReplicaRestoreJournalState& aState) noexcept;

    [[nodiscard]] static PartyQuestReplicaRestoreJournalStatus MarkRolledBack(
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
    ResourceLimitExceeded,
    PowerLossDurabilityUnsupported
};

struct PartyQuestReplicaRestoreJournalPersistenceResult
{
    PartyQuestReplicaRestoreJournalPersistenceStatus Status{
        PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData};
    std::optional<PartyQuestReplicaRestoreJournalState> State;
    bool UsedTemporary{};
};

enum class PartyQuestReplicaRestoreJournalPersistenceBoundary : uint8_t
{
    TemporaryVerified,
    PrimaryMovedToBackup,
    TemporaryPublished
};

enum class PartyQuestReplicaRestoreJournalPersistenceDirective : uint8_t
{
    Continue,
    FailClosed
};

/**
 * Ephemeral local observer for deterministic persistence fault validation.
 * It is never serialized, receives no paths or journal bytes, and grants no
 * filesystem authority. FailClosed returns an I/O failure at the exact atomic
 * publication boundary without hiding the resulting recovery evidence.
 */
struct PartyQuestReplicaRestoreJournalPersistenceHooks
{
    using Callback = PartyQuestReplicaRestoreJournalPersistenceDirective (*)(
        PartyQuestReplicaRestoreJournalPersistenceBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestReplicaRestoreJournalPersistenceDirective Invoke(
        PartyQuestReplicaRestoreJournalPersistenceBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestReplicaRestoreJournalPersistenceDirective::Continue;
    }
};

/**
 * Restore journal persistence.
 *
 * Encode writes v2. Decode remains backward-compatible with v1, whose phase
 * values 0..4 retain their original meaning. v1 cannot represent RolledBack and
 * a forged v1 archive carrying phase 5 is rejected fail-closed.
 *
 * SaveAtomically/Load retain the original process-crash recovery protocol.
 * SavePowerLossDurably uses stable staged writes plus stable same-directory
 * primary/backup publication and never performs an unproved rollback after a
 * durable namespace transition. LoadPowerLossDurably is its recovery partner:
 * it never promotes a valid .tmp with an ordinary rename and never overwrites a
 * present invalid primary. Its parent directory must already exist; the caller
 * remains responsible for proving that directory and every filesystem
 * prerequisite for a journal phase are durable before publishing that phase.
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
        const PartyQuestReplicaRestoreJournalState& acState,
        PartyQuestReplicaRestoreJournalPersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceStatus SavePowerLossDurably(
        const std::filesystem::path& acPath,
        const PartyQuestReplicaRestoreJournalState& acState,
        PartyQuestReplicaRestoreJournalPersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceResult Load(
        const std::filesystem::path& acPath);

    [[nodiscard]] static PartyQuestReplicaRestoreJournalPersistenceResult LoadPowerLossDurably(
        const std::filesystem::path& acPath);
};
