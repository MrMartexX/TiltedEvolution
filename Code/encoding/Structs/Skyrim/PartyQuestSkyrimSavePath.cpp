#include <Structs/Skyrim/PartyQuestSkyrimSavePath.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
constexpr std::string_view kRoot = "CoopCampaigns";
constexpr char kSeparator = '\\';

bool StartsWith(
    std::string_view acValue,
    std::string_view acPrefix) noexcept
{
    return acValue.size() >= acPrefix.size() &&
        acValue.substr(0, acPrefix.size()) == acPrefix;
}

bool IsAsciiHex(char aCharacter) noexcept
{
    const unsigned char value = static_cast<unsigned char>(aCharacter);
    return std::isdigit(value) != 0 ||
        (aCharacter >= 'A' && aCharacter <= 'F');
}

bool IsExpectedIdComponent(
    std::string_view acComponent,
    std::string_view acPrefix) noexcept
{
    if (acComponent.size() != acPrefix.size() + 32 ||
        !StartsWith(acComponent, acPrefix))
    {
        return false;
    }

    const std::string_view id = acComponent.substr(acPrefix.size());
    return std::all_of(id.begin(), id.end(), IsAsciiHex);
}

std::vector<std::string_view> SplitComponents(std::string_view acPath)
{
    std::vector<std::string_view> components;
    size_t offset{};
    while (offset < acPath.size())
    {
        const size_t separator = acPath.find(kSeparator, offset);
        const size_t end = separator == std::string_view::npos
            ? acPath.size()
            : separator;
        components.emplace_back(acPath.substr(offset, end - offset));
        if (separator == std::string_view::npos)
            break;
        offset = separator + 1;
    }
    return components;
}
} // namespace

std::string PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId)
{
    if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
        return {};

    const std::string campaignId =
        PartyQuestCoopSaveLayout::FormatCampaignId(acCampaignId);
    const std::string playerId =
        PartyQuestCoopSaveLayout::FormatPlayerProfileId(acPlayerProfileId);
    if (campaignId.empty() || playerId.empty())
        return {};

    std::string path;
    path.reserve(14 + 1 + 9 + campaignId.size() + 1 + 7 + playerId.size() + 7);
    path.append(kRoot);
    path.push_back(kSeparator);
    path.append("Campaign_");
    path.append(campaignId);
    path.push_back(kSeparator);
    path.append("Player_");
    path.append(playerId);
    path.push_back(kSeparator);
    path.append("saves");
    path.push_back(kSeparator);
    return path;
}

bool PartyQuestSkyrimSavePathPolicy::MatchesRelativeSavePath(
    std::string_view acPath,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    try
    {
        const std::string expected = BuildRelativeSavePath(
            acCampaignId,
            acPlayerProfileId);
        return !expected.empty() && acPath == expected;
    }
    catch (...)
    {
        return false;
    }
}

bool PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(
    std::string_view acPath) noexcept
{
    try
    {
        if (acPath.empty() || acPath.back() != kSeparator)
            return false;

        // One canonical separator prevents normalization differences between
        // Skyrim, SKSE and std::filesystem.
        if (acPath.find('/') != std::string_view::npos)
            return false;

        // Reject rooted Windows forms before component parsing.
        if (acPath.front() == kSeparator ||
            acPath.find(':') != std::string_view::npos ||
            StartsWith(acPath, "\\\\") ||
            StartsWith(acPath, "\\?\\") ||
            StartsWith(acPath, "\\.\\"))
        {
            return false;
        }

        // Remove the required trailing separator for exact component parsing.
        const std::string_view body = acPath.substr(0, acPath.size() - 1);
        const auto components = SplitComponents(body);
        if (components.size() != 4)
            return false;

        if (components[0] != kRoot ||
            !IsExpectedIdComponent(components[1], "Campaign_") ||
            !IsExpectedIdComponent(components[2], "Player_") ||
            components[3] != "saves")
        {
            return false;
        }

        for (const std::string_view component : components)
        {
            if (component.empty() || component == "." || component == "..")
                return false;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}
