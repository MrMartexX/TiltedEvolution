#pragma once

#include <Structs/GameId.h>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

enum class PartyQuestTransitionAdmissionStatus : uint8_t
{
    Eligible,
    NeedsReview,
    Unsupported,
    StaleRevision,
    Duplicate,
    Retired
};

enum class PartyQuestExpectedRunningState : uint8_t
{
    Unknown,
    Running,
    Stopped
};

enum class PartyQuestRecoveryClass : uint8_t
{
    Unknown,
    NoMutation,
    DurableCheckpointRequired
};

struct PartyQuestTransitionIdentity
{
    GameId QuestId{};
    uint16_t SourceStage{};
    uint16_t TargetStage{};
    uint32_t PolicyVersion{};
    uint64_t EnvironmentFingerprint{};
    uint64_t TopologyFingerprint{};
    uint64_t WinningOverrideFingerprint{};
    uint64_t ScriptFingerprint{};

    bool operator==(const PartyQuestTransitionIdentity&) const noexcept = default;
};

struct PartyQuestTransitionEvidence
{
    static constexpr uint32_t MaxCollectionEntries = 4096;

    PartyQuestTransitionIdentity Identity;
    uint32_t AnalyzerSchemaVersion{};
    uint32_t AnalyzerVersion{};
    uint64_t AnalyzerProvenanceFingerprint{};
    uint64_t AuthoritativeRevision{};
    uint64_t OperationId{};
    PartyQuestExpectedRunningState RunningState{PartyQuestExpectedRunningState::Unknown};
    std::vector<uint16_t> PriorStages;
    std::vector<uint16_t> Objectives;
    uint32_t AliasCount{};
    uint32_t SceneCount{};
    uint32_t CreatedReferenceCount{};
    bool HasPlayerAlias{};
    bool HasPlayerSpecificPreconditions{};
    bool HasPartySpecificPreconditions{};
    bool HasInventoryMutation{};
    bool HasQuestObjectMutation{};
    bool HasWorldMutation{};
    bool HasAliasMutation{};
    bool HasUnboundedScriptEffects{};
    bool HasIntermediateStage{};
    bool SideEffectSurfaceKnown{};
    bool ReadinessEvidenceKnown{};
    bool RequiresReadiness{};
    bool QuiescencePolicyKnown{};
    bool RequiresQuiescence{};
    bool PostconditionsKnown{};
    PartyQuestRecoveryClass RecoveryClass{PartyQuestRecoveryClass::Unknown};
};

struct PartyQuestReviewedTransition
{
    PartyQuestTransitionIdentity Identity;
    uint32_t AnalyzerSchemaVersion{};
    uint32_t AnalyzerVersion{};
    uint64_t AnalyzerProvenanceFingerprint{};
    uint64_t ReviewerFingerprint{};
    PartyQuestExpectedRunningState RunningState{PartyQuestExpectedRunningState::Unknown};
    std::vector<uint16_t> RequiredPriorStages;
    std::vector<uint16_t> RequiredObjectives;
    bool PlayerSpecific{};
    bool PartySpecific{};
    bool RequiresReadiness{};
    bool RequiresQuiescence{};
    PartyQuestRecoveryClass RecoveryClass{PartyQuestRecoveryClass::Unknown};
};

struct PartyQuestTransitionAdmissionDecision
{
    PartyQuestTransitionAdmissionStatus Status{PartyQuestTransitionAdmissionStatus::Unsupported};
    bool StructuralEligibility{};
    bool EnvironmentCompatible{};
    bool AuthorizationGranted{}; // Intentionally always false in Task 05.
    uint64_t TransitionFingerprint{};
    const char* Reason{"unsupported"};

    [[nodiscard]] bool IsEligible() const noexcept
    {
        return Status == PartyQuestTransitionAdmissionStatus::Eligible &&
            StructuralEligibility && EnvironmentCompatible &&
            !AuthorizationGranted && TransitionFingerprint != 0;
    }
};

class PartyQuestCompatibilityAdmissionPolicy final
{
public:
    [[nodiscard]] static PartyQuestTransitionAdmissionDecision Evaluate(
        const PartyQuestReviewedTransition& acReviewed,
        const PartyQuestTransitionEvidence& acEvidence,
        uint64_t aLastAcceptedRevision = 0,
        uint64_t aLastOperationId = 0) noexcept;
};

class PartyQuestReviewedTransitionManifest final
{
public:
    [[nodiscard]] bool Publish(
        std::span<const PartyQuestReviewedTransition> acReviewed) noexcept;
    [[nodiscard]] const PartyQuestReviewedTransition* Find(
        const PartyQuestTransitionIdentity& acIdentity) const noexcept;
    [[nodiscard]] size_t Size() const noexcept { return m_entries.size(); }

private:
    struct KeyHash
    {
        size_t operator()(const PartyQuestTransitionIdentity& acIdentity) const noexcept;
    };
    std::unordered_map<PartyQuestTransitionIdentity,
        PartyQuestReviewedTransition, KeyHash> m_entries;
};
