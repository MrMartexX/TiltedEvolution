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
    ReplayMismatch
};

struct PartyQuestPersistenceResult
{
    PartyQuestPersistenceStatus Status{PartyQuestPersistenceStatus::InvalidData};
    std::optional<PartyQuestState> State;
    bool UsedBackup{};
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
        const PartyQuestState& acState);

    /** Loads the primary archive and falls back to its .bak sibling if needed. */
    [[nodiscard]] static PartyQuestPersistenceResult Load(const std::filesystem::path& acPath);
};
