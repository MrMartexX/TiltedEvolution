#pragma once

#include <cstdint>

/**
 * Stable server-owned identity for one cooperative campaign.
 *
 * Party ids are transient runtime routing handles. CampaignId survives server
 * restarts and party recreation and must never be replaced merely because a
 * different party becomes the active participant group.
 */
struct PartyQuestCampaignId
{
    uint64_t High{};
    uint64_t Low{};

    [[nodiscard]] bool IsValid() const noexcept { return High != 0 || Low != 0; }
    bool operator==(const PartyQuestCampaignId&) const noexcept = default;
};
