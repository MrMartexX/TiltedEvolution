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
    InvalidData,
    PowerLossDurabilityUnsupported
};

struct PartyQuestCampaignPersistenceResult
{
    PartyQuestCampaignPersistenceStatus Status{PartyQuestCampaignPersistenceStatus::InvalidData};
    std::optional<PartyQuestCampaignId> CampaignId;
    bool CanonicalArchiveRequired{};
    bool UsedBackup{};
    bool UsedTemporary{};
    bool BackupRefreshRequired{};
};

enum class PartyQuestCampaignPersistenceBoundary : uint8_t
{
    TemporaryVerified,
    PrimaryMovedToBackup,
    PrimaryPublished,
    BackupTemporaryVerified,
    BackupPublished
};

enum class PartyQuestCampaignPersistenceDirective : uint8_t
{
    Continue,
    FailClosed
};

/** Local-only fault observer; it carries no paths, bytes or storage authority. */
struct PartyQuestCampaignPersistenceHooks
{
    using Callback = PartyQuestCampaignPersistenceDirective (*)(
        PartyQuestCampaignPersistenceBoundary,
        void*) noexcept;

    Callback OnBoundary{};
    void* Context{};

    [[nodiscard]] PartyQuestCampaignPersistenceDirective Invoke(
        PartyQuestCampaignPersistenceBoundary aBoundary) const noexcept
    {
        return OnBoundary
            ? OnBoundary(aBoundary, Context)
            : PartyQuestCampaignPersistenceDirective::Continue;
    }
};

/**
 * Persistent immutable metadata for the stable server campaign identity.
 *
 * The immutable bootstrap copy is stored beside the canonical quest-state
 * archive. Current state archives also embed the same identity. Metadata v2 is
 * published only after a matching canonical archive exists and records that
 * the archive is required on every later successful bootstrap.
 *
 * SavePowerLossDurably preserves the campaign-specific invariant that primary
 * and backup are both refreshed to the exact current v2 identity archive. Its
 * parent directory must already exist and satisfy the caller's surrounding
 * namespace-durability contract.
 */
class PartyQuestCampaignPersistence final
{
public:
    [[nodiscard]] static PartyQuestCampaignId GenerateCampaignId() noexcept;

    [[nodiscard]] static std::vector<uint8_t> Encode(const PartyQuestCampaignId& acCampaignId);
    [[nodiscard]] static PartyQuestCampaignPersistenceResult Decode(const std::vector<uint8_t>& acBytes);

    [[nodiscard]] static PartyQuestCampaignPersistenceStatus SaveAtomically(
        const std::filesystem::path& acPath,
        const PartyQuestCampaignId& acCampaignId,
        PartyQuestCampaignPersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestCampaignPersistenceStatus SavePowerLossDurably(
        const std::filesystem::path& acPath,
        const PartyQuestCampaignId& acCampaignId,
        PartyQuestCampaignPersistenceHooks aHooks = {});

    [[nodiscard]] static PartyQuestCampaignPersistenceResult Load(const std::filesystem::path& acPath);
};
