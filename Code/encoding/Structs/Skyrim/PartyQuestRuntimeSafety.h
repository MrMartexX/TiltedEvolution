#pragma once

#include <Structs/Skyrim/PartyQuestAdmission.h>

#include <cstddef>
#include <cstdint>
#include <optional>

class PartyQuestRuntimeCompatibilityPolicy;
class PartyQuestRuntimeSafetyTestAccess;

/**
 * Structural runtime-safety disposition for a canonical quest snapshot.
 *
 * This is deliberately stricter than admission. SharedProvisional means a
 * quest may enter CampaignState; it does not mean the quest may be mutated on
 * another Skyrim runtime. The current milestone only builds dry-run plans.
 */
enum class PartyQuestRuntimeSafetyStatus : uint8_t
{
    Blocked,
    StageOnly,
    Deferred,
    RequiresAdapter,
    RuntimeSafe
};

enum class PartyQuestRuntimeSafetyReason : uint8_t
{
    AdmissionBlocked,
    SimpleStageTransition,
    ReferenceAliasesNeedWorld,
    SceneParticipantActive,
    InactiveQuestState,
    TerminalQuestState,
    CreatedReferences,
    LocationAliases,
    QuestObjectAliases,
    UnresolvedReferenceAliases,
    ComplexAliasTopology,
    VerifiedNativeAdapter
};

enum class PartyQuestApplyAction : uint32_t
{
    None = 0,
    StageTransition = 1u << 0,
    VerifyObjectives = 1u << 1,
    WaitForWorldTargets = 1u << 2,
    WaitForPapyrusQuiescence = 1u << 3,
    ResnapshotAndVerify = 1u << 4,
    AdapterManaged = 1u << 5
};

constexpr PartyQuestApplyAction operator|(PartyQuestApplyAction aLeft, PartyQuestApplyAction aRight) noexcept
{
    return static_cast<PartyQuestApplyAction>(
        static_cast<uint32_t>(aLeft) | static_cast<uint32_t>(aRight));
}

constexpr PartyQuestApplyAction& operator|=(PartyQuestApplyAction& aLeft, PartyQuestApplyAction aRight) noexcept
{
    aLeft = aLeft | aRight;
    return aLeft;
}

constexpr bool HasPartyQuestApplyAction(PartyQuestApplyAction aActions, PartyQuestApplyAction aAction) noexcept
{
    return (static_cast<uint32_t>(aActions) & static_cast<uint32_t>(aAction)) != 0;
}

enum class PartyQuestVerificationComponent : uint32_t
{
    None = 0,
    QuestSnapshot = 1u << 0,
    Aliases = 1u << 1,
    InventoryEffects = 1u << 2,
    WorldEffects = 1u << 3,
    AdapterState = 1u << 4,
    Compatibility = 1u << 5
};

constexpr PartyQuestVerificationComponent operator|(
    PartyQuestVerificationComponent aLeft,
    PartyQuestVerificationComponent aRight) noexcept
{
    return static_cast<PartyQuestVerificationComponent>(
        static_cast<uint32_t>(aLeft) | static_cast<uint32_t>(aRight));
}

struct PartyQuestVerificationEnvelopeV1
{
    static constexpr uint16_t kSchemaVersion = 1;

    uint16_t SchemaVersion{kSchemaVersion};
    PartyQuestVerificationComponent Required{PartyQuestVerificationComponent::None};
    uint64_t QuestSnapshotDigest{};
    uint64_t AliasDigest{};
    uint64_t InventoryEffectsDigest{};
    uint64_t WorldEffectsDigest{};
    uint64_t AdapterStateDigest{};
    uint64_t CompatibilityFingerprint{};

    [[nodiscard]] uint64_t ComputeFingerprint() const noexcept;
    bool operator==(const PartyQuestVerificationEnvelopeV1&) const noexcept = default;
};

/** Local fail-closed action-to-postcondition coverage policy. */
class PartyQuestVerificationPolicy final
{
public:
    [[nodiscard]] static std::optional<PartyQuestVerificationEnvelopeV1> BuildExpected(
        PartyQuestApplyAction aActions,
        uint64_t aQuestSnapshotDigest,
        uint64_t aCompatibilityFingerprint) noexcept;

    [[nodiscard]] static bool IsCompleteForActions(
        const PartyQuestVerificationEnvelopeV1& acEnvelope,
        PartyQuestApplyAction aActions) noexcept;
};

struct PartyQuestRuntimeSafetyFacts
{
    size_t ReferenceAliasCount{};
    size_t LocationAliasCount{};
    size_t CreatedReferenceCount{};
    size_t ObjectiveCount{};
    size_t UnresolvedReferenceAliasCount{};
    size_t QuestObjectAliasCount{};
    bool HasSceneParticipant{};
    bool IsInactiveState{};
    bool IsTerminalState{};
};

/**
 * Quest-specific compatibility capability.
 *
 * A verified profile is issued exclusively by
 * PartyQuestRuntimeCompatibilityPolicy after exact manifest/fact matching. It
 * is bound to the reviewed QuestId and to a deterministic fingerprint of that
 * exact compatibility contract, so a profile authorized for Quest A cannot be
 * reused to classify Quest B RuntimeSafe.
 */
class PartyQuestRuntimeSafetyProfile final
{
public:
    PartyQuestRuntimeSafetyProfile() noexcept = default;

    [[nodiscard]] bool HasVerifiedNativeAdapter() const noexcept
    {
        return static_cast<bool>(m_questId) && m_compatibilityFingerprint != 0;
    }

