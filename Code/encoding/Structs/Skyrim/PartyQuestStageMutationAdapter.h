#pragma once

#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>
#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestReviewedQuestProfiles.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <cstdint>
#include <functional>
#include <optional>

enum class PartyQuestStageMutationStatus : uint8_t
{
    Applied,
    IdempotentNoOp,
    ControlledActivationDisabled,
    InvalidThread,
    InvalidPlan,
    PreconditionSnapshotFailed,
    PreconditionSurfaceRejected,
    StageNotFound,
    BackwardTransitionRejected,
    UnreviewedQuest,
    UnreviewedTransition,
    SaveGuardInactive,
    SessionUnbound,
    RecoveryBlocked,
    CoordinatorInactive,
    CheckpointNotCommitted,
    DurableBarrierUnarmed,
    RevisionMismatch,
    TransactionMismatch,
    GenerationChanged,
    CompatibilityRejected,
    PapyrusBusy,
    NativeInvocationFailed,
    PostconditionMismatch,
    DuplicateTransactionReplayed
};

[[nodiscard]] constexpr const char* PartyQuestStageMutationStatusName(PartyQuestStageMutationStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestStageMutationStatus::Applied: return "applied";
    case PartyQuestStageMutationStatus::IdempotentNoOp: return "idempotent-no-op";
    case PartyQuestStageMutationStatus::ControlledActivationDisabled: return "controlled-activation-disabled";
    case PartyQuestStageMutationStatus::InvalidThread: return "invalid-thread";
    case PartyQuestStageMutationStatus::InvalidPlan: return "invalid-plan";
    case PartyQuestStageMutationStatus::PreconditionSnapshotFailed: return "precondition-snapshot-failed";
    case PartyQuestStageMutationStatus::PreconditionSurfaceRejected: return "precondition-surface-rejected";
    case PartyQuestStageMutationStatus::StageNotFound: return "stage-not-found";
    case PartyQuestStageMutationStatus::BackwardTransitionRejected: return "backward-transition-rejected";
    case PartyQuestStageMutationStatus::UnreviewedQuest: return "unreviewed-quest";
    case PartyQuestStageMutationStatus::UnreviewedTransition: return "unreviewed-transition";
    case PartyQuestStageMutationStatus::SaveGuardInactive: return "save-guard-inactive";
    case PartyQuestStageMutationStatus::SessionUnbound: return "session-unbound";
    case PartyQuestStageMutationStatus::RecoveryBlocked: return "recovery-blocked";
    case PartyQuestStageMutationStatus::CoordinatorInactive: return "coordinator-inactive";
    case PartyQuestStageMutationStatus::CheckpointNotCommitted: return "checkpoint-not-committed";
    case PartyQuestStageMutationStatus::DurableBarrierUnarmed: return "durable-barrier-unarmed";
    case PartyQuestStageMutationStatus::RevisionMismatch: return "revision-mismatch";
    case PartyQuestStageMutationStatus::TransactionMismatch: return "transaction-mismatch";
    case PartyQuestStageMutationStatus::GenerationChanged: return "generation-changed";
    case PartyQuestStageMutationStatus::CompatibilityRejected: return "compatibility-rejected";
    case PartyQuestStageMutationStatus::PapyrusBusy: return "papyrus-busy";
    case PartyQuestStageMutationStatus::NativeInvocationFailed: return "native-invocation-failed";
    case PartyQuestStageMutationStatus::PostconditionMismatch: return "postcondition-mismatch";
    case PartyQuestStageMutationStatus::DuplicateTransactionReplayed: return "duplicate-transaction-replayed";
    }
    return "unknown";
}

struct PartyQuestStageMutationResult
{
    PartyQuestStageMutationStatus Status{PartyQuestStageMutationStatus::ControlledActivationDisabled};
    bool Success{false};

    [[nodiscard]] bool WasApplied() const noexcept
    {
        return Success && (Status == PartyQuestStageMutationStatus::Applied || Status == PartyQuestStageMutationStatus::IdempotentNoOp);
    }
};

struct PartyQuestStageMutationContext
{
    std::function<std::optional<QuestSnapshot>()> CollectSnapshot;
    std::function<bool(uint16_t aStage)> ContainsStage;
    std::function<bool(uint16_t aStage)> NativeSetStage;
    std::function<bool(uint64_t aTransactionId)> IsPapyrusQuiescent;
    std::function<bool()> IsCurrentUpdateThread;
    std::function<void(uint64_t aTransactionId, PartyQuestStageMutationStatus aReason)> OnRecoveryRequired;
};

/**
 * Stage mutation adapter enforcing the complete P0 verification envelope.
 *
 * Native SetStage is treated as attempt evidence only. Pre-repair checkpoint
 * authorization, fresh compatibility facts, main update thread, reviewed quest
 * and transition authorization, and postcondition verification are mandatory.
 */
class PartyQuestStageMutationAdapter final
{
public:
    [[nodiscard]] static PartyQuestStageMutationResult Execute(
        const PartyQuestRuntimeApplyRequest& acRequest, PartyQuestStageMutationContext& aContext,
        std::optional<std::reference_wrapper<PartyQuestRuntimeGuardedSession>> aGuardedSession = std::nullopt,
        std::optional<std::reference_wrapper<PartyQuestRuntimeGenerationFence>> aGenerationFence = std::nullopt) noexcept;

    static void SetControlledActivationEnabled(bool aEnabled) noexcept;
    [[nodiscard]] static bool IsControlledActivationEnabled() noexcept;

    static void ResetExecutedTransactionsForTest() noexcept;
    [[nodiscard]] static bool IsTransactionExecuted(uint64_t aTransactionId) noexcept;

    [[nodiscard]] static bool HasOnlyStageMutationSurface(const QuestSnapshot& acSnapshot) noexcept;
};
