#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>

enum class PartyQuestModMappingIdentityResult : uint8_t
{
    Accepted = 0,
    DuplicateServerId,
    KindMismatch,
    DuplicateLiteSlot,
};

[[nodiscard]] constexpr const char* GetPartyQuestModMappingIdentityDiagnosticReason(
    PartyQuestModMappingIdentityResult aResult) noexcept
{
    switch (aResult)
    {
    case PartyQuestModMappingIdentityResult::Accepted:
        return "accepted";
    case PartyQuestModMappingIdentityResult::DuplicateServerId:
        return "duplicate-server-id-fail-closed";
    case PartyQuestModMappingIdentityResult::KindMismatch:
        return "server-local-kind-mismatch-fail-closed";
    case PartyQuestModMappingIdentityResult::DuplicateLiteSlot:
        return "duplicate-local-lite-slot-fail-closed";
    }

    return "unknown-identity-conflict-fail-closed";
}

/**
 * Candidate-only identity validator for server<->local mod mappings.
 *
 * Missing local mods remain valid partial-mapping entries, but their server IDs
 * still participate in uniqueness validation. Resolved entries must preserve
 * the server/local standard-vs-lite kind and may not alias a local lite slot.
 */
class PartyQuestModMappingIdentityCandidate final
{
public:
    [[nodiscard]] PartyQuestModMappingIdentityResult Register(
        uint16_t aServerModId,
        bool aServerIsLite,
        bool aResolved,
        uint32_t aLocalModId,
        bool aLocalIsLite) noexcept
    {
        if (m_serverIds.test(aServerModId))
            return PartyQuestModMappingIdentityResult::DuplicateServerId;

        m_serverIds.set(aServerModId);

        if (!aResolved)
            return PartyQuestModMappingIdentityResult::Accepted;

        if (aServerIsLite != aLocalIsLite)
            return PartyQuestModMappingIdentityResult::KindMismatch;

        if (aServerIsLite)
        {
            const size_t liteSlot = static_cast<size_t>(aLocalModId & 0xFFFu);
            if (m_liteSlots.test(liteSlot))
                return PartyQuestModMappingIdentityResult::DuplicateLiteSlot;

            m_liteSlots.set(liteSlot);
        }

        return PartyQuestModMappingIdentityResult::Accepted;
    }

private:
    std::bitset<1u << 16> m_serverIds{};
    std::bitset<1u << 12> m_liteSlots{};
};

/**
 * Standard-mod reverse mapping with occupancy stored independently from value.
 *
 * Server mod ID zero is valid, so zero cannot represent an unoccupied local
 * slot. The reserved 0xFF temporary-form slot is always mapped to UINT32_MAX.
 */
class PartyQuestStandardModMapping final
{
public:
    PartyQuestStandardModMapping() noexcept
    {
        m_serverIds[0xFF] = std::numeric_limits<uint32_t>::max();
        m_occupied.set(0xFF);
    }

    [[nodiscard]] bool TryAssign(uint8_t aLocalSlot, uint16_t aServerModId) noexcept
    {
        if (m_occupied.test(aLocalSlot))
            return false;

        m_serverIds[aLocalSlot] = aServerModId;
        m_occupied.set(aLocalSlot);
        return true;
    }

    [[nodiscard]] bool TryLookup(uint8_t aLocalSlot, uint32_t& aServerModId) const noexcept
    {
        if (!m_occupied.test(aLocalSlot))
            return false;

        aServerModId = m_serverIds[aLocalSlot];
        return true;
    }

private:
    std::array<uint32_t, 0x100> m_serverIds{};
    std::bitset<0x100> m_occupied{};
};