    [[nodiscard]] bool IsVerifiedFor(const GameId& acQuestId) const noexcept
    {
        return HasVerifiedNativeAdapter() && acQuestId == m_questId;
    }

    [[nodiscard]] const GameId& GetQuestId() const noexcept
    {
        return m_questId;
    }

    [[nodiscard]] uint64_t GetCompatibilityFingerprint() const noexcept
    {
        return m_compatibilityFingerprint;
    }

private:
    PartyQuestRuntimeSafetyProfile(
        const GameId& acQuestId,
        uint64_t aCompatibilityFingerprint) noexcept
        : m_questId(acQuestId)
        , m_compatibilityFingerprint(aCompatibilityFingerprint)
    {
    }

    GameId m_questId{};
    uint64_t m_compatibilityFingerprint{};

    friend class PartyQuestRuntimeCompatibilityPolicy;
};

/**
 * Unforgeable capability for one exact runtime apply plan.
 *
 * Public callers can only construct the unverified default. A verified token is
 * issued by PartyQuestRuntimeSafetyPolicy after a quest-scoped compatibility
 * profile has classified the exact canonical snapshot RuntimeSafe and after the
 * final action set is known. The token binds:
 *
 *  - QuestId;
 *  - canonical snapshot digest;
 *  - exact compatibility-contract fingerprint;
 *  - exact apply-action bitset;
 *  - DryRunOnly disposition.
 *
 * RuntimeApply validates this token before admitting a new transaction, so
 * manually setting Safety.Status=RuntimeSafe is not mutation authority.
 */
class PartyQuestRuntimeMutationAuthorization final
{
public:
    PartyQuestRuntimeMutationAuthorization() noexcept = default;

    [[nodiscard]] bool IsVerified() const noexcept
    {
        return m_verified;
    }

    [[nodiscard]] uint64_t GetCompatibilityFingerprint() const noexcept
    {
        return m_compatibilityFingerprint;
    }

    [[nodiscard]] bool Matches(
        const QuestSnapshot& acSnapshot,
        PartyQuestApplyAction aActions,
        bool aDryRunOnly) const noexcept
    {
        if (!m_verified ||
            !acSnapshot.QuestId ||
            acSnapshot.QuestId != m_questId ||
            aActions != m_actions ||
            aDryRunOnly != m_dryRunOnly)
        {
            return false;
        }

        return acSnapshot.ComputeDigest() == m_canonicalDigest;
    }

private:
    PartyQuestRuntimeMutationAuthorization(
        const GameId& acQuestId,
        uint64_t aCanonicalDigest,
        uint64_t aCompatibilityFingerprint,
        PartyQuestApplyAction aActions,
        bool aDryRunOnly) noexcept
        : m_questId(acQuestId)
        , m_canonicalDigest(aCanonicalDigest)
        , m_compatibilityFingerprint(aCompatibilityFingerprint)
        , m_actions(aActions)
        , m_dryRunOnly(aDryRunOnly)
        , m_verified(
              static_cast<bool>(acQuestId) &&
              aCanonicalDigest != 0 &&
              aCompatibilityFingerprint != 0 &&
              aActions != PartyQuestApplyAction::None)
    {
    }

    GameId m_questId{};
    uint64_t m_canonicalDigest{};
    uint64_t m_compatibilityFingerprint{};
    PartyQuestApplyAction m_actions{PartyQuestApplyAction::None};
    bool m_dryRunOnly{true};
    bool m_verified{};

    friend class PartyQuestRuntimeSafetyPolicy;
    // Defined only in Code/tests; no production factory/API exists.
    friend class PartyQuestRuntimeSafetyTestAccess;
};

struct PartyQuestRuntimeSafetyDecision
{
    PartyQuestRuntimeSafetyStatus Status{PartyQuestRuntimeSafetyStatus::Blocked};
    PartyQuestRuntimeSafetyReason Reason{PartyQuestRuntimeSafetyReason::AdmissionBlocked};
    PartyQuestRuntimeSafetyFacts Facts;

    [[nodiscard]] bool IsRuntimeSafe() const noexcept
    {
        return Status == PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    }
};

/**
 * Non-executing plan describing what a future repair executor would need to do.
 * DryRunOnly remains true in this milestone even for a structurally verified
 * adapter path; no canonical SetStage/alias/inventory mutation is wired yet.
 */
struct PartyQuestApplyPlan
{
    PartyQuestRuntimeSafetyDecision Safety;
    PartyQuestApplyAction Actions{PartyQuestApplyAction::None};
    PartyQuestRuntimeMutationAuthorization MutationAuthorization;
    bool DryRunOnly{true};

    [[nodiscard]] bool WouldMutateQuestStage() const noexcept
    {
        return HasPartyQuestApplyAction(Actions, PartyQuestApplyAction::StageTransition);
    }
};

class PartyQuestRuntimeSafetyPolicy final
{
public:
    static constexpr size_t kComplexAliasThreshold = 16;

    [[nodiscard]] static PartyQuestRuntimeSafetyFacts Inspect(
        const QuestSnapshot& acSnapshot) noexcept;

    [[nodiscard]] static PartyQuestRuntimeSafetyDecision Evaluate(
        const PartyQuestAdmissionDecision& acAdmission,
        const QuestSnapshot& acSnapshot,
        const PartyQuestRuntimeSafetyProfile& acProfile = {}) noexcept;

    [[nodiscard]] static PartyQuestApplyPlan BuildApplyPlan(
        const PartyQuestAdmissionDecision& acAdmission,
        const QuestSnapshot& acSnapshot,
        const PartyQuestRuntimeSafetyProfile& acProfile = {}) noexcept;
};
