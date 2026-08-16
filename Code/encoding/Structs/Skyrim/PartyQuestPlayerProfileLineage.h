#pragma once

#include <Structs/Skyrim/PartyQuestPlayerProfile.h>

#include <cstdint>

class PartyQuestSkyrimPlayerProfileLineageResolver;
class PartyQuestPlayerProfileLineageTestAccess;

/**
 * Process-local proof that one stable PlayerProfileId belongs to the currently
 * loaded Skyrim character/save lineage for one exact runtime generation.
 *
 * The numeric PlayerProfileId and runtime generation are data, not authority.
 * Production code cannot mint this capability from a save filename, network
 * PlayerId or generated UUID. A future concrete Skyrim lineage resolver is the
 * only production issuer and must prove that the identity is persisted with the
 * character lineage and remains stable across save rename/Save As operations.
 *
 * Runtime bootstrap additionally acquires the exact process generation before
 * binding the owner, so a capability observed before LoadGame/NewGame/other
 * lifecycle invalidation cannot be reused after the character context changes.
 */
class PartyQuestPlayerProfileLineageAuthorization final
{
public:
    PartyQuestPlayerProfileLineageAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_profileId.IsValid() &&
            m_runtimeGeneration != 0 &&
            m_exactCharacterLineage &&
            m_persistedWithCharacterLineage &&
            m_filenameIndependent;
    }

    [[nodiscard]] const PartyQuestPlayerProfileId& GetProfileId() const noexcept
    {
        return m_profileId;
    }

    [[nodiscard]] uint64_t GetRuntimeGeneration() const noexcept
    {
        return m_runtimeGeneration;
    }

private:
    friend class PartyQuestSkyrimPlayerProfileLineageResolver;
    friend class PartyQuestPlayerProfileLineageTestAccess;

    PartyQuestPlayerProfileLineageAuthorization(
        PartyQuestPlayerProfileId aProfileId,
        uint64_t aRuntimeGeneration,
        bool aExactCharacterLineage,
        bool aPersistedWithCharacterLineage,
        bool aFilenameIndependent) noexcept
        : m_profileId(aProfileId)
        , m_runtimeGeneration(aRuntimeGeneration)
        , m_exactCharacterLineage(aExactCharacterLineage)
        , m_persistedWithCharacterLineage(aPersistedWithCharacterLineage)
        , m_filenameIndependent(aFilenameIndependent)
    {
    }

    PartyQuestPlayerProfileId m_profileId{};
    uint64_t m_runtimeGeneration{};
    bool m_exactCharacterLineage{};
    bool m_persistedWithCharacterLineage{};
    bool m_filenameIndependent{};
};
