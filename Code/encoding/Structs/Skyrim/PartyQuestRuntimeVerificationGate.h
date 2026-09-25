#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>

#include <functional>
#include <optional>

enum class PartyQuestRuntimeEnvelopeOutcome : uint8_t
{
    Authorized,
    Verified,
    BusyRetryable,
    RejectedStale,
    Unsupported,
    Timeout,
    MutationFailed,
    PostconditionMismatch,
    ObserverUnavailable
};

enum class PartyQuestRuntimeEnvelopeDisposition : uint8_t
{
    Commit,
    RecoveryRequired,
    NoOp
};

struct PartyQuestRuntimeEnvelopeIdentity
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t SessionId{};
    uint64_t PartyId{};
    uint64_t RuntimeGeneration{};
    uint64_t TransactionId{};
    uint64_t AuthoritativeRevision{};
    uint64_t OperationId{};
    PartyQuestTransitionIdentity Transition;
    uint64_t CompatibilityFingerprint{};
    uint64_t ExpectedCurrentSnapshotDigest{};
    uint64_t ExpectedTargetSnapshotDigest{};

    bool operator==(const PartyQuestRuntimeEnvelopeIdentity&) const noexcept = default;
};

/** Fresh point-of-use observations. Booleans are evidence, never authority. */
struct PartyQuestRuntimeEnvelopePreconditions
{
    PartyQuestRuntimeEnvelopeIdentity CurrentIdentity;
    QuestSnapshot CurrentSnapshot;
    bool ProcessOwnerCurrent{};
    bool SessionCurrent{};
    bool PartyCurrent{};
    bool TransactionActive{};
    bool RevisionCurrent{};
    bool CompatibilityCacheCurrent{};
    bool ReferenceEvidenceAvailable{};
    bool ReferenceReady{};
    uint64_t ReferenceGeneration{};
    bool PapyrusObserverAvailable{};
    bool PapyrusQuiescent{};
    bool CheckpointAuthorized{};
    uint64_t CheckpointTransactionId{};
    uint64_t CheckpointRevision{};
};

struct PartyQuestRuntimeEnvelopePostconditions
{
    std::optional<QuestSnapshot> Snapshot;
    uint32_t StableSamples{};
    bool AliasDelta{};
    bool SceneDelta{};
    bool InventoryDelta{};
    bool QuestObjectDelta{};
    bool WorldDelta{};
};

class PartyQuestRuntimeEnvelopeAuthorization final
{
public:
    PartyQuestRuntimeEnvelopeAuthorization() noexcept = default;
    PartyQuestRuntimeEnvelopeAuthorization(PartyQuestRuntimeEnvelopeAuthorization&& aOther) noexcept
        : m_identity(std::move(aOther.m_identity))
        , m_expectedTarget(std::move(aOther.m_expectedTarget))
        , m_nonce(aOther.m_nonce)
    {
        aOther.m_nonce = 0;
    }
    PartyQuestRuntimeEnvelopeAuthorization& operator=(PartyQuestRuntimeEnvelopeAuthorization&& aOther) noexcept
    {
        if (this != &aOther)
        {
            m_identity = std::move(aOther.m_identity);
            m_expectedTarget = std::move(aOther.m_expectedTarget);
            m_nonce = aOther.m_nonce;
            aOther.m_nonce = 0;
        }
        return *this;
    }
    PartyQuestRuntimeEnvelopeAuthorization(const PartyQuestRuntimeEnvelopeAuthorization&) = delete;
    PartyQuestRuntimeEnvelopeAuthorization& operator=(const PartyQuestRuntimeEnvelopeAuthorization&) = delete;
    [[nodiscard]] bool IsValid() const noexcept { return m_nonce != 0; }

private:
    friend class PartyQuestRuntimeVerificationGate;
    PartyQuestRuntimeEnvelopeIdentity m_identity;
    QuestSnapshot m_expectedTarget;
    uint64_t m_nonce{};
};

struct PartyQuestRuntimeEnvelopeResult
{
    PartyQuestRuntimeEnvelopeOutcome Outcome{PartyQuestRuntimeEnvelopeOutcome::Unsupported};
    PartyQuestRuntimeEnvelopeDisposition Disposition{PartyQuestRuntimeEnvelopeDisposition::NoOp};
    std::optional<PartyQuestRuntimeEnvelopeAuthorization> Authorization;
};

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
class PartyQuestRuntimeVerificationAttemptTestAccess;

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
    friend class PartyQuestRuntimeVerificationAttemptTestAccess;

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

    /**
     * Builds a single-use postcondition-verification authorization for one
     * exact reviewed transition. The supplied observations are validated data,
     * not process authority: callers still have to cross the existing owned
     * generation/dispatch gates. This token cannot dispatch native mutation or
     * bypass the global mutation-disable policy.
     */
    [[nodiscard]] static PartyQuestRuntimeEnvelopeResult AuthorizeEnvelope(
        const PartyQuestReviewedTransition& acReviewed,
        const PartyQuestTransitionEvidence& acAdmissionEvidence,
        const PartyQuestRuntimeEnvelopeIdentity& acExpectedIdentity,
        const QuestSnapshot& acExpectedTarget,
        const PartyQuestRuntimeEnvelopePreconditions& acObserved) noexcept;

    /** Classifies bounded post-mutation evidence and consumes authorization. */
    [[nodiscard]] static PartyQuestRuntimeEnvelopeResult CompleteEnvelope(
        PartyQuestRuntimeEnvelopeAuthorization&& aAuthorization,
        const PartyQuestRuntimeEnvelopeIdentity& acCurrentIdentity,
        const PartyQuestRuntimeEnvelopePostconditions& acObserved,
        bool aMutationAttempted,
        bool aMutationReturnedSuccess,
        bool aTimedOut = false) noexcept;

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
