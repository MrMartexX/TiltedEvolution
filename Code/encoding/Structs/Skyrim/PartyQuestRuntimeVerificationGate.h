#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <functional>
#include <optional>

/**
 * Point-of-use post-mutation verification gate.
 *
 * Production verification must not accept a caller-supplied snapshot together
 * with the compatibility fingerprint copied from the expected envelope. This
 * gate first proves that the exact process runtime owner, guarded session,
 * durable session and process SaveGuard are one identity domain. Structural
 * mismatch returns InvalidState without observing runtime state, installing a
 * capability or applying fail-closed recovery to either session.
 *
 * BeginAttempt captures an immutable owner/operation envelope. Submit consumes
 * that one-shot attempt, revalidates the complete envelope under the shared
 * generation lease, then obtains and classifies the postcondition observation.
 *
 * Missing observers are Unknown, not mismatch and never success. Duplicate and
 * out-of-order attempts cannot count as independent stable samples.
 */
class PartyQuestRuntimeVerificationAttempt final
{
public:
    PartyQuestRuntimeVerificationAttempt() noexcept = default;
    PartyQuestRuntimeVerificationAttempt(
        PartyQuestRuntimeVerificationAttempt&& aOther) noexcept
        : m_campaignId(aOther.m_campaignId)
        , m_playerProfileId(aOther.m_playerProfileId)
        , m_runtimeGeneration(aOther.m_runtimeGeneration)
        , m_transactionId(aOther.m_transactionId)
        , m_targetWorldRevision(aOther.m_targetWorldRevision)
        , m_questId(aOther.m_questId)
        , m_actions(aOther.m_actions)
        , m_expected(aOther.m_expected)
        , m_attemptId(aOther.m_attemptId)
    {
        aOther.Invalidate();
    }

    PartyQuestRuntimeVerificationAttempt& operator=(
        PartyQuestRuntimeVerificationAttempt&& aOther) noexcept
    {
        if (this != &aOther)
        {
            m_campaignId = aOther.m_campaignId;
            m_playerProfileId = aOther.m_playerProfileId;
            m_runtimeGeneration = aOther.m_runtimeGeneration;
            m_transactionId = aOther.m_transactionId;
            m_targetWorldRevision = aOther.m_targetWorldRevision;
            m_questId = aOther.m_questId;
            m_actions = aOther.m_actions;
            m_expected = aOther.m_expected;
            m_attemptId = aOther.m_attemptId;
            aOther.Invalidate();
        }
        return *this;
    }

    PartyQuestRuntimeVerificationAttempt(
        const PartyQuestRuntimeVerificationAttempt&) = delete;
    PartyQuestRuntimeVerificationAttempt& operator=(
        const PartyQuestRuntimeVerificationAttempt&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_campaignId.IsValid() && m_playerProfileId.IsValid() &&
            m_runtimeGeneration != 0 && m_transactionId != 0 &&
            m_targetWorldRevision != 0 && m_questId && m_attemptId != 0;
    }

    [[nodiscard]] uint64_t GetAttemptId() const noexcept { return m_attemptId; }

private:
    friend class PartyQuestRuntimeVerificationGate;

    void Invalidate() noexcept
    {
        m_runtimeGeneration = 0;
        m_transactionId = 0;
        m_attemptId = 0;
    }

    PartyQuestCampaignId m_campaignId;
    PartyQuestPlayerProfileId m_playerProfileId;
    uint64_t m_runtimeGeneration{};
    uint64_t m_transactionId{};
    uint64_t m_targetWorldRevision{};
    GameId m_questId{};
    PartyQuestApplyAction m_actions{PartyQuestApplyAction::None};
    PartyQuestVerificationEnvelopeV1 m_expected;
    uint64_t m_attemptId{};
};

struct PartyQuestRuntimeVerificationAttemptResult
{
    PartyQuestRuntimeVerificationEvidenceStatus Status{
        PartyQuestRuntimeVerificationEvidenceStatus::InvalidEvidence};
    std::optional<PartyQuestRuntimeVerificationAttempt> Attempt;
};

class PartyQuestRuntimeVerificationGate final
{
public:
    using SnapshotObserver = std::function<std::optional<QuestSnapshot>(
        const GameId&)>;
    using CompatibilityObserver = std::function<std::optional<PartyQuestRuntimeCompatibilityFacts>(
        const GameId&)>;

    [[nodiscard]] static PartyQuestRuntimeVerificationAttemptResult BeginAttempt(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        PartyQuestRuntimeApplySession& aSession,
        PartyQuestRuntimeVerificationMonitor& aMonitor,
        uint64_t aTransactionId) noexcept;

    [[nodiscard]] static PartyQuestRuntimeGuardedVerificationResult Submit(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        PartyQuestRuntimeApplySession& aSession,
        PartyQuestRuntimeVerificationMonitor& aMonitor,
        PartyQuestRuntimeVerificationAttempt&& aAttempt,
        uint64_t aNowMs,
        const PartyQuestRuntimeCompatibilityRequirement& acRequirement,
        const SnapshotObserver& acSnapshotObserver,
        const CompatibilityObserver& acCompatibilityObserver) noexcept;
};
