#pragma once

#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>

#include <cstdint>
#include <filesystem>
#include <optional>

enum class PartyQuestRuntimeRestoreAttemptStatus : uint8_t
{
    Success,
    Created,
    RecoveredInitialization,
    AlreadyAdvanced,
    UnsupportedPlatform,
    InvalidIdentity,
    InvalidLayout,
    InvalidTransactionId,
    InvalidOrdinal,
    AttemptLimitReached,
    RestoreIdAllocationLimitReached,
    WorkspaceCapabilityRequired,
    FileNotFound,
    InvalidData,
    IoError,
    PersistenceFailed,
    JournalMismatch,
    StaleJournal
};

struct PartyQuestRuntimeRestoreAttemptState
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t TransactionId{};
    uint32_t CurrentOrdinal{};
    uint64_t CurrentRestoreId{};
    uint64_t LastRolledBackRestoreId{};

    bool operator==(const PartyQuestRuntimeRestoreAttemptState&) const noexcept = default;
};

struct PartyQuestRuntimeRestoreAttemptResult
{
    PartyQuestRuntimeRestoreAttemptStatus Status{
        PartyQuestRuntimeRestoreAttemptStatus::InvalidData};
    std::optional<PartyQuestRuntimeRestoreAttemptState> State;
    std::filesystem::path StatePath;
    std::filesystem::path AttemptDirectory;
    std::filesystem::path JournalPath;
    uint64_t RestoreId{};

    [[nodiscard]] bool IsUsable() const noexcept
    {
        return Status == PartyQuestRuntimeRestoreAttemptStatus::Success ||
            Status == PartyQuestRuntimeRestoreAttemptStatus::Created ||
            Status == PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization ||
            Status == PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced;
    }
};

/**
 * Local, restart-stable identity for one filesystem restore attempt belonging to
 * one server/runtime transaction.
 *
 * Runtime TransactionId is never hashed, truncated or replaced. Logical attempt
 * identity is the exact structural pair (TransactionId, ordinal). Each logical
 * attempt is mapped durably to one monotonically allocated local RestoreId. The
 * allocator skips every pre-existing restore transaction directory before it
 * reserves an id, so legacy and strong tombstones remain occupied and no hash,
 * reserved-bit or probabilistic collision assumption is introduced.
 *
 * Physical restore journals deliberately retain the existing reviewed layout:
 *
 *   metadata/restore/Transaction_<RestoreId>/journal.bin
 *
 * This lets the current restore journal, preparation and durable executor keep
 * their exact path-confinement checks unchanged. The per-transaction mapping is:
 *
 *   metadata/runtime_restore_attempts/RuntimeTransaction_<TransactionId>.bin
 *
 * The attempt-state file and allocator are local power-loss durability
 * primitives. Linux uses durable directory creation, staged durable write and
 * same-directory atomic publication. Windows fails closed because the reviewed
 * durable directory-tree contract required by the strong restore path is still
 * unavailable there.
 *
 * Allocation is leak-safe: the global next-id barrier is durably advanced before
 * the transaction mapping is published. A crash in between can waste an id but
 * cannot reuse it or create two authoritative journals for one attempt.
 *
 * Advancing an attempt requires the exact persisted PowerLossDurable terminal
 * RolledBack journal named by CurrentRestoreId plus an exact live workspace
 * capability. Repeating the same advance after a crash is idempotent: ordinal N
 * can advance to N+1 once, and replaying that same terminal journal returns
 * AlreadyAdvanced rather than allocating N+2.
 */
class PartyQuestRuntimeRestoreAttemptStore final
{
public:
    static constexpr uint32_t MaxAttemptsPerTransaction = 64;
    static constexpr uint32_t MaxRestoreIdAllocationProbe = 65536;

    [[nodiscard]] static PartyQuestRuntimeRestoreAttemptResult Load(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] static PartyQuestRuntimeRestoreAttemptResult EnsureInitializedAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        uint64_t aTransactionId,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept;

    [[nodiscard]] static PartyQuestRuntimeRestoreAttemptResult AdvanceAfterRolledBackAuthorized(
        const PartyQuestCoopSavePaths& acPaths,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        uint64_t aTransactionId,
        uint32_t aRolledBackOrdinal,
        const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept;

    [[nodiscard]] static std::filesystem::path GetStatePath(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] static std::filesystem::path GetRestoreDirectory(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aRestoreId) noexcept;

    [[nodiscard]] static std::filesystem::path GetJournalPath(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aRestoreId) noexcept;
};
