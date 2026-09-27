#pragma once

#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <string>
#include <string_view>

/**
 * Pure, platform-independent policy for the Skyrim INI setting
 * sLocalSavePath:General used by a co-op replica session.
 *
 * Skyrim and SKSE interpret sLocalSavePath relative to the Skyrim user data
 * directory. The policy therefore never accepts an absolute root or arbitrary
 * caller-provided path components. Campaign/player ids are rendered through
 * the same fixed-width hexadecimal components as PartyQuestCoopSaveLayout.
 *
 * Canonical form:
 *
 *   CoopCampaigns\Campaign_<32HEX>\Player_<32HEX>\saves\
 *
 * The resulting string deliberately uses a single Windows separator form so a
 * later runtime setting scope cannot depend on filesystem normalization rules.
 */
class PartyQuestSkyrimSavePathPolicy final
{
public:
    [[nodiscard]] static std::string BuildRelativeSavePath(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId);

    [[nodiscard]] static bool MatchesRelativeSavePath(
        std::string_view acPath,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept;

    /**
     * Structural validation for an sLocalSavePath value before it is exposed
     * to Skyrim/SKSE. This rejects absolute/UNC/device paths, traversal, empty
     * components and slash-normalization ambiguity.
     */
    [[nodiscard]] static bool IsSafeRelativeSavePath(
        std::string_view acPath) noexcept;
};
