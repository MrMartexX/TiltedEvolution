#include <TiltedOnlinePCH.h>

#include <Systems/ModSystem.h>

#include <Structs/Mods.h>
#include <Structs/GameId.h>
#include <Structs/Skyrim/PartyQuestExceptionBoundary.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <Games/TES.h>
#include <PartyQuestP0LiveDiagnostics.h>

#include <array>
#include <utility>

ModSystem::ModSystem(entt::dispatcher& aDispatcher) noexcept
{
    std::memset(m_standardToServer, 0, sizeof(m_standardToServer));
    // Deal with temporary ids
    m_standardToServer[0xFF] = std::numeric_limits<uint32_t>::max();

    m_modsConnection = aDispatcher.sink<Mods>().connect<&ModSystem::HandleMods>(this);
}

bool ModSystem::GetServerModId(const uint32_t aGameId, uint32_t& aModId, uint32_t& aBaseId) const noexcept
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = generationFence.GetGeneration();
    auto generationExecution = generationFence.TryAcquire(generation);
    if (!generationExecution || !generationExecution->IsValid())
        return false;

    if (!m_mappingPublication.IsReady())
        return false;

    uint32_t hiByte = aGameId >> 24;

    // If it's a lite mod
    if (hiByte == 0xFE)
    {
        const uint16_t liteId = ((aGameId & 0x00FFF000) >> 12) & 0xFFFF;

        const auto itor = m_liteToServer.find(liteId);
        if (itor != std::end(m_liteToServer))
        {
            aModId = itor->second;
            aBaseId = aGameId & 0x00000FFF;
            return true;
        }

        return false;
    }

    // Here we have a standard mod
    aModId = m_standardToServer[hiByte & 0xFF];
    aBaseId = aGameId & 0x00FFFFFFu;

    return true;
}

bool ModSystem::GetServerModId(uint32_t aGameId, GameId& aServerId) const noexcept
{
    return GetServerModId(aGameId, aServerId.ModId, aServerId.BaseId);
}

uint32_t ModSystem::GetGameId(uint32_t aServerId, uint32_t aFormId) const noexcept
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generation = generationFence.GetGeneration();
    auto generationExecution = generationFence.TryAcquire(generation);
    if (!generationExecution || !generationExecution->IsValid())
        return 0;

    if (!m_mappingPublication.IsReady())
        return 0;

    auto itor = m_serverToGame.find(aServerId);
    if (itor != std::end(m_serverToGame))
    {
        if (itor->second.isLite)
        {
            aFormId &= 0xFFFu;
            aFormId |= 0xFE000000u;
            aFormId |= uint32_t(itor->second.id) << 12;
        }
        else
        {
            aFormId &= 0x00FFFFFFu;
            aFormId |= uint32_t(itor->second.id) << 24;
        }

        return aFormId;
    }

    return 0;
}

uint32_t ModSystem::GetGameId(const GameId& acGameId) const noexcept
{
    return GetGameId(acGameId.ModId, acGameId.BaseId);
}

