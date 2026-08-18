#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

class PartyQuestReplica;

enum class PartyQuestRuntimeCanonicalObserveStatus : uint8_t
{
    Accepted,
    Superseded,
    Duplicate,
    Stale,
    TransactionConflict,
    CampaignMismatch,
    ReplicaHeadMismatch,
    InvalidInput,
    ResourceLimitExceeded
};

struct PartyQuestRuntimeCanonicalCandidate
{
    PartyQuestCampaignId CampaignId;
    uint64_t TransactionId{};
    uint64_t WorldRevision{};
    QuestSnapshot CanonicalSnapshot;

    bool operator==(const PartyQuestRuntimeCanonicalCandidate&) const noexcept = default;
};

/**
 * One-shot process-local proof that an exact runtime candidate passed the
 * canonical inbox and was still the current published replica head when the
 * capability was issued.
 *
 * This is provenance authority, not authentication. The network/protocol layer
 * is still responsible for admitting only verified server canonical deliveries
 * into PartyQuestRuntimeCanonicalInbox. Public callers cannot construct a
 * verified token, and a token is consumed by runtime request planning so it
 * cannot authorize a second locally reconstructed request.
 */
class PartyQuestRuntimeCanonicalAuthorization final
{
public:
    PartyQuestRuntimeCanonicalAuthorization() noexcept = default;
    PartyQuestRuntimeCanonicalAuthorization(
        const PartyQuestRuntimeCanonicalAuthorization&) = delete;
    PartyQuestRuntimeCanonicalAuthorization& operator=(
        const PartyQuestRuntimeCanonicalAuthorization&) = delete;

    PartyQuestRuntimeCanonicalAuthorization(
        PartyQuestRuntimeCanonicalAuthorization&& aOther) noexcept
    {
        *this = std::move(aOther);
    }

    PartyQuestRuntimeCanonicalAuthorization& operator=(
        PartyQuestRuntimeCanonicalAuthorization&& aOther) noexcept
    {
        if (this == &aOther)
            return *this;

        m_campaignId = aOther.m_campaignId;
        m_transactionId = aOther.m_transactionId;
        m_worldRevision = aOther.m_worldRevision;
        m_questId = aOther.m_questId;
        m_questRevision = aOther.m_questRevision;
        m_canonicalDigest = aOther.m_canonicalDigest;
        m_issued = aOther.m_issued;

        aOther.m_campaignId = {};
        aOther.m_transactionId = 0;
        aOther.m_worldRevision = 0;
        aOther.m_questId = {};
        aOther.m_questRevision = 0;
        aOther.m_canonicalDigest = 0;
        aOther.m_issued = false;
        return *this;
    }

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_issued;
    }

    [[nodiscard]] bool Matches(
        const PartyQuestRuntimeCanonicalCandidate& acCandidate) const noexcept
    {
        return m_issued &&
            acCandidate.CampaignId == m_campaignId &&
            acCandidate.TransactionId == m_transactionId &&
            acCandidate.WorldRevision == m_worldRevision &&
            acCandidate.CanonicalSnapshot.QuestId == m_questId &&
            acCandidate.CanonicalSnapshot.Revision == m_questRevision &&
            acCandidate.CanonicalSnapshot.ComputeDigest() == m_canonicalDigest;
    }

    /** Consumes this capability only when the exact candidate still matches. */
    [[nodiscard]] bool Consume(
        const PartyQuestRuntimeCanonicalCandidate& acCandidate) noexcept
    {
        if (!Matches(acCandidate))
            return false;

        m_issued = false;
        return true;
    }

