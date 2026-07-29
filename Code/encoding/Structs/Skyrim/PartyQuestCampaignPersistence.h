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
    bool UsedBackup{};
};

/**
 * Durable immutable metadata for the stable server campaign identity.
 *
 * The identity is stored beside the canonical quest-state archive. It is kept
 * separate because it is immutable while the state archive is replaced for
 * every accepted canonical transaction.
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
