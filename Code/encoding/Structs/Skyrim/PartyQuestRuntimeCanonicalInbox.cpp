#include <Structs/Skyrim/PartyQuestRuntimeCanonicalInbox.h>

#include <Structs/Skyrim/PartyQuestRepair.h>

#include <utility>

namespace
{
bool MatchesPublishedReplicaHead(
    const PartyQuestRuntimeCanonicalCandidate& acCandidate,
    const PartyQuestReplica& acPublishedReplica) noexcept
{
    if (acPublishedReplica.GetWorldRevision() != acCandidate.WorldRevision)
        return false;

    const QuestSnapshot* pPublished =
        acPublishedReplica.FindQuest(acCandidate.CanonicalSnapshot.QuestId);
    return pPublished && *pPublished == acCandidate.CanonicalSnapshot;
}
} // namespace

bool PartyQuestRuntimeCanonicalInbox::BindCampaign(
    const PartyQuestCampaignId& acCampaignId)
{
    if (!acCampaignId.IsValid())
        return false;

    if (m_campaignId == acCampaignId)
        return true;

    m_pendingByQuest.clear();
    m_transactionIdentities.clear();
    m_campaignId = acCampaignId;
    return true;
}

void PartyQuestRuntimeCanonicalInbox::Reset() noexcept
{
    m_pendingByQuest.clear();
    m_transactionIdentities.clear();
    m_campaignId = {};
}

PartyQuestRuntimeCanonicalObserveStatus PartyQuestRuntimeCanonicalInbox::Observe(
    PartyQuestRuntimeCanonicalCandidate aCandidate,
    const PartyQuestReplica& acPublishedReplica)
{
    aCandidate.CanonicalSnapshot.Canonicalize();

    if (!m_campaignId.IsValid() ||
        !aCandidate.CampaignId.IsValid() ||
        aCandidate.TransactionId == 0 ||
        aCandidate.WorldRevision == 0 ||
        !aCandidate.CanonicalSnapshot.QuestId ||
        aCandidate.CanonicalSnapshot.Revision == 0)
    {
        return PartyQuestRuntimeCanonicalObserveStatus::InvalidInput;
    }

    if (aCandidate.CampaignId != m_campaignId)
        return PartyQuestRuntimeCanonicalObserveStatus::CampaignMismatch;

    TransactionIdentity identity;
    identity.CampaignId = aCandidate.CampaignId;
    identity.WorldRevision = aCandidate.WorldRevision;
    identity.QuestId = aCandidate.CanonicalSnapshot.QuestId;
    identity.QuestRevision = aCandidate.CanonicalSnapshot.Revision;
    identity.CanonicalDigest = aCandidate.CanonicalSnapshot.ComputeDigest();

    const auto transactionIt = m_transactionIdentities.find(
        aCandidate.TransactionId);
    if (transactionIt != m_transactionIdentities.end() &&
        transactionIt->second != identity)
    {
        return PartyQuestRuntimeCanonicalObserveStatus::TransactionConflict;
    }

    // Protocol-level duplicate recognition intentionally survives normal
    // retransmission. Runtime evidence has the stronger requirement that the
    // exact canonical packet must still be the currently published replica
    // head. A repair or later canonical update may make an otherwise exact
    // cached protocol Duplicate stale for runtime planning.
    if (!MatchesPublishedReplicaHead(aCandidate, acPublishedReplica))
        return PartyQuestRuntimeCanonicalObserveStatus::ReplicaHeadMismatch;

    if (transactionIt != m_transactionIdentities.end())
        return PartyQuestRuntimeCanonicalObserveStatus::Duplicate;

    if (m_transactionIdentities.size() >= MaxRememberedTransactions)
        return PartyQuestRuntimeCanonicalObserveStatus::ResourceLimitExceeded;

    const auto pendingIt = m_pendingByQuest.find(identity.QuestId);
    if (pendingIt != m_pendingByQuest.end())
    {
        const auto& current = pendingIt->second;
        if (aCandidate.WorldRevision <= current.WorldRevision ||
            aCandidate.CanonicalSnapshot.Revision <=
                current.CanonicalSnapshot.Revision)
        {
            m_transactionIdentities.emplace(
                aCandidate.TransactionId,
                identity);
            return PartyQuestRuntimeCanonicalObserveStatus::Stale;
        }

        m_transactionIdentities.emplace(aCandidate.TransactionId, identity);
        pendingIt->second = std::move(aCandidate);
        return PartyQuestRuntimeCanonicalObserveStatus::Superseded;
    }

    if (m_pendingByQuest.size() >= MaxPendingQuests)
        return PartyQuestRuntimeCanonicalObserveStatus::ResourceLimitExceeded;

    m_transactionIdentities.emplace(aCandidate.TransactionId, identity);
    m_pendingByQuest.emplace(identity.QuestId, std::move(aCandidate));
    return PartyQuestRuntimeCanonicalObserveStatus::Accepted;
}

const PartyQuestRuntimeCanonicalCandidate*
PartyQuestRuntimeCanonicalInbox::FindLatest(
    const GameId& acQuestId) const noexcept
{
    const auto it = m_pendingByQuest.find(acQuestId);
    return it != m_pendingByQuest.end() ? &it->second : nullptr;
}

std::optional<PartyQuestRuntimeCanonicalAuthorization>
PartyQuestRuntimeCanonicalInbox::TryAuthorizeLatest(
    const GameId& acQuestId,
    const PartyQuestReplica& acPublishedReplica) const noexcept
{
    if (!m_campaignId.IsValid() || !acQuestId)
        return std::nullopt;

    const auto it = m_pendingByQuest.find(acQuestId);
    if (it == m_pendingByQuest.end())
        return std::nullopt;

    const auto& candidate = it->second;
    if (candidate.CampaignId != m_campaignId ||
        !MatchesPublishedReplicaHead(candidate, acPublishedReplica))
    {
        return std::nullopt;
    }

    PartyQuestRuntimeCanonicalAuthorization authorization(candidate);
    if (!authorization.IsVerified())
        return std::nullopt;

    return authorization;
}
