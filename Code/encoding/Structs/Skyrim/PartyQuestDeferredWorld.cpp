#include <Structs/Skyrim/PartyQuestDeferredWorld.h>

#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

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
    if (!aRequest.Plan.DryRunOnly)
        return PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerRequired;

    return EnqueueBound(std::move(aRequest), nullptr, nullptr, 0);
}

PartyQuestDeferredWorldEnqueueStatus PartyQuestDeferredWorldQueue::EnqueueRuntime(
    PartyQuestRuntimeApplyRequest aRequest,
    const PartyQuestRuntimeGuardedSession& acGuardedSession,
    PartyQuestRuntimeGenerationFence& aGenerationFence) noexcept
{
    if (aRequest.Plan.DryRunOnly)
        return PartyQuestDeferredWorldEnqueueStatus::NotDeferred;

    const auto& session = acGuardedSession.GetRuntimeSession();
    const auto& campaignId = session.GetCampaignId();
    const auto& playerProfileId = session.GetPlayerProfileId();
    if (!campaignId.IsValid() || !playerProfileId.IsValid())
        return PartyQuestDeferredWorldEnqueueStatus::RuntimeOwnerMismatch;

    const uint64_t generation = aGenerationFence.GetGeneration();
    auto lease = aGenerationFence.TryAcquire(generation);
    if (generation == 0 || !lease || !lease->IsValid())
        return PartyQuestDeferredWorldEnqueueStatus::RuntimeGenerationChanged;

    try
    {
        return EnqueueBound(
            std::move(aRequest), &campaignId, &playerProfileId, generation);
    }
    catch (...)
    {
        return PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded;
    }
}

PartyQuestDeferredWorldEnqueueStatus PartyQuestDeferredWorldQueue::EnqueueBound(
    PartyQuestRuntimeApplyRequest aRequest,
    const PartyQuestCampaignId* apCampaignId,
    const PartyQuestPlayerProfileId* apPlayerProfileId,
    uint64_t aEnqueueGeneration)
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

    // Build the complete candidate off to the side. Allocation failure must
    // not publish only one of the correlated indexes.
    auto candidateEntries = m_entries;
    auto candidateTransactionQuests = m_transactionQuests;
    auto candidateTransactionFingerprints = m_transactionFingerprints;

    const GameId questId = aRequest.CanonicalSnapshot.QuestId;
    const auto existingIt = candidateEntries.find(questId);
    if (existingIt != candidateEntries.end())
    {
        const uint64_t existingRevision = existingIt->second.Request.CanonicalSnapshot.Revision;
        const uint64_t incomingRevision = aRequest.CanonicalSnapshot.Revision;

        if (incomingRevision < existingRevision)
            return PartyQuestDeferredWorldEnqueueStatus::Stale;

        if (incomingRevision == existingRevision)
            return PartyQuestDeferredWorldEnqueueStatus::TransactionConflict;

        candidateTransactionQuests.erase(existingIt->second.Request.TransactionId);

        PartyQuestDeferredWorldEntry replacement;
        replacement.ReferenceTargets = CollectReferenceTargets(aRequest.CanonicalSnapshot);
        replacement.LocationTargets = CollectLocationTargets(aRequest.CanonicalSnapshot);
        replacement.ReferencedWorldTargets = CollectWorldTargets(aRequest.CanonicalSnapshot);
        replacement.HasSceneDependency = aRequest.CanonicalSnapshot.SceneParticipantPlayerId.has_value();
        if (apCampaignId && apPlayerProfileId)
        {
            replacement.CampaignId = *apCampaignId;
            replacement.PlayerProfileId = *apPlayerProfileId;
            replacement.EnqueueGeneration = aEnqueueGeneration;
        }
        replacement.Request = std::move(aRequest);

        const uint64_t transactionId = replacement.Request.TransactionId;
        existingIt->second = std::move(replacement);
        candidateTransactionQuests.emplace(transactionId, questId);
        candidateTransactionFingerprints.emplace(transactionId, *fingerprint);
        m_entries.swap(candidateEntries);
        m_transactionQuests.swap(candidateTransactionQuests);
        m_transactionFingerprints.swap(candidateTransactionFingerprints);
        return PartyQuestDeferredWorldEnqueueStatus::ReplacedOlderQuestRevision;
    }

    if (m_entries.size() >= MaxPendingEntries)
        return PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded;

    PartyQuestDeferredWorldEntry entry;
    entry.ReferenceTargets = CollectReferenceTargets(aRequest.CanonicalSnapshot);
    entry.LocationTargets = CollectLocationTargets(aRequest.CanonicalSnapshot);
    entry.ReferencedWorldTargets = CollectWorldTargets(aRequest.CanonicalSnapshot);
    entry.HasSceneDependency = aRequest.CanonicalSnapshot.SceneParticipantPlayerId.has_value();
    if (apCampaignId && apPlayerProfileId)
    {
        entry.CampaignId = *apCampaignId;
        entry.PlayerProfileId = *apPlayerProfileId;
        entry.EnqueueGeneration = aEnqueueGeneration;
    }
    entry.Request = std::move(aRequest);

    const uint64_t transactionId = entry.Request.TransactionId;
    candidateEntries.emplace(questId, std::move(entry));
    candidateTransactionQuests.emplace(transactionId, questId);
    candidateTransactionFingerprints.emplace(transactionId, *fingerprint);
    m_entries.swap(candidateEntries);
    m_transactionQuests.swap(candidateTransactionQuests);
    m_transactionFingerprints.swap(candidateTransactionFingerprints);
    return PartyQuestDeferredWorldEnqueueStatus::Queued;
}

