#include <Structs/Skyrim/PartyQuestRuntimeCanonicalInbox.h>

#include <utility>

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
    PartyQuestRuntimeCanonicalCandidate aCandidate)
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
    if (transactionIt != m_transactionIdentities.end())
    {
        return transactionIt->second == identity
            ? PartyQuestRuntimeCanonicalObserveStatus::Duplicate
            : PartyQuestRuntimeCanonicalObserveStatus::TransactionConflict;
    }

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
