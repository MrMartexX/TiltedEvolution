#pragma once

#include <Structs/Skyrim/PartyQuestAdmission.h>

#include <cstddef>
#include <cstdint>

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

struct PartyQuestRuntimeSafetyFacts
{
    size_t ReferenceAliasCount{};
    size_t LocationAliasCount{};
    size_t CreatedReferenceCount{};
    size_t ObjectiveCount{};
    size_t UnresolvedReferenceAliasCount{};
    size_t QuestObjectAliasCount{};
    bool HasSceneParticipant{};
    bool IsTerminalState{};
};

/**
 * Quest-specific compatibility capability. No live code currently supplies a
 * verified adapter, so the default path can never become RuntimeSafe.
 */
struct PartyQuestRuntimeSafetyProfile
{
    bool HasVerifiedNativeAdapter{};
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
