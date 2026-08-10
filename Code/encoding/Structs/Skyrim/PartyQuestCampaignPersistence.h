#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

enum class PartyQuestCampaignPersistenceStatus : uint8_t
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

struct PartyQuestCampaignPersistenceResult
{
    PartyQuestCampaignPersistenceStatus Status{PartyQuestCampaignPersistenceStatus::InvalidData};
    std::optional<PartyQuestCampaignId> CampaignId;
    bool CanonicalArchiveRequired{};
    bool UsedBackup{};
    bool BackupRefreshRequired{};
};

/**
 * Persistent immutable metadata for the stable server campaign identity.
 *
 * The immutable bootstrap copy is stored beside the canonical quest-state
 * archive. Current state archives also embed the same identity. Metadata v2 is
 * published only after a matching canonical archive exists and records that
 * the archive is required on every later successful bootstrap.
 */
class PartyQuestCampaignPersistence final
{
public:
    [[nodiscard]] static PartyQuestCampaignId GenerateCampaignId() noexcept;

    [[nodiscard]] static std::vector<uint8_t> Encode(const PartyQuestCampaignId& acCampaignId);
    [[nodiscard]] static PartyQuestCampaignPersistenceResult Decode(const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestCampaignPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestCampaignId& acCampaignId);

    [[nodiscard]] static PartyQuestCampaignPersistenceResult Load(const std::filesystem::path& acPath);
};
