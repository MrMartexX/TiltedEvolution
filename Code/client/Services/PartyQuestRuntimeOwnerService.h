#pragma once

#include <Events/EventDispatcher.h>

#include <chrono>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct UpdateEvent;

/**
 * Client-session bootstrap/orchestration for the process PartyQuestRuntimeOwner.
 *
 * Network/party events are translated into the same generation domain used by
 * Skyrim identity hooks. Update performs fail-closed production binding once a
 * server-verified campaign and a stable SKSE character-lineage authorization
 * exist. No generated/network id is ever substituted for PlayerProfileId.
 */
class PartyQuestRuntimeOwnerService final
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
    void OnUpdate(const UpdateEvent&) noexcept;

    void TryBootstrap() noexcept;

    World& m_world;
    std::chrono::steady_clock::time_point m_nextBootstrapAttempt{};

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_updateConnection;
};
