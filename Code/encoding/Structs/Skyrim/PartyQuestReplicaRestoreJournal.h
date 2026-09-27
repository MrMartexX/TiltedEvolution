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
 * archives. RolledBack was introduced by v2 and is only reachable after the
 * exact pre-mutation destination state is verified.
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

enum class PartyQuestReplicaRestoreJournalArchiveDurability : uint8_t
{
    /** v1/v2 did not identify which persistence protocol created the archive. */
    AmbiguousLegacyEncoding,
    /** Archive belongs to the process-crash SaveAtomically/Load protocol. */
    ProcessCrashResilient,
    /** Archive belongs to the stable SavePowerLossDurably/LoadPowerLossDurably protocol. */
    PowerLossDurable
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
    PowerLossDurabilityUnsupported,
    DurabilityAmbiguous,
    DurabilityMismatch
};

struct PartyQuestReplicaRestoreJournalPersistenceResult
{
    PartyQuestReplicaRestoreJournalPersistenceStatus Status{
        PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData};
    std::optional<PartyQuestReplicaRestoreJournalState> State;
    PartyQuestReplicaRestoreJournalArchiveDurability ArchiveDurability{
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding};
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
 * Archive versions are also a persisted protocol-domain discriminator:
 *
 * - v1/v2: readable legacy evidence whose durability origin is ambiguous;
 * - v3: explicit process-crash-resilient SaveAtomically/Load journal;
 * - v4: explicit power-loss-durable SavePowerLossDurably/LoadPowerLossDurably journal.
 *
 * Decode accepts all four versions for inspection. The two recovery loaders are
 * stricter: Load accepts only explicit v3 process-crash journals and
 * LoadPowerLossDurably accepts only explicit v4 strong journals. v1/v2 fail
 * closed as DurabilityAmbiguous instead of being guessed into either executor.
 * A loader encountering the other explicit domain returns DurabilityMismatch
 * without promoting temporary evidence through the wrong rename protocol.
 *
 * Encode is the process-crash archive encoder used by SaveAtomically and writes
 * v3. SavePowerLossDurably writes v4 through a private strong encoder. The
 * payload/state schema is otherwise identical, so the discriminator grants no
 * additional filesystem or Skyrim mutation authority by itself.
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

private:
    [[nodiscard]] static std::vector<uint8_t> EncodeForArchiveDurability(
        const PartyQuestReplicaRestoreJournalState& acState,
        PartyQuestReplicaRestoreJournalArchiveDurability aDurability);
};
