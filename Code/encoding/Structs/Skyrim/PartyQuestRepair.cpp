#include <Structs/Skyrim/PartyQuestRepair.h>
#include <Structs/Skyrim/PartyQuestAdmission.h>

#include <algorithm>
#include <unordered_set>

namespace
{
bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;

    return acLeft.BaseId < acRight.BaseId;
}
} // namespace

PartyQuestRepairPlan PartyQuestRepairPlanner::Build(
    const PartyQuestState& acCanonicalState,
    const PartyQuestReplicaReport& acClientReport)
{
    PartyQuestRepairPlan plan;
    plan.BaseClientWorldRevision = acClientReport.WorldRevision;
    plan.TargetWorldRevision = acCanonicalState.GetWorldRevision();

    if (acClientReport.WorldRevision > acCanonicalState.GetWorldRevision())
    {
        plan.Status = PartyQuestRepairPlanStatus::ClientAhead;
        return plan;
    }

    std::vector<const QuestSnapshot*> orderedSnapshots;
    orderedSnapshots.reserve(acCanonicalState.GetQuestCount());
    for (const auto& [questId, snapshot] : acCanonicalState.GetQuests())
    {
        if (PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(questId))
            continue;

        orderedSnapshots.push_back(&snapshot);
    }

    std::sort(orderedSnapshots.begin(), orderedSnapshots.end(), [](const QuestSnapshot* apLeft, const QuestSnapshot* apRight)
    {
        return GameIdLess(apLeft->QuestId, apRight->QuestId);
    });

    for (const QuestSnapshot* pCanonicalSnapshot : orderedSnapshots)
    {
        const auto clientIt = acClientReport.Quests.find(pCanonicalSnapshot->QuestId);

        PartyQuestRepairReason reason{};
        bool requiresRepair = false;

        if (clientIt == acClientReport.Quests.end())
        {
            reason = PartyQuestRepairReason::MissingQuest;
            requiresRepair = true;
        }
        else if (clientIt->second.QuestRevision != pCanonicalSnapshot->Revision)
        {
            reason = PartyQuestRepairReason::RevisionMismatch;
            requiresRepair = true;
        }
        else if (clientIt->second.Digest != pCanonicalSnapshot->ComputeDigest())
        {
            reason = PartyQuestRepairReason::DigestMismatch;
            requiresRepair = true;
        }

        if (requiresRepair)
            plan.Items.push_back({reason, *pCanonicalSnapshot});
    }

    for (const auto& [questId, entry] : acClientReport.Quests)
    {
        (void)entry;
        if (PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(questId))
            plan.RemovedQuestIds.push_back(questId);
    }
    std::sort(plan.RemovedQuestIds.begin(), plan.RemovedQuestIds.end(), GameIdLess);

    if (!plan.Items.empty() || !plan.RemovedQuestIds.empty() ||
        acClientReport.WorldRevision != acCanonicalState.GetWorldRevision())
    {
        plan.Status = PartyQuestRepairPlanStatus::RepairRequired;
    }
    else
    {
        plan.Status = PartyQuestRepairPlanStatus::UpToDate;
    }

    return plan;
}

PartyQuestRepairSummary PartyQuestRepairPlanner::Summarize(
    const PartyQuestState& acCanonicalState,
    const PartyQuestReplicaReport& acClientReport,
    const PartyQuestRepairPlan& acPlan)
{
    PartyQuestRepairSummary summary;

    for (const PartyQuestRepairItem& item : acPlan.Items)
    {
        switch (item.Reason)
        {
        case PartyQuestRepairReason::MissingQuest:
            ++summary.MissingQuestCount;
            break;
        case PartyQuestRepairReason::RevisionMismatch:
            ++summary.RevisionMismatchCount;
            break;
        case PartyQuestRepairReason::DigestMismatch:
            ++summary.DigestMismatchCount;
            break;
        }
    }

    summary.QuarantinedQuestRemovalCount = acPlan.RemovedQuestIds.size();

    for (const auto& [questId, entry] : acClientReport.Quests)
    {
        (void)entry;
        if (PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(questId))
            continue;

        if (!acCanonicalState.FindQuest(questId))
            ++summary.ClientOnlyQuestCount;
    }

    return summary;
}

