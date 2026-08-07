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
    AttemptLimitExceeded
};

struct PartyQuestPreRepairCaptureAttemptDecision
{
    PartyQuestPreRepairCaptureAttemptStatus Status{
        PartyQuestPreRepairCaptureAttemptStatus::InvalidContext};
    size_t ExistingAttemptCount{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PartyQuestPreRepairCaptureAttemptStatus::Ready;
    }
};

/**
 * Immutable local admission bound for engine-generated PreRepair save sources.
 * It never deletes evidence and accepts no remotely supplied limit. Durable
 * reference-aware reclamation is a separate policy; this gate only prevents an
 * active transaction/revision from generating an unbounded retry set.
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
};
