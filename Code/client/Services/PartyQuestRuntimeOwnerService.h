#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/Skyrim/PartyQuestRuntimeBootstrapSignal.h>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct UpdateEvent;
struct NotifyPartyQuestRepairPlan;

/**
 * Client-session bootstrap/orchestration for the process PartyQuestRuntimeOwner.
 *
 * Network/party events are translated into the same generation domain used by
 * Skyrim identity hooks. Bootstrap is edge-triggered only when new authoritative
 * campaign or character-load evidence may exist; Update never scans on a timer.
 * The final bind still requires the server-verified campaign plus a stable SKSE
 * character-lineage authorization under the current generation lease.
 */
class PartyQuestRuntimeOwnerService final : public BSTEventSink<TESLoadGameEvent>
{
public:
    PartyQuestRuntimeOwnerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PartyQuestRuntimeOwnerService() noexcept;

    PartyQuestRuntimeOwnerService(const PartyQuestRuntimeOwnerService&) = delete;
    PartyQuestRuntimeOwnerService& operator=(const PartyQuestRuntimeOwnerService&) = delete;

private:
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPartyJoined(const PartyJoinedEvent&) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;
    void OnPartyQuestRepairPlan(const NotifyPartyQuestRepairPlan&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;

    BSTEventResult OnEvent(
        const TESLoadGameEvent*,
        const EventDispatcher<TESLoadGameEvent>*) override;

    void TryBootstrap() noexcept;

    World& m_world;
    PartyQuestRuntimeBootstrapSignal m_bootstrapSignal;
    EventDispatcher<TESLoadGameEvent>* m_pLoadGameDispatcher{};

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_partyQuestRepairPlanConnection;
    entt::scoped_connection m_updateConnection;
};
