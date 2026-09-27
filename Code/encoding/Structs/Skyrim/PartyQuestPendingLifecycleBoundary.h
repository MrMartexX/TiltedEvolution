#pragma once

#include <cstdint>

enum class PartyQuestPendingLifecycleReason : uint16_t
{
    Connected = 1u << 0u,
    PartyJoined = 1u << 1u,
    PartyLeft = 1u << 2u,
    CampaignSwitch = 1u << 3u,
    Disconnect = 1u << 4u,
    LoadGame = 1u << 5u,
    Shutdown = 1u << 6u
};

enum class PartyQuestPendingSessionRelease : uint8_t
{
    None,
    PartyLeft,
    CampaignSwitch,
    Disconnect,
    Shutdown
};

struct PartyQuestPendingLifecycleDisposition
{
    uint16_t Reasons{};
    uint64_t Revision{};
    PartyQuestPendingSessionRelease Release{
        PartyQuestPendingSessionRelease::None};
    bool ResetBootstrap{};
    bool ApplyConnectedBoundary{};
    bool ApplyPartyJoinedBoundary{};
    bool BlockForLoadGame{};
    bool Shutdown{};

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return Reasons == 0;
    }

    [[nodiscard]] constexpr bool Has(
        PartyQuestPendingLifecycleReason aReason) const noexcept
    {
        return (Reasons & static_cast<uint16_t>(aReason)) != 0;
    }
};

/**
 * Pure, monotonic accumulator for lifecycle boundaries that could not yet be
 * applied by their runtime owner.
 *
 * Reasons are retained independently so normalization never erases distinct
 * evidence (notably LoadGame and CampaignSwitch). Release precedence is an
 * explicit policy rather than enum ordering. Any pending release or LoadGame
 * suppresses reconnect/rejoin actions until the complete snapshot is applied;
 * callers must not partially clear it.
 */
class PartyQuestPendingLifecycleBoundary final
{
public:
    constexpr void Add(PartyQuestPendingLifecycleReason aReason) noexcept
    {
        const auto reason = static_cast<uint16_t>(aReason);
        if ((m_reasons & reason) != 0)
            return;

        m_reasons |= reason;
        ++m_revision;
    }

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return m_reasons == 0;
    }

    [[nodiscard]] constexpr bool Has(
        PartyQuestPendingLifecycleReason aReason) const noexcept
    {
        return (m_reasons & static_cast<uint16_t>(aReason)) != 0;
    }

    [[nodiscard]] constexpr PartyQuestPendingLifecycleDisposition Normalize()
        const noexcept
    {
        PartyQuestPendingLifecycleDisposition result;
        result.Reasons = m_reasons;
        result.Revision = m_revision;
        if (result.Empty())
            return result;

        result.ResetBootstrap = true;
        result.BlockForLoadGame = Has(PartyQuestPendingLifecycleReason::LoadGame);
        result.Shutdown = Has(PartyQuestPendingLifecycleReason::Shutdown);

        if (result.Shutdown)
            result.Release = PartyQuestPendingSessionRelease::Shutdown;
        else if (Has(PartyQuestPendingLifecycleReason::Disconnect))
            result.Release = PartyQuestPendingSessionRelease::Disconnect;
        else if (Has(PartyQuestPendingLifecycleReason::CampaignSwitch))
            result.Release = PartyQuestPendingSessionRelease::CampaignSwitch;
        else if (Has(PartyQuestPendingLifecycleReason::PartyLeft))
            result.Release = PartyQuestPendingSessionRelease::PartyLeft;

        const bool blocksRebind = result.Shutdown || result.BlockForLoadGame ||
            result.Release != PartyQuestPendingSessionRelease::None;
        if (!blocksRebind)
        {
            result.ApplyConnectedBoundary =
                Has(PartyQuestPendingLifecycleReason::Connected);
            result.ApplyPartyJoinedBoundary =
                Has(PartyQuestPendingLifecycleReason::PartyJoined);
        }

        return result;
    }

    // Clear only the exact snapshot whose complete normalized action succeeded.
    // A reason added during application changes the revision and is preserved.
    [[nodiscard]] constexpr bool ClearAfterSuccessfulApplication(
        const PartyQuestPendingLifecycleDisposition& aApplied) noexcept
    {
        if (aApplied.Empty() || aApplied.Reasons != m_reasons ||
            aApplied.Revision != m_revision)
        {
            return false;
        }

        m_reasons = 0;
        return true;
    }

private:
    uint16_t m_reasons{};
    uint64_t m_revision{};
};