void ModSystem::HandleMods(const Mods& acMods) noexcept
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generationBefore = generationFence.GetGeneration();

    // The server<->local FormID map is part of runtime mutation identity. Never
    // rebuild it unless the exact exclusive generation barrier is actually held.
    // A synchronization failure poisons the fence and therefore blocks all later
    // runtime execution instead of allowing a half-published mapping.
    auto generationInvalidation = generationFence.TryBeginInvalidation();
    if (!generationInvalidation || !generationInvalidation->IsValid())
    {
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "mod-mapping-rebuild",
            "exclusive-lease-unavailable-fail-closed",
            generationBefore,
            0);
        (void)PartyQuestExceptionBoundary::Invoke(
            []()
            {
                spdlog::error(
                    "PartyQuest rejected mod mapping rebuild because the runtime generation barrier is unavailable");
            });
        return;
    }

    const uint64_t generationAfter = generationInvalidation->GetGeneration();

    // Revoke the previously published mapping before candidate construction.
    // Conversion entry points remain fail-closed until the complete candidate is
    // committed below.
    m_mappingPublication.BeginRebuild();

    PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
        "mod-mapping-rebuild", "exclusive-lease-acquired", generationBefore, generationAfter);
    PartyQuestP0LiveDiagnostics::RecordModMappingBegin(
        acMods.ModList.size(), generationBefore, generationAfter);

    const auto pModManager = ModManager::Get();
    if (!pModManager)
    {
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "mod-mapping-rebuild",
            "mod-manager-unavailable-fail-closed",
            generationBefore,
            generationAfter);
        (void)PartyQuestExceptionBoundary::Invoke(
            []()
            {
                spdlog::error(
                    "PartyQuest rejected mod mapping rebuild because ModManager is unavailable");
            });
        return;
    }

    size_t resolvedCount = 0;
    size_t missingCount = 0;

    try
    {
        Map<uint32_t, GameMod> candidateServerToGame;
        Map<uint16_t, uint32_t> candidateLiteToServer;
        std::array<uint32_t, 0x100> candidateStandardToServer{};
        candidateStandardToServer[0xFF] =
            std::numeric_limits<uint32_t>::max();

        for (const auto& mod : acMods.ModList)
        {
            if (Mod* pMod = pModManager->GetByName(mod.Filename.c_str()))
            {
                const uint32_t localModId = pMod->GetId();
                const bool localIsLite = pMod->IsLite();
                PartyQuestP0LiveDiagnostics::RecordModMappingEntry(
                    mod.Id, mod.Filename.c_str(), mod.IsLite, true, localModId, localIsLite);
                ++resolvedCount;

                if (mod.IsLite)
                {
                    candidateServerToGame.emplace(
                        mod.Id,
                        GameMod{static_cast<uint16_t>(localModId & 0xFFFu), true});
                    candidateLiteToServer.emplace(
                        static_cast<uint16_t>(localModId & 0xFFFu),
                        mod.Id);
                }
                else
                {
                    candidateServerToGame.emplace(
                        mod.Id,
                        GameMod{static_cast<uint16_t>(localModId & 0xFFu), false});
                    candidateStandardToServer[localModId & 0xFFu] = mod.Id;
                }
            }
            else
            {
                PartyQuestP0LiveDiagnostics::RecordModMappingEntry(
                    mod.Id, mod.Filename.c_str(), mod.IsLite, false, 0, false);
                ++missingCount;
                (void)PartyQuestExceptionBoundary::Invoke(
                    [&]()
                    {
                        spdlog::error(
                            "Failed to find mod {}, is lite? {}, id: {:X}",
                            mod.Filename.c_str(),
                            mod.IsLite,
                            mod.Id);
                    });
            }
        }

        // Publication must not introduce a new exception point after the first
        // live container changes. If the concrete Map type ever loses noexcept
        // swap semantics, fail the build rather than weakening atomicity.
        static_assert(noexcept(m_serverToGame.swap(candidateServerToGame)));
        static_assert(noexcept(m_liteToServer.swap(candidateLiteToServer)));

        m_serverToGame.swap(candidateServerToGame);
        m_liteToServer.swap(candidateLiteToServer);
        std::memcpy(
            m_standardToServer,
            candidateStandardToServer.data(),
            sizeof(m_standardToServer));
        m_mappingPublication.Commit();
    }
    catch (...)
    {
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "mod-mapping-rebuild",
            "candidate-build-failed-closed",
            generationBefore,
            generationAfter);
        (void)PartyQuestExceptionBoundary::Invoke(
            []()
            {
                spdlog::error(
                    "PartyQuest mod mapping candidate rebuild threw a C++ exception; previous mapping remains unpublished and conversion is fail-closed");
            });
        return;
    }

    PartyQuestP0LiveDiagnostics::RecordModMappingEnd(
        acMods.ModList.size(), resolvedCount, missingCount, generationAfter);
}
