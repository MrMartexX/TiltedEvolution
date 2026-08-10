#include <Structs/Skyrim/PartyQuestDeferredWorld.h>

#include <algorithm>

namespace
{
bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;
    return acLeft.BaseId < acRight.BaseId;
}
} // namespace

std::vector<GameId> PartyQuestDeferredWorldQueue::CollectWorldTargets(
    const QuestSnapshot& acSnapshot)
{
    std::vector<GameId> targets;
    targets.reserve(
        acSnapshot.ReferenceAliases.size() +
        acSnapshot.LocationAliases.size() +
        acSnapshot.CreatedReferences.size());

    for (const QuestReferenceAliasSnapshot& alias : acSnapshot.ReferenceAliases)
    {
        if (alias.ReferenceId)
            targets.push_back(*alias.ReferenceId);
    }

    for (const QuestLocationAliasSnapshot& alias : acSnapshot.LocationAliases)
    {
        if (alias.LocationId)
            targets.push_back(*alias.LocationId);
    }

    for (const GameId& reference : acSnapshot.CreatedReferences)
    {
        if (reference)
            targets.push_back(reference);
    }

    std::sort(targets.begin(), targets.end(), GameIdLess);
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    return targets;
}

PartyQuestDeferredWorldEnqueueStatus PartyQuestDeferredWorldQueue::Enqueue(
    PartyQuestRuntimeApplyRequest aRequest)
{
    if (aRequest.TransactionId == 0 ||
        aRequest.TargetWorldRevision == 0 ||
        aRequest.SidecarManifestFingerprint == 0 ||
        !aRequest.CanonicalSnapshot.QuestId ||
        aRequest.CanonicalSnapshot.Revision == 0)
    {
        return PartyQuestDeferredWorldEnqueueStatus::InvalidRequest;
    }

    if (aRequest.Plan.Safety.Status != PartyQuestRuntimeSafetyStatus::RuntimeSafe ||
        !HasPartyQuestApplyAction(aRequest.Plan.Actions, PartyQuestApplyAction::WaitForWorldTargets))
    {
        return PartyQuestDeferredWorldEnqueueStatus::NotDeferred;
    }

    if (!aRequest.Plan.MutationAuthorization.Matches(
            aRequest.CanonicalSnapshot,
            aRequest.Plan.Actions,
            aRequest.Plan.DryRunOnly))
    {
        return PartyQuestDeferredWorldEnqueueStatus::UnsafePlan;
    }

    aRequest.CanonicalSnapshot.Canonicalize();
    const auto fingerprint =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(aRequest);
    if (!fingerprint)
        return PartyQuestDeferredWorldEnqueueStatus::UnsafePlan;

    const auto seenIt = m_transactionFingerprints.find(aRequest.TransactionId);
    if (seenIt != m_transactionFingerprints.end())
    {
        return seenIt->second == *fingerprint
            ? PartyQuestDeferredWorldEnqueueStatus::Duplicate
            : PartyQuestDeferredWorldEnqueueStatus::TransactionConflict;
    }

    const GameId questId = aRequest.CanonicalSnapshot.QuestId;
    const auto existingIt = m_entries.find(questId);
    if (existingIt != m_entries.end())
    {
        const uint64_t existingRevision = existingIt->second.Request.CanonicalSnapshot.Revision;
        const uint64_t incomingRevision = aRequest.CanonicalSnapshot.Revision;

        if (incomingRevision < existingRevision)
            return PartyQuestDeferredWorldEnqueueStatus::Stale;

        if (incomingRevision == existingRevision)
            return PartyQuestDeferredWorldEnqueueStatus::TransactionConflict;

        m_transactionQuests.erase(existingIt->second.Request.TransactionId);

        PartyQuestDeferredWorldEntry replacement;
        replacement.ReferencedWorldTargets = CollectWorldTargets(aRequest.CanonicalSnapshot);
        replacement.HasSceneDependency = aRequest.CanonicalSnapshot.SceneParticipantPlayerId.has_value();
        replacement.Request = std::move(aRequest);

        const uint64_t transactionId = replacement.Request.TransactionId;
        existingIt->second = std::move(replacement);
        m_transactionQuests.emplace(transactionId, questId);
        m_transactionFingerprints.emplace(transactionId, *fingerprint);
        return PartyQuestDeferredWorldEnqueueStatus::ReplacedOlderQuestRevision;
    }

    PartyQuestDeferredWorldEntry entry;
    entry.ReferencedWorldTargets = CollectWorldTargets(aRequest.CanonicalSnapshot);
    entry.HasSceneDependency = aRequest.CanonicalSnapshot.SceneParticipantPlayerId.has_value();
    entry.Request = std::move(aRequest);

    const uint64_t transactionId = entry.Request.TransactionId;
    m_entries.emplace(questId, std::move(entry));
    m_transactionQuests.emplace(transactionId, questId);
    m_transactionFingerprints.emplace(transactionId, *fingerprint);
    return PartyQuestDeferredWorldEnqueueStatus::Queued;
}