bool PartyQuestDeferredWorldQueue::MatchesRuntimeOwner(
    const PartyQuestDeferredWorldEntry& acEntry,
    const PartyQuestRuntimeGuardedSession& acGuardedSession,
    uint64_t aGeneration) noexcept
{
    const auto& session = acGuardedSession.GetRuntimeSession();
    return acEntry.CampaignId.has_value() &&
        acEntry.PlayerProfileId.has_value() &&
        acEntry.EnqueueGeneration != 0 &&
        acEntry.EnqueueGeneration == aGeneration &&
        *acEntry.CampaignId == session.GetCampaignId() &&
        *acEntry.PlayerProfileId == session.GetPlayerProfileId();
}

void PartyQuestDeferredWorldQueue::Retire(
    std::unordered_map<GameId, PartyQuestDeferredWorldEntry>::iterator aIt) noexcept
{
    m_transactionQuests.erase(aIt->second.Request.TransactionId);
    m_entries.erase(aIt);
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
    const PartyQuestRuntimeGuardedSession& acGuardedSession,
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

    const uint64_t currentGeneration = aGenerationFence.GetGeneration();
    if (entryIt->second.EnqueueGeneration != currentGeneration)
    {
        Retire(entryIt);
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::RuntimeGenerationChanged;
        return result;
    }

    if (!MatchesRuntimeOwner(
            entryIt->second, acGuardedSession, currentGeneration))
    {
        Retire(entryIt);
        result.Status = PartyQuestDeferredWorldRuntimeReadinessStatus::IdentityMismatch;
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

size_t PartyQuestDeferredWorldQueue::ConsumeRuntimeReady(
    PartyQuestRuntimeGuardedSession& aGuardedSession,
    const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
    const PartyQuestDeferredWorldRuntimeReadinessSources& acSources,
    const CanonicalRevisionObserver& acCanonicalRevisionObserver) noexcept
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    if (!processOwner.IsBound() ||
        processOwner.GetGuardedSession() != &aGuardedSession ||
        !acCanonicalRevisionObserver ||
        !acReferenceReadiness.UsesGenerationFence(generationFence) ||
        &aGuardedSession.GetSaveGuard() != &processGuard)
    {
        return 0;
    }

    std::vector<GameId> orderedQuestIds;
    orderedQuestIds.reserve(m_entries.size());
    for (const auto& [questId, entry] : m_entries)
    {
        if (entry.Ready && !entry.Request.Plan.DryRunOnly)
            orderedQuestIds.push_back(questId);
    }

    std::sort(orderedQuestIds.begin(), orderedQuestIds.end(), [this](
        const GameId& acLeft,
        const GameId& acRight)
    {
        const auto leftIt = m_entries.find(acLeft);
        const auto rightIt = m_entries.find(acRight);
        if (leftIt == m_entries.end() || rightIt == m_entries.end())
            return GameIdLess(acLeft, acRight);

        const auto& left = leftIt->second.Request;
        const auto& right = rightIt->second.Request;
        if (left.TargetWorldRevision != right.TargetWorldRevision)
            return left.TargetWorldRevision < right.TargetWorldRevision;
        return left.TransactionId < right.TransactionId;
    });

    size_t consumed = 0;
    for (const GameId& questId : orderedQuestIds)
    {
        auto entryIt = m_entries.find(questId);
        if (entryIt == m_entries.end() ||
            !entryIt->second.Ready ||
            entryIt->second.Request.Plan.DryRunOnly)
        {
            continue;
        }

        const uint64_t ownerGeneration = generationFence.GetGeneration();
        if (!MatchesRuntimeOwner(
                entryIt->second, aGuardedSession, ownerGeneration))
        {
            Retire(entryIt);
            continue;
        }

        uint64_t currentRevision = 0;
        try
        {
            currentRevision = acCanonicalRevisionObserver(questId);
        }
        catch (...)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        const uint64_t queuedRevision =
            entryIt->second.Request.CanonicalSnapshot.Revision;
        if (currentRevision > queuedRevision)
        {
            m_transactionQuests.erase(entryIt->second.Request.TransactionId);
            m_entries.erase(entryIt);
            continue;
        }

        if (currentRevision == 0 || currentRevision != queuedRevision)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        const auto currentIdentity =
            PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(
                entryIt->second.Request);
        const auto fingerprintIt = m_transactionFingerprints.find(
            entryIt->second.Request.TransactionId);
        if (!currentIdentity ||
            fingerprintIt == m_transactionFingerprints.end() ||
            fingerprintIt->second != *currentIdentity)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        const auto readiness = EvaluateRuntimeReadiness(
            entryIt->second,
            generationFence,
            acReferenceReadiness,
            acSources);
        if (!readiness.IsReady() ||
            readiness.RuntimeGeneration != entryIt->second.ReadyGeneration)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        auto finalLease = generationFence.TryAcquire(readiness.RuntimeGeneration);
        if (!finalLease || !finalLease->IsValid())
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        if (!MatchesRuntimeOwner(
                entryIt->second,
                aGuardedSession,
                finalLease->GetGeneration()))
        {
            Retire(entryIt);
            continue;
        }

        uint64_t pinnedRevision = 0;
        try
        {
            pinnedRevision = acCanonicalRevisionObserver(questId);
        }
        catch (...)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        if (pinnedRevision > queuedRevision)
        {
            m_transactionQuests.erase(entryIt->second.Request.TransactionId);
            m_entries.erase(entryIt);
            continue;
        }

        if (pinnedRevision == 0 || pinnedRevision != queuedRevision)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        const auto transition =
            aGuardedSession.MarkWorldReadyPinned(entryIt->second.Request);
        if (transition.Status != PartyQuestRuntimeGuardStatus::Ready ||
            transition.TransitionStatus !=
                PartyQuestRuntimeDurableTransitionStatus::Applied)
        {
            ResetRuntimeReadiness(entryIt->second);
            continue;
        }

        m_transactionQuests.erase(entryIt->second.Request.TransactionId);
        m_entries.erase(entryIt);
        ++consumed;
    }

    return consumed;
}

std::vector<PartyQuestRuntimeApplyRequest> PartyQuestDeferredWorldQueue::TakeRuntimeReady(
    const PartyQuestRuntimeGuardedSession& acGuardedSession,
    PartyQuestRuntimeGenerationFence& aGenerationFence,
    const PartyQuestRuntimeReferenceReadiness& acReferenceReadiness,
    const PartyQuestDeferredWorldRuntimeReadinessSources& acSources,
    const CanonicalRevisionObserver& acCanonicalRevisionObserver) noexcept
{
    std::vector<PartyQuestRuntimeApplyRequest> ready;
    if (!acCanonicalRevisionObserver ||
        &aGenerationFence == &PartyQuestRuntimeGenerationFence::GetProcessFence())
    {
        return ready;
    }

    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (!it->second.Ready || it->second.Request.Plan.DryRunOnly)
        {
            ++it;
            continue;
        }

        const uint64_t ownerGeneration = aGenerationFence.GetGeneration();
        if (!MatchesRuntimeOwner(
                it->second, acGuardedSession, ownerGeneration))
        {
            auto stale = it++;
            Retire(stale);
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

        if (!MatchesRuntimeOwner(
                it->second,
                acGuardedSession,
                finalLease->GetGeneration()))
        {
            auto stale = it++;
            Retire(stale);
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
