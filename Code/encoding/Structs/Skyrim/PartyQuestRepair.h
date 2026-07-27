#pragma once

#include <Structs/Skyrim/PartyQuestState.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

/** Compact client report used to compare a local quest replica with the server. */
struct PartyQuestReplicaEntry
{
    uint64_t QuestRevision{};
    uint64_t Digest{};

    bool operator==(const PartyQuestReplicaEntry&) const noexcept = default;
};

struct PartyQuestReplicaReport
{
    uint64_t WorldRevision{};
    std::unordered_map<GameId, PartyQuestReplicaEntry> Quests;
};

enum class PartyQuestRepairReason : uint8_t
{
    MissingQuest,
    RevisionMismatch,
    DigestMismatch
};

struct PartyQuestRepairItem
{
    PartyQuestRepairReason Reason{PartyQuestRepairReason::MissingQuest};
    QuestSnapshot CanonicalSnapshot;

    bool operator==(const PartyQuestRepairItem&) const noexcept = default;
};

enum class PartyQuestRepairPlanStatus : uint8_t
{
    UpToDate,
    RepairRequired,
    ClientAhead
};

/**
 * Server-generated repair plan for one client replica.
 *
 * Client-only quests are deliberately ignored: only quests owned by the
 * canonical campaign are repaired. This keeps personal/local quests outside
 * the shared campaign unless they are explicitly admitted into PartyQuestState.
 */
struct PartyQuestRepairPlan
{
    PartyQuestRepairPlanStatus Status{PartyQuestRepairPlanStatus::UpToDate};
    uint64_t BaseClientWorldRevision{};
    uint64_t TargetWorldRevision{};
    std::vector<PartyQuestRepairItem> Items;
};

class PartyQuestRepairPlanner final
{
public:
    [[nodiscard]] static PartyQuestRepairPlan Build(
        const PartyQuestState& acCanonicalState,
        const PartyQuestReplicaReport& acClientReport);
};

enum class PartyQuestReplicaApplyStatus : uint8_t
{
    Applied,
    NoChanges,
    ClientAhead,
    StalePlan,
    InvalidPlan
};

/**
 * Game-independent client model used to exercise divergence and repair before
 * wiring the protocol into Skyrim runtime mutations.
 */
class PartyQuestReplica final
{
public:
    [[nodiscard]] static PartyQuestReplica FromCanonical(const PartyQuestState& acCanonicalState);

    /** Replaces one locally observed snapshot without changing world revision. */
    void ObserveLocalSnapshot(QuestSnapshot aSnapshot);
    void SetObservedWorldRevision(uint64_t aWorldRevision) noexcept { m_worldRevision = aWorldRevision; }

    [[nodiscard]] PartyQuestReplicaReport BuildReport() const;
    [[nodiscard]] PartyQuestReplicaApplyStatus Apply(const PartyQuestRepairPlan& acPlan);

    [[nodiscard]] const QuestSnapshot* FindQuest(const GameId& acQuestId) const noexcept;
    [[nodiscard]] uint64_t GetWorldRevision() const noexcept { return m_worldRevision; }
    [[nodiscard]] size_t GetQuestCount() const noexcept { return m_quests.size(); }

private:
    uint64_t m_worldRevision{};
    std::unordered_map<GameId, QuestSnapshot> m_quests;
};
