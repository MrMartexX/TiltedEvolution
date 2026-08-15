#include <Structs/Skyrim/PartyQuestDeferredWorld.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <algorithm>

namespace
{
bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;
    return acLeft.BaseId < acRight.BaseId;
}

void CanonicalizeTargets(std::vector<GameId>& aTargets)
{
    std::sort(aTargets.begin(), aTargets.end(), GameIdLess);
    aTargets.erase(std::unique(aTargets.begin(), aTargets.end()), aTargets.end());
}

void ResetRuntimeReadiness(PartyQuestDeferredWorldEntry& aEntry) noexcept
{
    aEntry.Ready = false;
    aEntry.ReadyGeneration = 0;
}
} // namespace

std::vector<GameId> PartyQuestDeferredWorldQueue::CollectReferenceTargets(
    const QuestSnapshot& acSnapshot)
{
    std::vector<GameId> targets;
    targets.reserve(
        acSnapshot.ReferenceAliases.size() + acSnapshot.CreatedReferences.size());

    for (const QuestReferenceAliasSnapshot& alias : acSnapshot.ReferenceAliases)
    {
        if (alias.ReferenceId)
            targets.push_back(*alias.ReferenceId);
    }

    for (const GameId& reference : acSnapshot.CreatedReferences)
    {
        if (reference)
            targets.push_back(reference);
    }

    CanonicalizeTargets(targets);
    return targets;
}

std::vector<GameId> PartyQuestDeferredWorldQueue::CollectLocationTargets(
    const QuestSnapshot& acSnapshot)
{
    std::vector<GameId> targets;
    targets.reserve(acSnapshot.LocationAliases.size());

    for (const QuestLocationAliasSnapshot& alias : acSnapshot.LocationAliases)
    {
        if (alias.LocationId)
            targets.push_back(*alias.LocationId);
    }

    CanonicalizeTargets(targets);
    return targets;
}

std::vector<GameId> PartyQuestDeferredWorldQueue::CollectWorldTargets(
    const QuestSnapshot& acSnapshot)
{
    auto targets = CollectReferenceTargets(acSnapshot);
    auto locations = CollectLocationTargets(acSnapshot);
    targets.insert(targets.end(), locations.begin(), locations.end());
    CanonicalizeTargets(targets);
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

    if (m_transactionFingerprints.size() >= MaxRememberedTransactions)
        return PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded;

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
        replacement.ReferenceTargets = CollectReferenceTargets(aRequest.CanonicalSnapshot);
        replacement.LocationTargets = CollectLocationTargets(aRequest.CanonicalSnapshot);
        replacement.ReferencedWorldTargets = CollectWorldTargets(aRequest.CanonicalSnapshot);
        replacement.HasSceneDependency = aRequest.CanonicalSnapshot.SceneParticipantPlayerId.has_value();
        replacement.Request = std::move(aRequest);

        const uint64_t transactionId = replacement.Request.TransactionId;
        existingIt->second = std::move(replacement);
        m_transactionQuests.emplace(transactionId, questId);
        m_transactionFingerprints.emplace(transactionId, *fingerprint);
        return PartyQuestDeferredWorldEnqueueStatus::ReplacedOlderQuestRevision;
    }

    if (m_entries.size() >= MaxPendingEntries)
        return PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded;

    PartyQuestDeferredWorldEntry entry;
    entry.ReferenceTargets = CollectReferenceTargets(aRequest.CanonicalSnapshot);
    entry.LocationTargets = CollectLocationTargets(aRequest.CanonicalSnapshot);
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
    PartyQuestRuntimeApplyRequest aCurrentRequest,
    uint64_t aCurrentCanonicalQuestRevision) noexcept
{
    if (!aCurrentRequest.Plan.DryRunOnly)
        return false;

    const uint64_t transactionId = aCurrentRequest.TransactionId;
    if (transactionId == 0 || aCurrentCanonicalQuestRevision == 0)
        return false;

    const auto transactionIt = m_transactionQuests.find(transactionId);
    if (transactionIt == m_transactionQuests.end())
        return false;

    const auto entryIt = m_entries.find(transactionIt->second);
    if (entryIt == m_entries.end() ||
        entryIt->second.Request.TransactionId != transactionId)
    {
        return false;
    }

    const uint64_t queuedRevision =
        entryIt->second.Request.CanonicalSnapshot.Revision;

    if (aCurrentCanonicalQuestRevision > queuedRevision)
    {
        m_transactionQuests.erase(transactionId);
        m_entries.erase(entryIt);
        return false;
    }

    if (aCurrentCanonicalQuestRevision != queuedRevision ||
        aCurrentRequest.CanonicalSnapshot.Revision !=
            aCurrentCanonicalQuestRevision)
    {
        return false;
    }

    const auto currentIdentity =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(aCurrentRequest);
    if (!currentIdentity)
        return false;

    const auto fingerprintIt = m_transactionFingerprints.find(transactionId);
    if (fingerprintIt == m_transactionFingerprints.end() ||
        fingerprintIt->second != *currentIdentity)
    {
        return false;
    }

    aCurrentRequest.CanonicalSnapshot.Canonicalize();
    entryIt->second.Request = std::move(aCurrentRequest);
    entryIt->second.Ready = true;
    entryIt->second.ReadyGeneration = 0;
    return true;
}

