#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestRuntimeApplyPersistenceStatus : uint8_t
{
    Success,
    FileNotFound,
    IoError,
    InvalidMagic,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    InvalidData
};

struct PartyQuestRuntimeApplyPersistenceResult
{
    PartyQuestRuntimeApplyPersistenceStatus Status{PartyQuestRuntimeApplyPersistenceStatus::InvalidData};
    std::optional<PartyQuestRuntimeRecoveryState> State;
    bool UsedBackup{};
};

/**
 * Durable sidecar for client runtime-apply idempotency and crash recovery.
 *
 * It stores committed transaction fingerprints plus an optional in-progress
 * recovery marker. It does not store Skyrim save data or execute repairs.
 */
class PartyQuestRuntimeApplyPersistence final
{
public:
    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestRuntimeRecoveryState& acState);

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestRuntimeRecoveryState& acState);

    [[nodiscard]] static PartyQuestRuntimeApplyPersistenceResult Load(
        const std::filesystem::path& acPath);
};