private:
    explicit PartyQuestRuntimeCanonicalAuthorization(
        const PartyQuestRuntimeCanonicalCandidate& acCandidate) noexcept
        : m_campaignId(acCandidate.CampaignId)
        , m_transactionId(acCandidate.TransactionId)
        , m_worldRevision(acCandidate.WorldRevision)
        , m_questId(acCandidate.CanonicalSnapshot.QuestId)
        , m_questRevision(acCandidate.CanonicalSnapshot.Revision)
        , m_canonicalDigest(acCandidate.CanonicalSnapshot.ComputeDigest())
        , m_issued(
              acCandidate.CampaignId.IsValid() &&
              acCandidate.TransactionId != 0 &&
              acCandidate.WorldRevision != 0 &&
              static_cast<bool>(acCandidate.CanonicalSnapshot.QuestId) &&
              acCandidate.CanonicalSnapshot.Revision != 0 &&
              m_canonicalDigest != 0)
    {
    }

    PartyQuestCampaignId m_campaignId;
    uint64_t m_transactionId{};
    uint64_t m_worldRevision{};
    GameId m_questId{};
    uint64_t m_questRevision{};
    uint64_t m_canonicalDigest{};
    bool m_issued{};

    friend class PartyQuestRuntimeCanonicalInbox;
};

/**
 * Evidence-only handoff between canonical protocol state and runtime planning.
 *
 * The inbox preserves exact server transaction provenance and the latest
 * monotonic candidate per quest. It has no runtime-session, SaveGuard,
 * compatibility, checkpoint or mutation APIs. A candidate leaving this object
 * still has to pass PartyQuestRuntimeRequestPlanner with independent local
 * admission/compatibility evidence before it can become a RuntimeApplyRequest.
 */
class PartyQuestRuntimeCanonicalInbox final
{
public:
    static constexpr size_t MaxRememberedTransactions = 4096;
    static constexpr size_t MaxPendingQuests = 4096;

    /**
     * Binds the inbox to one verified campaign. Rebinding to another valid
     * campaign clears all old pending candidates and transaction identities.
     */
    bool BindCampaign(const PartyQuestCampaignId& acCampaignId);

    /** Clears candidates and transaction history and removes campaign binding. */
    void Reset() noexcept;

    /**
     * Observes exact server provenance only while it is still the published
     * canonical replica head for that quest/world revision. The replica check
     * prevents an old exact protocol Duplicate from being reintroduced after a
     * later repair or canonical update has advanced the published replica.
     */
    [[nodiscard]] PartyQuestRuntimeCanonicalObserveStatus Observe(
        PartyQuestRuntimeCanonicalCandidate aCandidate,
        const PartyQuestReplica& acPublishedReplica);

    [[nodiscard]] const PartyQuestRuntimeCanonicalCandidate* FindLatest(
        const GameId& acQuestId) const noexcept;

    /**
     * Issues one process-local planning capability only if the remembered latest
     * candidate is still the exact currently published replica head. A repair or
     * canonical advance therefore invalidates issuance even before Reset().
     */
    [[nodiscard]] std::optional<PartyQuestRuntimeCanonicalAuthorization>
    TryAuthorizeLatest(
        const GameId& acQuestId,
        const PartyQuestReplica& acPublishedReplica) const noexcept;

    [[nodiscard]] const PartyQuestCampaignId& GetCampaignId() const noexcept
    {
        return m_campaignId;
    }

    [[nodiscard]] size_t GetPendingQuestCount() const noexcept
    {
        return m_pendingByQuest.size();
    }

    [[nodiscard]] size_t GetRememberedTransactionCount() const noexcept
    {
        return m_transactionIdentities.size();
    }

private:
    struct TransactionIdentity
    {
        PartyQuestCampaignId CampaignId;
        uint64_t WorldRevision{};
        GameId QuestId{};
        uint64_t QuestRevision{};
        uint64_t CanonicalDigest{};

        bool operator==(const TransactionIdentity&) const noexcept = default;
    };

    PartyQuestCampaignId m_campaignId;
    std::unordered_map<GameId, PartyQuestRuntimeCanonicalCandidate> m_pendingByQuest;
    std::unordered_map<uint64_t, TransactionIdentity> m_transactionIdentities;
};