PartyQuestDeferredWorldRuntimeReadinessResult
PartyQuestDeferredWorldQueue::EvaluateRuntimeReadiness(
    const PartyQuestDeferredWorldEntry& acEntry,
    PartyQuestRuntimeGenerationFence& aGenerationFence,
    const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
    const PartyQuestDeferredWorldRuntimeReadinessSources& acSources) noexcept
{
    PartyQuestDeferredWorldRuntimeReadinessResult result;

    if (acEntry.Request.Plan.DryRunOnly ||
        acEntry.Request.TransactionId == 0 ||
        acEntry.Request.CanonicalSnapshot.Revision == 0)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::NotRuntimePlan;
        return result;
    }

    if (!acReferenceReadiness.UsesGenerationFence(aGenerationFence))
    {
        result.Status =
            PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceReadinessDomainMismatch;
        return result;
    }

    // No production Skyrim observer with reviewed semantics exists yet for
    // location-alias or scene readiness. Callback-shaped placeholders are not
    // authority: accepting a caller-supplied `return true` would turn an
    // unreviewed heuristic into mutation authorization. Until dedicated
    // capability-backed observers exist, these dependency classes are
    // deliberately unsupported and fail closed.
    if (!acEntry.LocationTargets.empty())
    {
        result.Status =
            PartyQuestDeferredWorldRuntimeReadinessStatus::LocationReadinessUnavailable;
        return result;
    }

    if (acEntry.Request.CanonicalSnapshot.SceneParticipantPlayerId)
    {
        result.Status =
            PartyQuestDeferredWorldRuntimeReadinessStatus::SceneReadinessUnavailable;
        return result;
    }

    const uint64_t expectedGeneration = aGenerationFence.GetGeneration();
    if (expectedGeneration == 0)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
        return result;
    }

    std::vector<uint32_t> localReferenceIds;
    localReferenceIds.reserve(acEntry.ReferenceTargets.size());

    try
    {
        // Resolve the canonical -> local mapping while the exact load-order
        // generation is pinned. ModSystem rebuilds take the exclusive side of
        // this same fence.
        auto generationLease = aGenerationFence.TryAcquire(expectedGeneration);
        if (!generationLease || !generationLease->IsValid())
        {
            result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
            return result;
        }

        if (!acEntry.ReferenceTargets.empty())
        {
            if (!acSources.ResolveReferenceFormId)
            {
                result.Status =
                    PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceMappingUnavailable;
                return result;
            }

            for (const GameId& target : acEntry.ReferenceTargets)
            {
                const uint32_t localFormId = acSources.ResolveReferenceFormId(target);
                if (localFormId == 0)
                {
                    result.Status =
                        PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceMappingUnavailable;
                    return result;
                }
                localReferenceIds.push_back(localFormId);
            }
        }
    }
    catch (...)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::ObserverFailure;
        return result;
    }

    if (aGenerationFence.GetGeneration() != expectedGeneration)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
        return result;
    }

    if (!localReferenceIds.empty())
    {
        if (!acReferenceReadiness.AreLoaded(localReferenceIds, expectedGeneration))
        {
            result.Status = aGenerationFence.GetGeneration() == expectedGeneration
                ? PartyQuestDeferredWorldRuntimeReadinessStatus::ReferenceNotReady
                : PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
            return result;
        }
    }

    if (aGenerationFence.GetGeneration() != expectedGeneration)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
        return result;
    }

    result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::Ready;
    result.RuntimeGeneration = expectedGeneration;
    return result;
}

