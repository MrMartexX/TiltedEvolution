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
 * Runtime TransactionId is never hashed, truncated or replaced. Attempt identity
 * is the exact structural pair (TransactionId, ordinal). The filesystem layout
 * therefore remains collision-free without assuming unused TransactionId bits:
 *
 *   metadata/runtime_restore_attempts/RuntimeTransaction_<TransactionId>.bin
 *   metadata/restore/RuntimeTransaction_<TransactionId>/Attempt_<ordinal>/journal.bin
 *
 * RestoreId inside the generic restore journal is ordinal + 1. It is only unique
 * inside the transaction namespace and is never treated as the server transaction
 * identity.
 *
 * The attempt-state file is a local power-loss durability primitive. Linux uses
 * durable directory creation, durable staged write and same-directory atomic
 * publication. Windows fails closed because the reviewed durable directory-tree
 * contract required by the strong restore path is still unavailable there.
 *
 * Advancing an attempt requires the exact persisted PowerLossDurable terminal
 * RolledBack journal for the named ordinal plus an exact live workspace
 * capability. Repeating the same advance after a crash is idempotent: ordinal N
 * can advance to N+1 once, and replaying the same RolledBack evidence returns
 * AlreadyAdvanced rather than N+2.
 */
class PartyQuestRuntimeRestoreAttemptStore final
{
public:
    static constexpr uint32_t MaxAttemptsPerTransaction = 64;

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

    [[nodiscard]] static std::filesystem::path GetAttemptDirectory(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aTransactionId,
        uint32_t aOrdinal) noexcept;

    [[nodiscard]] static std::filesystem::path GetJournalPath(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aTransactionId,
        uint32_t aOrdinal) noexcept;

    [[nodiscard]] static constexpr uint64_t GetRestoreId(uint32_t aOrdinal) noexcept
    {
        return static_cast<uint64_t>(aOrdinal) + 1;
    }
};
