#pragma once

#include <Structs/Skyrim/PartyQuestState.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData,
    ReplayMismatch,
    BackupRecoveryRequired
};

struct PartyQuestPersistenceResult
{
    PartyQuestPersistenceStatus Status{PartyQuestPersistenceStatus::InvalidData};
    std::optional<PartyQuestState> State;
    bool UsedBackup{};
    bool UsedTemporary{};
};

enum class PartyQuestStatePersistenceBoundary : uint8_t
{
    TemporaryVerified,
    PrimaryMovedToBackup,
    TemporaryPublished
};

enum class PartyQuestStatePersistenceDirective : uint8_t
{
    Continue,
    FailClosed
};

/** Local-only fault observer; it carries no paths, bytes or storage authority. */
struct PartyQuestStatePersistenceHooks
{
    using Callback = PartyQuestStatePersistenceDirective (*)(
        PartyQuestStatePersistenceBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestStatePersistenceDirective Invoke(
        PartyQuestStatePersistenceBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestStatePersistenceDirective::Continue;
    }
};

/**
 * Durable, self-validating storage for the canonical party quest state.
 *
 * Format version 1 stores a canonical checkpoint together with the complete
 * accepted transaction journal. Loading replays the journal and verifies that
 * it exactly reproduces the checkpoint before exposing the restored state.
 * A later format can compact the historical prefix without changing the
 * PartyQuestState transaction contract.
 */
class PartyQuestStatePersistence final
{
public:
    [[nodiscard]] static std::vector<uint8_t> Encode(const PartyQuestState& acState);
    [[nodiscard]] static PartyQuestPersistenceResult Decode(const std::vector<uint8_t>& acBytes);

    /** Writes through a sibling .tmp file and retains the previous file as .bak. */
    [[nodiscard]] static PartyQuestPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestState& acState,
        PartyQuestStatePersistenceHooks aHooks = {});

    /** A stale backup is exposed only as uncertain recovery, never canonical truth. */
    [[nodiscard]] static PartyQuestPersistenceResult Load(const std::filesystem::path& acPath);
};