PartyQuestDeferredWorldRuntimeReadinessResult
PartyQuestDeferredWorldQueue::TryMarkRuntimeReady(
    PartyQuestRuntimeApplyRequest aCurrentRequest,
    uint64_t aCurrentCanonicalQuestRevision,
    PartyQuestRuntimeGenerationFence& aGenerationFence,
    const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
    const PartyQuestDeferredWorldRuntimeReadinessSources& acSources) noexcept
{
    PartyQuestDeferredWorldRuntimeReadinessResult result;

    if (aCurrentRequest.Plan.DryRunOnly)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::NotRuntimePlan;
        return result;
    }

    const uint64_t transactionId = aCurrentRequest.TransactionId;
    if (transactionId == 0 || aCurrentCanonicalQuestRevision == 0)
        return result;

    const auto transactionIt = m_transactionQuests.find(transactionId);
    if (transactionIt == m_transactionQuests.end())
        return result;

    const auto entryIt = m_entries.find(transactionIt->second);
    if (entryIt == m_entries.end() ||
        entryIt->second.Request.TransactionId != transactionId)
    {
        return result;
    }

    const uint64_t queuedRevision =
        entryIt->second.Request.CanonicalSnapshot.Revision;

    if (aCurrentCanonicalQuestRevision > queuedRevision)
    {
        m_transactionQuests.erase(transactionId);
        m_entries.erase(entryIt);
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::StaleRevision;
        return result;
    }

    if (aCurrentCanonicalQuestRevision != queuedRevision ||
        aCurrentRequest.CanonicalSnapshot.Revision != aCurrentCanonicalQuestRevision)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::StaleRevision;
        return result;
    }

    const auto currentIdentity =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(aCurrentRequest);
    const auto fingerprintIt = m_transactionFingerprints.find(transactionId);
    if (!currentIdentity ||
        fingerprintIt == m_transactionFingerprints.end() ||
        fingerprintIt->second != *currentIdentity)
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::IdentityMismatch;
        return result;
    }

    result = EvaluateRuntimeReadiness(
        entryIt->second,
        aGenerationFence,
        acReferenceReadiness,
        acSources);
    if (!result.IsReady())
        return result;

    auto finalLease = aGenerationFence.TryAcquire(result.RuntimeGeneration);
    if (!finalLease || !finalLease->IsValid())
    {
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
        result.RuntimeGeneration = 0;
        return result;
    }

    aCurrentRequest.CanonicalSnapshot.Canonicalize();
    entryIt->second.Request = std::move(aCurrentRequest);
    entryIt->second.Ready = true;
    entryIt->second.ReadyGeneration = result.RuntimeGeneration;
    return result;
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
        if (!it->second.Ready || !it->second.Request.Plan.DryRunOnly)
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

std::vector<PartyQuestRuntimeApplyRequest> PartyQuestDeferredWorldQueue::TakeRuntimeReady(
    PartyQuestRuntimeGenerationFence& aGenerationFence,
    const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
    const PartyQuestDeferredWorldRuntimeReadinessSources& acSources,
    const CanonicalRevisionObserver& acCanonicalRevisionObserver) noexcept
{
    std::vector<PartyQuestRuntimeApplyRequest> ready;
    if (!acCanonicalRevisionObserver)
        return ready;

    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (!it->second.Ready || it->second.Request.Plan.DryRunOnly)
        {
            ++it;
            continue;
        }

        uint64_t currentRevision = 0;
        try
        {
            currentRevision = acCanonicalRevisionObserver(
                it->second.Request.CanonicalSnapshot.QuestId);
        }
        catch (...)
        {
            ResetRuntimeReadiness(it->second);
            ++it;
            continue;
        }

        const uint64_t queuedRevision =
            it->second.Request.CanonicalSnapshot.Revision;
        if (currentRevision > queuedRevision)
        {
            m_transactionQuests.erase(it->second.Request.TransactionId);
            it = m_entries.erase(it);
            continue;
        }

        if (currentRevision == 0 || currentRevision != queuedRevision)
        {
            ResetRuntimeReadiness(it->second);
            ++it;
            continue;
        }

        const auto readiness = EvaluateRuntimeReadiness(
            it->second,
            aGenerationFence,
            acReferenceReadiness,
            acSources);
        if (!readiness.IsReady() ||
            readiness.RuntimeGeneration != it->second.ReadyGeneration)
        {
            ResetRuntimeReadiness(it->second);
            ++it;
            continue;
        }

        auto finalLease = aGenerationFence.TryAcquire(readiness.RuntimeGeneration);
        if (!finalLease || !finalLease->IsValid())
        {
            ResetRuntimeReadiness(it->second);
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
