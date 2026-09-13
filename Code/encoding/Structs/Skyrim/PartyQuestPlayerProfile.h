#pragma once

#include <cstdint>

/**
 * Stable local identity for one co-op character/profile.
 *
 * This is deliberately independent of transient network PlayerId. It will bind
 * per-player save replicas, checkpoints and runtime-apply sidecars to the same
 * character across reconnects and server restarts.
 */
struct PartyQuestPlayerProfileId
{
    uint64_t High{};
    uint64_t Low{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return High != 0 || Low != 0;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
    bool operator==(const PartyQuestPlayerProfileId&) const noexcept = default;
};
