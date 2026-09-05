#pragma once

#include <Structs/Skyrim/PartyQuestLineageProviderAbi.h>
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

/**
 * Production adapter for the SKSE co-save lineage bridge.
 *
 * Resolve() never loads a DLL and never accepts a generated/session/network
 * identifier. It samples only an already-loaded exact bridge export under the
 * current runtime-generation lease. The private pure helper requires two
 * field-for-field stable, persisted ABI-v1 snapshots before issuing authority.
 */
class PartyQuestSkyrimPlayerProfileLineageResolver final
{
public:
    [[nodiscard]] static PartyQuestPlayerProfileLineageAuthorization Resolve() noexcept;

private:
    friend class PartyQuestPlayerProfileLineageTestAccess;

    [[nodiscard]] static PartyQuestPlayerProfileLineageAuthorization ResolveStableSnapshots(
        const PartyQuestLineageProviderDescriptor& acProvider,
        const PartyQuestLineageRuntimeVersion& acExpectedRuntime,
        const PartyQuestLineageBridgeSnapshot& acFirst,
        const PartyQuestLineageBridgeSnapshot& acSecond,
        uint64_t aRuntimeGeneration) noexcept
    {
        constexpr uint32_t kAbiVersion = 1u;
        constexpr uint32_t kSnapshotSize =
            static_cast<uint32_t>(sizeof(PartyQuestLineageBridgeSnapshot));

        const PartyQuestPlayerProfileId profile{
            acFirst.ProfileHigh,
            acFirst.ProfileLow};
        if (!PartyQuestLineageTargetRuntimeRegistry::IsApprovedDescriptor(
                acProvider,
                acExpectedRuntime) ||
            aRuntimeGeneration == 0 ||
            acFirst.AbiVersion != kAbiVersion ||
            acFirst.StructSize != kSnapshotSize ||
            acFirst.Sequence == 0 ||
            acFirst.State != static_cast<uint32_t>(
                PartyQuestLineageBridgeEvidenceState::Persisted) ||
            acFirst.Reserved != 0 ||
            !profile.IsValid() ||
            acSecond.AbiVersion != acFirst.AbiVersion ||
            acSecond.StructSize != acFirst.StructSize ||
            acSecond.Sequence != acFirst.Sequence ||
            acSecond.State != acFirst.State ||
            acSecond.Reserved != acFirst.Reserved ||
            acSecond.ProfileHigh != acFirst.ProfileHigh ||
            acSecond.ProfileLow != acFirst.ProfileLow)
        {
            return {};
        }

        return PartyQuestPlayerProfileLineageAuthorization(
            profile,
            aRuntimeGeneration,
            true,
            true,
            true);
    }
};
