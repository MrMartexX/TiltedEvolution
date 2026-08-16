#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

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
