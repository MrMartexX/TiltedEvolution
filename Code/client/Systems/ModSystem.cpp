#include <TiltedOnlinePCH.h>

#include <Systems/ModSystem.h>

#include <Structs/Mods.h>
#include <Structs/GameId.h>
#include <Structs/Skyrim/PartyQuestExceptionBoundary.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

#include <Games/TES.h>
#include <PartyQuestP0LiveDiagnostics.h>

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

    // Here we have a standard mod. Occupancy is independent from the stored
    // server ID because zero is a valid server-assigned mod ID.
    const uint8_t standardId = static_cast<uint8_t>(hiByte & 0xFFu);
    if (!m_standardToServer.TryLookup(standardId, aModId))
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
        PartyQuestStandardModMapping candidateStandardToServer;
        PartyQuestModMappingIdentityCandidate candidateIdentity;

        for (const auto& mod : acMods.ModList)
        {
            Mod* pMod = pModManager->GetByName(mod.Filename.c_str());
            const bool resolved = pMod != nullptr;
            uint32_t localModId = 0;
            bool localIsLite = false;

            if (resolved)
            {
                localModId = pMod->GetId();
                localIsLite = pMod->IsLite();
                PartyQuestP0LiveDiagnostics::RecordModMappingEntry(
                    mod.Id, mod.Filename.c_str(), mod.IsLite, true, localModId, localIsLite);
                ++resolvedCount;
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

            // Validate the complete wire identity even for locally missing mods.
            // Missing mods remain a supported partial mapping, but malformed or
            // ambiguous server IDs and resolved local identities are rejected.
            const auto identityResult = candidateIdentity.Register(
                mod.Id, mod.IsLite, resolved, localModId, localIsLite);
            if (identityResult != PartyQuestModMappingIdentityResult::Accepted)
            {
                const char* reason =
                    GetPartyQuestModMappingIdentityDiagnosticReason(identityResult);
                PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
                    "mod-mapping-rebuild", reason, generationBefore, generationAfter);
                (void)PartyQuestExceptionBoundary::Invoke(
                    [&]()
                    {
                        spdlog::error(
                            "PartyQuest rejected ambiguous mod mapping entry {} (server id {:X}): {}",
                            mod.Filename.c_str(),
                            mod.Id,
                            reason);
                    });
                return;
            }

            if (!resolved)
                continue;

            if (mod.IsLite)
            {
                const uint16_t localLiteId =
                    static_cast<uint16_t>(localModId & 0xFFFu);
                candidateServerToGame.emplace(
                    mod.Id,
                    GameMod{localLiteId, true});
                candidateLiteToServer.emplace(localLiteId, mod.Id);
            }
            else
            {
                const uint8_t localStandardId =
                    static_cast<uint8_t>(localModId & 0xFFu);
                if (!candidateStandardToServer.TryAssign(localStandardId, mod.Id))
                {
                    PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
                        "mod-mapping-rebuild",
                        "duplicate-local-standard-slot-fail-closed",
                        generationBefore,
                        generationAfter);
                    (void)PartyQuestExceptionBoundary::Invoke(
                        [&]()
                        {
                            spdlog::error(
                                "PartyQuest rejected ambiguous standard mod mapping entry {} (local slot {:X}, server id {:X})",
                                mod.Filename.c_str(),
                                localStandardId,
                                mod.Id);
                        });
                    return;
                }

                candidateServerToGame.emplace(
                    mod.Id,
                    GameMod{localStandardId, false});
            }
        }

        // Every live mapping update below remains inside the exclusive generation
        // lease while publication is revoked. Conversion readers hold the
        // corresponding shared execution lease, so they cannot observe any
        // intermediate state. If a concrete Map operation throws, the catch below
        // leaves publication revoked and readers fail closed after invalidation
        // releases. The fixed-size standard mapping copy cannot throw.
        m_serverToGame.swap(candidateServerToGame);
        m_liteToServer.swap(candidateLiteToServer);
        m_standardToServer = candidateStandardToServer;
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
