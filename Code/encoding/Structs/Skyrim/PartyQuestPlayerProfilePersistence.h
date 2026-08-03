#pragma once

#include <Structs/Skyrim/PartyQuestPlayerProfile.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestPlayerProfilePersistenceStatus : uint8_t
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

struct PartyQuestPlayerProfilePersistenceResult
{
    PartyQuestPlayerProfilePersistenceStatus Status{PartyQuestPlayerProfilePersistenceStatus::InvalidData};
    std::optional<PartyQuestPlayerProfileId> ProfileId;
    bool UsedBackup{};
};

/**
 * Persists the stable identity of one local co-op character/profile.
 *
 * The profile id is immutable metadata. Unlike the runtime side-effect journal,
 * falling back to its previous identical identity backup is safe.
 */
class PartyQuestPlayerProfilePersistence final
{
public:
    [[nodiscard]] static PartyQuestPlayerProfileId GenerateProfileId() noexcept;

    [[nodiscard]] static std::vector<uint8_t> Encode(
        const PartyQuestPlayerProfileId& acProfileId);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceResult Decode(
        const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestPlayerProfileId& acProfileId);

    [[nodiscard]] static PartyQuestPlayerProfilePersistenceResult Load(
        const std::filesystem::path& acPath);
};