bool PartyQuestDeferredWorldQueue::MarkReady(
    PartyQuestRuntimeApplyRequest aCurrentRequest) noexcept
{
    const uint64_t transactionId = aCurrentRequest.TransactionId;
    const auto currentIdentity =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(aCurrentRequest);
    if (!currentIdentity)
        return false;

    const auto transactionIt = m_transactionQuests.find(transactionId);
    if (transactionIt == m_transactionQuests.end())
        return false;

    const auto entryIt = m_entries.find(transactionIt->second);
    const auto fingerprintIt = m_transactionFingerprints.find(transactionId);
    if (entryIt == m_entries.end() ||
        fingerprintIt == m_transactionFingerprints.end() ||
        entryIt->second.Request.TransactionId != transactionId ||
        fingerprintIt->second != *currentIdentity)
    {
        return false;
    }

    aCurrentRequest.CanonicalSnapshot.Canonicalize();
    entryIt->second.Request = std::move(aCurrentRequest);
    entryIt->second.Ready = true;
    return true;
}

bool PartyQuestDeferredWorldQueue::InvalidateIfOlder(
    const GameId& acQuestId,
    uint64_t aCanonicalQuestRevision) noexcept
{
    const auto it = m_entries.find(acQuestId);
    if (it == m_entries.end() ||
        it->second.Request.CanonicalSnapshot.Revision >= aCanonicalQuestRevision)
    {
        return false;
    }

    m_transactionQuests.erase(it->second.Request.TransactionId);
    m_entries.erase(it);
    return true;
}

std::vector<PartyQuestRuntimeApplyRequest> PartyQuestDeferredWorldQueue::TakeReady()
{
    std::vector<PartyQuestRuntimeApplyRequest> ready;
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (!it->second.Ready)
        {
            ++it;
            continue;
        }

        m_transactionQuests.erase(it->second.Request.TransactionId);
        ready.push_back(std::move(it->second.Request));
        it = m_entries.erase(it);
    }

    std::sort(ready.begin(), ready.end(), [](const auto& acLeft, const auto& acRight)
    {
        if (acLeft.TargetWorldRevision != acRight.TargetWorldRevision)
            return acLeft.TargetWorldRevision < acRight.TargetWorldRevision;
        return acLeft.TransactionId < acRight.TransactionId;
    });
    return ready;
}

const PartyQuestDeferredWorldEntry* PartyQuestDeferredWorldQueue::FindByQuest(
    const GameId& acQuestId) const noexcept
{
    const auto it = m_entries.find(acQuestId);
    return it != m_entries.end() ? &it->second : nullptr;
}

const PartyQuestDeferredWorldEntry* PartyQuestDeferredWorldQueue::FindByTransaction(
    uint64_t aTransactionId) const noexcept
{
    const auto transactionIt = m_transactionQuests.find(aTransactionId);
    if (transactionIt == m_transactionQuests.end())
        return nullptr;
    return FindByQuest(transactionIt->second);
}
