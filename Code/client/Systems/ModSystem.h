#pragma once

#include <Structs/Skyrim/PartyQuestModMappingIdentity.h>
#include <Structs/Skyrim/PartyQuestModMappingPublication.h>

struct Mods;
struct GameId;

/**
 * @brief Makes sure that different clients can have independent load orders and mods.
 */
struct ModSystem
{
    ModSystem(entt::dispatcher& aDispatcher) noexcept;
    ~ModSystem() noexcept;

    TP_NOCOPYMOVE(ModSystem);

    bool GetServerModId(uint32_t aGameId, uint32_t& aModId, uint32_t& aBaseId) const noexcept;
    bool GetServerModId(uint32_t aGameId, GameId& aServerId) const noexcept;
    uint32_t GetGameId(uint32_t aServerId, uint32_t aFormId) const noexcept;
    uint32_t GetGameId(const GameId& acGameId) const noexcept;

protected:
    void HandleMods(const Mods& acMods) noexcept;

private:
    struct GameMod
    {
        uint16_t id;
        bool isLite;
    };

    entt::scoped_connection m_modsConnection;
    PartyQuestModMappingPublication m_mappingPublication;
    Map<uint16_t, uint32_t> m_liteToServer;
    Map<uint32_t, GameMod> m_serverToGame;
    PartyQuestStandardModMapping m_standardToServer;
};
