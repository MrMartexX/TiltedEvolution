#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <optional>

struct NotifyQuestUpdate;
struct NotifyPartyQuestTransactionResult;
struct NotifyPartyQuestRepairPlan;
struct NotifyPartyQuestCanonicalUpdate;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;

struct TESQuest;

/**
 * @brief Handles quest sync.
 *
 * The legacy stage-only runtime path remains unchanged. The equal-party path is
 * diagnostic-only: it mirrors canonical protocol state in memory and never
 * applies repair snapshots to Skyrim or save files.
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>, BSTEventSink<TESQuestStageEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);

private:
    friend struct QuestEventHandler;

    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPartyJoined(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeft(const PartyLeftEvent& acEvent) noexcept;

    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void OnPartyQuestTransactionResult(const NotifyPartyQuestTransactionResult& acResult) noexcept;
    void OnPartyQuestRepairPlan(const NotifyPartyQuestRepairPlan& acPlan) noexcept;
    void OnPartyQuestCanonicalUpdate(const NotifyPartyQuestCanonicalUpdate& acUpdate) noexcept;

    void CollectLogAndSubmitPartyQuestSnapshot(uint32_t aFormId, const char* acReason) noexcept;
    void SendPartyQuestReplicaReport(bool aReconnect, const char* acReason) noexcept;
    [[nodiscard]] uint64_t AllocateScopedId(uint64_t& aSequence) noexcept;

    World& m_world;

    uint32_t m_localPlayerId{};
    uint64_t m_connectionGeneration{};
    uint64_t m_nextRequestSequence{1};
    uint64_t m_nextReportSequence{1};
    uint64_t m_nextTransactionSequence{1};
    std::optional<PartyQuestClientSession> m_partyQuestSession;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_partyQuestTransactionResultConnection;
    entt::scoped_connection m_partyQuestRepairPlanConnection;
    entt::scoped_connection m_partyQuestCanonicalUpdateConnection;
};
