#pragma once

#include <Messages/PartyQuestMessages.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

enum class PartyQuestTransactionHandleStatus : uint8_t
{
    Processed,
    DuplicateRequest,
    UnknownClient,
    InvalidMessage,
    RequestIdConflict,
    PersistenceFailure
};

struct PartyQuestTransactionDispatch
{
    PartyQuestTransactionHandleStatus Status{PartyQuestTransactionHandleStatus::InvalidMessage};
    NotifyPartyQuestTransactionResult Response;
    std::optional<NotifyPartyQuestCanonicalUpdate> Broadcast;
    std::vector<uint32_t> Recipients;
};

enum class PartyQuestReportHandleStatus : uint8_t
{
    Generated,
    DuplicateReport,
    UnknownClient,
    InvalidMessage,
    ReportIdConflict
};

struct PartyQuestReportDispatch
{
    PartyQuestReportHandleStatus Status{PartyQuestReportHandleStatus::InvalidMessage};
    std::optional<NotifyPartyQuestRepairPlan> Response;
};

enum class PartyQuestAckHandleStatus : uint8_t
{
    Verified,
    DuplicateAck,
    RepairRejected,
    ClientAhead,
    UnknownClient,
    UnknownPlan,
    InvalidMessage,
    AckConflict
};

struct PartyQuestAckResult
{
    PartyQuestAckHandleStatus Status{PartyQuestAckHandleStatus::InvalidMessage};
    PartyQuestRepairPlanStatus VerificationStatus{PartyQuestRepairPlanStatus::RepairRequired};
};

struct PartyQuestCoordinatorSessionInfo
{
    bool Connected{};
    uint64_t ConnectionGeneration{};
    uint64_t LastReportedWorldRevision{};
    uint64_t LastVerifiedWorldRevision{};
    uint64_t PendingPlanId{};
    bool LastReportWasReconnect{};
};

/**
 * Game-independent server protocol coordinator.
 *
 * It owns canonical quest state, correlates requests, caches duplicate replies,
 * tracks reconnect/repair sessions, and emits canonical broadcasts. It does not
 * touch Skyrim runtime state or save files directly. An optional durable commit
 * handler can persist a candidate state before it becomes canonical.
 */
class PartyQuestProtocolCoordinator final
{
public:
    using DurableCommitHandler = std::function<bool(const PartyQuestState&)>;

    bool ConnectClient(uint32_t aClientId);
    bool DisconnectClient(uint32_t aClientId);
    [[nodiscard]] bool IsClientConnected(uint32_t aClientId) const noexcept;

    /** Installs a pre-commit durability barrier. An empty handler disables it. */
    void SetDurableCommitHandler(DurableCommitHandler aHandler);

    /** Restores validated canonical state before any protocol session is created. */
    [[nodiscard]] bool RestoreCanonicalState(PartyQuestState aState);

    [[nodiscard]] PartyQuestTransactionDispatch HandleTransaction(
        uint32_t aClientId,
        const RequestPartyQuestTransaction& acRequest);

    [[nodiscard]] PartyQuestReportDispatch HandleReplicaReport(
        uint32_t aClientId,
        const RequestPartyQuestReplicaReport& acRequest);

    [[nodiscard]] PartyQuestAckResult HandleRepairAck(
        uint32_t aClientId,
        const RequestPartyQuestRepairAck& acAck);

    [[nodiscard]] const PartyQuestState& GetCanonicalState() const noexcept { return m_state; }
    [[nodiscard]] const PartyQuestCoordinatorSessionInfo* FindSession(uint32_t aClientId) const noexcept;

private:
    struct TransactionCacheEntry
    {
        PartyQuestTransaction Transaction;
        NotifyPartyQuestTransactionResult Response;
    };

    struct ReportCacheEntry
    {
        bool IsReconnect{};
        PartyQuestReplicaReport Report;
        NotifyPartyQuestRepairPlan Response;
    };

    struct PlanCacheEntry
    {
        uint64_t ReportId{};
        PartyQuestRepairPlan Plan;
        std::optional<RequestPartyQuestRepairAck> Ack;
        PartyQuestAckResult AckResult;
    };

    struct Session
    {
        PartyQuestCoordinatorSessionInfo Info;
        std::unordered_map<uint64_t, TransactionCacheEntry> Transactions;
        std::unordered_map<uint64_t, ReportCacheEntry> Reports;
        std::unordered_map<uint64_t, PlanCacheEntry> Plans;
    };

