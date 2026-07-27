#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>
#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <optional>

struct World;
struct UpdateEvent;
struct Player;
struct PlayerLeaveEvent;
struct RequestQuestUpdate;
struct RequestPartyQuestTransaction;
struct RequestPartyQuestReplicaReport;
struct RequestPartyQuestRepairAck;

/**
 * @brief Dispatch quest sync messages.
 *
 * The legacy stage-only path remains unchanged. The equal-party protocol path
 * is diagnostic-only and guarded by a server setting; it never mutates Skyrim
 * runtime state or save files.
 */
class QuestService
{
public:
    QuestService(World& aWorld, entt::dispatcher& aDispatcher);

private:
    void OnQuestChanges(const PacketEvent<RequestQuestUpdate>& aChanges) noexcept;

    void OnPartyQuestTransaction(const PacketEvent<RequestPartyQuestTransaction>& acMessage) noexcept;
    void OnPartyQuestReplicaReport(const PacketEvent<RequestPartyQuestReplicaReport>& acMessage) noexcept;
    void OnPartyQuestRepairAck(const PacketEvent<RequestPartyQuestRepairAck>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

    [[nodiscard]] bool PreparePartyQuestClient(Player* apPlayer, uint32_t& aPartyId) noexcept;
    void SendCanonicalUpdateToCampaign(
        const NotifyPartyQuestCanonicalUpdate& acUpdate,
        const std::vector<uint32_t>& acRecipients,
        uint32_t aPartyId) const noexcept;

    World& m_world;
    PartyQuestProtocolCoordinator m_partyQuestCoordinator;
    std::optional<uint32_t> m_campaignPartyId;

    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_partyQuestTransactionConnection;
    entt::scoped_connection m_partyQuestReplicaReportConnection;
    entt::scoped_connection m_partyQuestRepairAckConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
