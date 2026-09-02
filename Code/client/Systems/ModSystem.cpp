#include <TiltedOnlinePCH.h>

#include <Systems/ModSystem.h>

#include <Structs/Mods.h>
#include <Structs/GameId.h>
#include <Structs/Skyrim/PartyQuestExceptionBoundary.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <Games/TES.h>
#include <PartyQuestP0LiveDiagnostics.h>

#include <utility>

ModSystem::ModSystem(entt::dispatcher& aDispatcher) noexcept
{
    m_modsConnection = aDispatcher.sink<Mods>().connect<&ModSystem::HandleMods>(this);
}

ModSystem::~ModSystem() noexcept
{
    // Disconnect while every callback dependency is still alive. Member
    // destruction runs after this body and would otherwise destroy the mapping
    // state before m_modsConnection because the connection is declared first.
    m_modsConnection.release();
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
    if (!m_standardToServer.TryGet(static_cast<uint8_t>(hiByte & 0xFFu), aModId))
        return false;

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
        PartyQuestModMappingCandidateIdentity candidateIdentity;

        for (const auto& mod : acMods.ModList)
        {
            Mod* pMod = pModManager->GetByName(mod.Filename.c_str());
            if (!pMod)
            {
                const auto missingResult = candidateIdentity.Observe(
                    mod.Id, mod.IsLite, false);
                (void)missingResult;
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
                continue;
            }

            const uint16_t localModId = pMod->GetId();
            const bool localIsLite = pMod->IsLite();
            PartyQuestP0LiveDiagnostics::RecordModMappingEntry(
                mod.Id, mod.Filename.c_str(), mod.IsLite, true, localModId, localIsLite);
            ++resolvedCount;

            const auto identityResult = candidateIdentity.Observe(
                mod.Id, mod.IsLite, true, localModId, localIsLite);
            if (identityResult != PartyQuestModMappingIdentityResult::Accepted)
            {
                PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
                    "mod-mapping-rebuild",
                    "mapping-identity-conflict-fail-closed",
                    generationBefore,
                    generationAfter);
                (void)PartyQuestExceptionBoundary::Invoke(
                    [&]()
                    {
                        spdlog::error(
                            "PartyQuest rejected conflicting mod mapping for {}, server id {:X}, local id {:X}, identity result {}",
                            mod.Filename.c_str(),
                            mod.Id,
                            localModId,
                            static_cast<uint32_t>(identityResult));
                    });
                return;
            }

            if (!candidateServerToGame.emplace(
                     mod.Id,
                     GameMod{
                         static_cast<uint16_t>(localModId & (mod.IsLite ? 0xFFFu : 0xFFu)),
                         mod.IsLite})
                     .second)
            {
                return;
            }

            if (mod.IsLite &&
                !candidateLiteToServer.emplace(
                     static_cast<uint16_t>(localModId & 0xFFFu),
                     mod.Id)
                     .second)
            {
                return;
            }
        }

        // Every live mapping update below remains inside the exclusive generation
        // lease while publication is revoked. Conversion readers now hold the
        // corresponding shared execution lease, so they cannot observe any
        // intermediate state. If a concrete Map operation throws, the catch below
        // leaves publication revoked and readers fail closed after invalidation
        // releases. Do not require Map::swap to be noexcept: that specification is
        // allocator/STL-implementation dependent and differs on MSVC.
        m_serverToGame.swap(candidateServerToGame);
        m_liteToServer.swap(candidateLiteToServer);
        m_standardToServer = candidateIdentity.GetStandardMapping();
        m_mappingPublication.Commit();
    }
    catch (...)
    {
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "mod-mapping-rebuild",
            "mapping-rebuild-failed-closed",
            generationBefore,
            generationAfter);
        (void)PartyQuestExceptionBoundary::Invoke(
            []()
            {
                spdlog::error(
                    "PartyQuest mod mapping rebuild threw a C++ exception; mapping remains unpublished and conversion is fail-closed");
            });
        return;
    }

    PartyQuestP0LiveDiagnostics::RecordModMappingEnd(
        acMods.ModList.size(), resolvedCount, missingCount, generationAfter);
}