PartyQuestReplica PartyQuestReplica::FromCanonical(const PartyQuestState& acCanonicalState)
{
    PartyQuestReplica replica;
    replica.m_worldRevision = acCanonicalState.GetWorldRevision();
    for (const auto& [questId, snapshot] : acCanonicalState.GetQuests())
    {
        if (!PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(questId))
            replica.m_quests.emplace(questId, snapshot);
    }
    return replica;
}

void PartyQuestReplica::ObserveLocalSnapshot(QuestSnapshot aSnapshot)
{
    aSnapshot.Canonicalize();
    m_quests[aSnapshot.QuestId] = std::move(aSnapshot);
}

PartyQuestReplicaReport PartyQuestReplica::BuildReport() const
{
    PartyQuestReplicaReport report;
    report.WorldRevision = m_worldRevision;
    report.Quests.reserve(m_quests.size());

    for (const auto& [questId, snapshot] : m_quests)
        report.Quests.emplace(questId, PartyQuestReplicaEntry{snapshot.Revision, snapshot.ComputeDigest()});

    return report;
}

PartyQuestReplicaApplyStatus PartyQuestReplica::Apply(const PartyQuestRepairPlan& acPlan)
{
    if (acPlan.Status == PartyQuestRepairPlanStatus::ClientAhead)
        return PartyQuestReplicaApplyStatus::ClientAhead;

    if (acPlan.BaseClientWorldRevision != m_worldRevision)
        return PartyQuestReplicaApplyStatus::StalePlan;

    if (acPlan.TargetWorldRevision < acPlan.BaseClientWorldRevision)
        return PartyQuestReplicaApplyStatus::InvalidPlan;

    std::unordered_set<GameId> seenQuestIds;
    seenQuestIds.reserve(acPlan.Items.size() + acPlan.RemovedQuestIds.size());

    for (const auto& item : acPlan.Items)
    {
        if (!item.CanonicalSnapshot.QuestId || item.CanonicalSnapshot.Revision == 0)
            return PartyQuestReplicaApplyStatus::InvalidPlan;

        if (!seenQuestIds.emplace(item.CanonicalSnapshot.QuestId).second)
            return PartyQuestReplicaApplyStatus::InvalidPlan;
    }

    for (const GameId& questId : acPlan.RemovedQuestIds)
    {
        if (!questId || !PartyQuestAdmissionPolicy::IsConfirmedServiceQuest(questId) ||
            !seenQuestIds.emplace(questId).second)
        {
            return PartyQuestReplicaApplyStatus::InvalidPlan;
        }
    }

    if (acPlan.Status == PartyQuestRepairPlanStatus::UpToDate)
    {
        if (!acPlan.Items.empty() || !acPlan.RemovedQuestIds.empty() ||
            acPlan.TargetWorldRevision != m_worldRevision)
        {
            return PartyQuestReplicaApplyStatus::InvalidPlan;
        }

        return PartyQuestReplicaApplyStatus::NoChanges;
    }

    for (const GameId& questId : acPlan.RemovedQuestIds)
        m_quests.erase(questId);

    for (auto item : acPlan.Items)
    {
        item.CanonicalSnapshot.Canonicalize();
        m_quests[item.CanonicalSnapshot.QuestId] = std::move(item.CanonicalSnapshot);
    }

    const bool changed = !acPlan.Items.empty() || !acPlan.RemovedQuestIds.empty() ||
        m_worldRevision != acPlan.TargetWorldRevision;
    m_worldRevision = acPlan.TargetWorldRevision;
    return changed ? PartyQuestReplicaApplyStatus::Applied : PartyQuestReplicaApplyStatus::NoChanges;
}

const QuestSnapshot* PartyQuestReplica::FindQuest(const GameId& acQuestId) const noexcept
{
    const auto it = m_quests.find(acQuestId);
    return it != m_quests.end() ? &it->second : nullptr;
}
