#include <TiltedOnlinePCH.h>

#include "World.h"

#include <Services/DiscoveryService.h>
#include <Services/InputService.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Services/ImguiService.h>
#include <Services/PapyrusService.h>
#include <Services/DiscordService.h>
#include <Services/ObjectService.h>
#include <Services/QuestService.h>
#include <Services/PartyQuestRuntimeOwnerService.h>
#include <Services/ActorValueService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/CommandService.h>
#include <Services/CalendarService.h>
#include <Services/StringCacheService.h>
#include <Services/PlayerService.h>
#include <Services/CombatService.h>
#include <Services/WeatherService.h>
#include <Services/MapService.h>

#include <Events/PreUpdateEvent.h>
#include <Events/UpdateEvent.h>

#include <ModCompat/BehaviorVar.h>
#include <PartyQuestP0LiveDiagnostics.h>
#include <PartyQuestSkyrimReferenceReadiness.h>
#include <PlayerCharacter.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRegistryContextTeardown.h>

World::World()
    : m_runner(m_dispatcher)
    , m_transport(*this, m_dispatcher)
    , m_modSystem(m_dispatcher)
    , m_lastFrameTime{std::chrono::high_resolution_clock::now()}
{
    // Register before services begin observing gameplay state. The corresponding
    // Load_Impl hook is installed at module initialization; this process-lifetime
    // sink supplies the authoritative TESLoadGameEvent completion boundary for
    // its cross-thread lifecycle ticket.
    InstallPartyQuestLoadGameLifecycleFence();

    // TESObjectLoadedEvent is authoritative only for concrete local reference
    // load/unload evidence. It is generation-bound and intentionally does not
    // imply location-alias or scene readiness by itself.
    InstallPartyQuestSkyrimReferenceReadiness();

    ctx().emplace<ImguiService>();
    ctx().emplace<DiscoveryService>(*this, m_dispatcher);
    ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher);
    ctx().emplace<InputService>(ctx().at<OverlayService>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport);
    ctx().emplace<DebugService>(m_dispatcher, *this, m_transport, ctx().at<ImguiService>());
    ctx().emplace<PapyrusService>(m_dispatcher);
    ctx().emplace<DiscordService>(m_dispatcher);
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CalendarService>(*this, m_dispatcher, m_transport);
    ctx().emplace<QuestService>(*this, m_dispatcher);
    ctx().emplace<PartyService>(*this, m_dispatcher, m_transport);

    // Equal-party runtime ownership is created once per STR client World after
    // QuestService and PartyService exist, so bootstrap can observe only their
    // verified session state. The durable process owner itself survives World
    // recreation and is generation-invalidated by this service's boundaries.
    ctx().emplace<PartyQuestRuntimeOwnerService>(*this, m_dispatcher);

    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport);
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CommandService>(*this, m_transport, m_dispatcher);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport);
    ctx().emplace<StringCacheService>(m_dispatcher);
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher);
    ctx().emplace<WeatherService>(*this, m_transport, m_dispatcher);
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);

    BehaviorVar::Get()->Init();
}

World::~World()
{
    // entt::registry is a base class, therefore its context would otherwise be
    // destroyed only after World members. Context services own dispatcher
    // subscriptions and references to transport/member services, so tear them
    // down explicitly while those dependencies are still alive.
    PartyQuestDestroyRegistryContextBeforeMembers(*this);
}

void World::Update() noexcept
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

    // This is deliberately labelled as presence rather than an engine lifecycle
    // hook. It gives live timing evidence without pretending to be the required
    // pre-LoadGame/NewGame/MainMenu mutation barrier.
    static bool s_presenceInitialized = false;
    static bool s_lastInGame = false;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const bool inGame = pPlayer && pPlayer->GetNiNode();
    if (!s_presenceInitialized || inGame != s_lastInGame)
    {
        s_presenceInitialized = true;
        s_lastInGame = inGame;
        PartyQuestP0LiveDiagnostics::RecordGamePresence(inGame);
    }

    m_dispatcher.trigger(PreUpdateEvent(cDeltaSeconds));

    // Force run this before so we get the tasks scheduled to run
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
}

uint64_t World::GetTick() const noexcept
{
    return m_transport.GetClock().GetCurrentTick();
}

void World::Create() noexcept
{
    if (!entt::locator<World>::has_value())
    {
        entt::locator<World>::emplace();
    }
}

void World::Destroy() noexcept
{
    PartyQuestDestroyLocatedService<World>();
}

World& World::Get() noexcept
{
    return entt::locator<World>::value();
}
