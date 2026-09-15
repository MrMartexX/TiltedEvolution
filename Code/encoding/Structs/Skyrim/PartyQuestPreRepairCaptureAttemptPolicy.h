#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <cstddef>
#include <cstdint>

enum class PartyQuestPreRepairCaptureAttemptStatus : uint8_t
{
    Ready,
    InvalidContext,
    InvalidLayout,
    SaveDirectoryUnavailable,
    DirectoryInspectionFailed,
    DirectoryEntryLimitExceeded,
    AttemptLimitExceeded,
    RetentionConflict,
    RetentionCleanupFailed
};

struct PartyQuestPreRepairCaptureAttemptDecision
{
    PartyQuestPreRepairCaptureAttemptStatus Status{
        PartyQuestPreRepairCaptureAttemptStatus::InvalidContext};
    size_t ExistingAttemptCount{};
    size_t ReclaimedFileCount{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestPreRepairCaptureAttemptStatus::Ready;
    }
};

/**
 * Immutable local resource policy for engine-generated PreRepair save sources.
 * It accepts no remotely supplied limit. Admission bounds retries for the
 * active transaction/revision; reclamation may remove only exact historical
 * capture sources after proving that durable recovery never references this
 * scratch namespace. Current transaction/revision evidence is never removed.
 */
class PartyQuestPreRepairCaptureAttemptPolicy final
{
public:
    static constexpr size_t MaxAttemptsPerTransactionRevision = 8;
    static constexpr size_t MaxInspectedDirectoryEntries = 4096;

    [[nodiscard]] static PartyQuestPreRepairCaptureAttemptDecision Evaluate(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision) noexcept;

    /**
     * Removes exact historical capture-source files only. The complete current
     * transaction/revision namespace is protected. All candidates are confined
     * and revalidated before the first deletion; ambiguity fails closed.
     */
    [[nodiscard]] static PartyQuestPreRepairCaptureAttemptDecision
    ReclaimHistoricalAttempts(
        const PartyQuestCoopSavePaths& acPaths,
        uint64_t aProtectedTransactionId,
        uint64_t aProtectedWorldRevision) noexcept;
};