    [[nodiscard]] Session* FindMutableConnectedSession(uint32_t aClientId) noexcept;
    [[nodiscard]] uint64_t AllocatePlanId() noexcept;
    [[nodiscard]] std::vector<uint32_t> BuildConnectedRecipientList() const;

    PartyQuestState m_state;
    DurableCommitHandler m_durableCommitHandler;
    uint64_t m_nextPlanId{1};
    std::unordered_map<uint32_t, Session> m_sessions;
};

enum class PartyQuestClientCanonicalStatus : uint8_t
{
    Applied,
    Duplicate,
    InvalidMessage,
    TransactionConflict,
    RevisionGap
};

enum class PartyQuestClientRepairStatus : uint8_t
{
    Applied,
    NoChanges,
    ClientAhead,
    StalePlan,
    InvalidPlan,
    Duplicate,
    PlanConflict
};

struct PartyQuestClientRepairResult
{
    PartyQuestClientRepairStatus Status{PartyQuestClientRepairStatus::InvalidPlan};
    RequestPartyQuestRepairAck Ack;
};

/** In-memory client replica/session used before Skyrim runtime repair is enabled. */
class PartyQuestClientSession final
{
public:
    explicit PartyQuestClientSession(uint32_t aClientId)
        : m_clientId(aClientId)
    {
    }

    [[nodiscard]] uint32_t GetClientId() const noexcept { return m_clientId; }
    [[nodiscard]] const PartyQuestReplica& GetReplica() const noexcept { return m_replica; }
    [[nodiscard]] PartyQuestReplica& GetReplica() noexcept { return m_replica; }

    [[nodiscard]] RequestPartyQuestReplicaReport BuildReplicaReport(uint64_t aReportId, bool aReconnect) const;
    [[nodiscard]] PartyQuestClientCanonicalStatus HandleCanonicalUpdate(const NotifyPartyQuestCanonicalUpdate& acUpdate);
    [[nodiscard]] PartyQuestClientRepairResult HandleRepairPlan(const NotifyPartyQuestRepairPlan& acPlan);

private:
    struct CanonicalCacheEntry
    {
        NotifyPartyQuestCanonicalUpdate Update;
    };

    struct RepairCacheEntry
    {
        NotifyPartyQuestRepairPlan Plan;
        RequestPartyQuestRepairAck Ack;
        PartyQuestClientRepairStatus Status{PartyQuestClientRepairStatus::InvalidPlan};
    };

    uint32_t m_clientId{};
    PartyQuestReplica m_replica;
    std::unordered_map<uint64_t, CanonicalCacheEntry> m_canonicalUpdates;
    std::unordered_map<uint64_t, RepairCacheEntry> m_repairs;
};

enum class PartyQuestClientSubmissionStatus : uint8_t
{
    Ready,
    Queued,
    ReplacedQueued,
    Duplicate,
    InvalidSnapshot
};

struct PartyQuestClientSubmissionDecision
{
    PartyQuestClientSubmissionStatus Status{PartyQuestClientSubmissionStatus::InvalidSnapshot};
    std::optional<QuestSnapshot> ReadySnapshot;
};

/**
 * Client-side per-quest submission gate.
 *
 * At most one transaction per quest may be in flight. Newer observations are
 * coalesced to the latest semantic snapshot, while equivalent start/stage
 * events and snapshots already present in the canonical replica are suppressed.
 */
class PartyQuestClientSubmissionQueue final
{
public:
    [[nodiscard]] PartyQuestClientSubmissionDecision Observe(
        const QuestSnapshot& acSnapshot,
        const PartyQuestReplica& acReplica);

    bool MarkInFlight(uint64_t aTransactionId, const QuestSnapshot& acSnapshot);
    [[nodiscard]] std::optional<QuestSnapshot> Complete(
        uint64_t aTransactionId,
        const QuestSnapshot& acCanonicalSnapshot);
    bool Reject(uint64_t aTransactionId);

    [[nodiscard]] std::vector<QuestSnapshot> TakeReady(const PartyQuestReplica& acReplica);
    void RequeueInFlight();
    void Clear() noexcept;

    [[nodiscard]] size_t GetInFlightCount() const noexcept;
    [[nodiscard]] size_t GetQueuedCount() const noexcept;

private:
    struct SubmissionSnapshot
    {
        QuestSnapshot Snapshot;
        uint64_t SemanticDigest{};
    };

    struct QuestEntry
    {
        std::optional<SubmissionSnapshot> InFlight;
        std::optional<SubmissionSnapshot> Queued;
    };

    std::unordered_map<GameId, QuestEntry> m_quests;
    std::unordered_map<uint64_t, GameId> m_transactionQuests;
};
