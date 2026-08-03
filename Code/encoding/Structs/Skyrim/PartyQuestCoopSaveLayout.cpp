#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <array>
#include <cstdio>

namespace
{
std::string FormatId(uint64_t aHigh, uint64_t aLow)
{
    std::array<char, 33> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "%016llX%016llX",
        static_cast<unsigned long long>(aHigh),
        static_cast<unsigned long long>(aLow));
    return buffer.data();
}
} // namespace

std::string PartyQuestCoopSaveLayout::FormatCampaignId(
    const PartyQuestCampaignId& acCampaignId)
{
    return acCampaignId.IsValid()
        ? FormatId(acCampaignId.High, acCampaignId.Low)
        : std::string{};
}

std::string PartyQuestCoopSaveLayout::FormatPlayerProfileId(
    const PartyQuestPlayerProfileId& acProfileId)
{
    return acProfileId.IsValid()
        ? FormatId(acProfileId.High, acProfileId.Low)
        : std::string{};
}

std::string PartyQuestCoopSaveLayout::FormatWorldRevision(uint64_t aWorldRevision)
{
    std::array<char, 26> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "Revision_%016llX",
        static_cast<unsigned long long>(aWorldRevision));
    return buffer.data();
}

std::optional<PartyQuestCoopSavePaths> PartyQuestCoopSaveLayout::Build(
    const std::filesystem::path& acRoot,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acProfileId)
{
    if (acRoot.empty() || !acCampaignId.IsValid() || !acProfileId.IsValid())
        return std::nullopt;

    const std::string campaignId = FormatCampaignId(acCampaignId);
    const std::string profileId = FormatPlayerProfileId(acProfileId);
    if (campaignId.empty() || profileId.empty())
        return std::nullopt;

    PartyQuestCoopSavePaths paths;
    paths.Root = acRoot.lexically_normal();
    paths.CampaignDirectory = paths.Root / ("Campaign_" + campaignId);
    paths.PlayerDirectory = paths.CampaignDirectory / ("Player_" + profileId);
    paths.CheckpointsDirectory = paths.PlayerDirectory / "checkpoints";
    paths.SavesDirectory = paths.PlayerDirectory / "saves";
    paths.SidecarsDirectory = paths.PlayerDirectory / "sidecars";
    paths.MetadataDirectory = paths.PlayerDirectory / "metadata";
    paths.RuntimeApplySidecar = paths.SidecarsDirectory / "party_quest_runtime_apply.bin";
    return paths;
}

const char* PartyQuestCoopSaveLayout::GetCheckpointName(
    PartyQuestCheckpointKind aKind) noexcept
{
    switch (aKind)
    {
    case PartyQuestCheckpointKind::PreJoin: return "PreJoin";
    case PartyQuestCheckpointKind::PreMigration: return "PreMigration";
    case PartyQuestCheckpointKind::PreRepair: return "PreRepair";
    case PartyQuestCheckpointKind::SessionStart: return "SessionStart";
    case PartyQuestCheckpointKind::LastKnownGood: return "LastKnownGood";
    }

    return "Unknown";
}

std::filesystem::path PartyQuestCoopSaveLayout::GetCheckpointDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind)
{
    return acPaths.CheckpointsDirectory / GetCheckpointName(aKind);
}

std::filesystem::path PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aWorldRevision)
{
    return GetCheckpointDirectory(acPaths, aKind) / FormatWorldRevision(aWorldRevision);
}
