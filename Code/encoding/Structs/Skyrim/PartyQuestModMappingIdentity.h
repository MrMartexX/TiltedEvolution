#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>
#include <unordered_set>

enum class PartyQuestModMappingIdentityResult : uint8_t
{
    Accepted,
    MissingLocalModSkipped,
    ServerLocalTypeMismatch,
    DuplicateServerId,
    DuplicateStandardLocalSlot,
    DuplicateLiteLocalSlot,
};

/**
 * Occupancy-aware reverse mapping for standard local mod slots.
 *
 * Server mod id zero is valid, so the mapped value cannot double as an empty
 * sentinel. Slot 0xFF retains the existing temporary-form mapping contract.
 */
class PartyQuestStandardModMapping final
{
public:
    PartyQuestStandardModMapping() noexcept
    {
        m_serverIds[0xFF] = std::numeric_limits<uint32_t>::max();
        m_occupied.set(0xFF);
    }

    [[nodiscard]] bool TryInsert(uint16_t aLocalModId, uint32_t aServerModId) noexcept
    {
        const auto slot = static_cast<uint8_t>(aLocalModId & 0xFFu);
        if (m_occupied.test(slot))
            return false;

        m_serverIds[slot] = aServerModId;
        m_occupied.set(slot);
        return true;
    }

    [[nodiscard]] bool TryGet(uint8_t aLocalSlot, uint32_t& aServerModId) const noexcept
    {
        if (!m_occupied.test(aLocalSlot))
            return false;

        aServerModId = m_serverIds[aLocalSlot];
        return true;
    }

    [[nodiscard]] bool IsOccupied(uint8_t aLocalSlot) const noexcept
    {
        return m_occupied.test(aLocalSlot);
    }

private:
    std::array<uint32_t, 0x100> m_serverIds{};
    std::bitset<0x100> m_occupied{};
};

/**
 * Pure candidate identity validator used before any ModSystem state is
 * published. Semantic conflicts leave the candidate rejected; missing local
 * mods remain an explicit no-op to preserve partial mapping behavior.
 */
class PartyQuestModMappingCandidateIdentity final
{
public:
    [[nodiscard]] PartyQuestModMappingIdentityResult Observe(
        uint32_t aServerModId,
        bool aServerIsLite,
        bool aLocalFound,
        uint16_t aLocalModId = 0,
        bool aLocalIsLite = false)
    {
        if (!aLocalFound)
            return PartyQuestModMappingIdentityResult::MissingLocalModSkipped;

        if (aServerIsLite != aLocalIsLite)
            return PartyQuestModMappingIdentityResult::ServerLocalTypeMismatch;

        if (m_serverIds.find(aServerModId) != m_serverIds.end())
            return PartyQuestModMappingIdentityResult::DuplicateServerId;

        if (aServerIsLite)
        {
            const auto slot = static_cast<uint16_t>(aLocalModId & 0xFFFu);
            if (m_liteSlots.test(slot))
                return PartyQuestModMappingIdentityResult::DuplicateLiteLocalSlot;

            m_serverIds.emplace(aServerModId);
            m_liteSlots.set(slot);
            return PartyQuestModMappingIdentityResult::Accepted;
        }

        const auto slot = static_cast<uint8_t>(aLocalModId & 0xFFu);
        if (m_standardMapping.IsOccupied(slot))
            return PartyQuestModMappingIdentityResult::DuplicateStandardLocalSlot;

        m_serverIds.emplace(aServerModId);
        if (!m_standardMapping.TryInsert(aLocalModId, aServerModId))
        {
            m_serverIds.erase(aServerModId);
            return PartyQuestModMappingIdentityResult::DuplicateStandardLocalSlot;
        }

        return PartyQuestModMappingIdentityResult::Accepted;
    }

    [[nodiscard]] const PartyQuestStandardModMapping& GetStandardMapping() const noexcept
    {
        return m_standardMapping;
    }

    [[nodiscard]] bool IsLiteSlotOccupied(uint16_t aLocalModId) const noexcept
    {
        return m_liteSlots.test(aLocalModId & 0xFFFu);
    }

private:
    std::unordered_set<uint32_t> m_serverIds;
    std::bitset<0x1000> m_liteSlots{};
    PartyQuestStandardModMapping m_standardMapping;
};
