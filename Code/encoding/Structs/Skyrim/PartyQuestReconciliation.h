#pragma once

#include <Structs/GameId.h>
#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/PartyQuestCompatibilityAdmission.h>
#include <Structs/Skyrim/PartyQuestRuntimeApply.h>
#include <Structs/Skyrim/QuestSnapshot.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * Deterministic classifications for the relationship between the authoritative server
 * canonical quest state and an independent local client save.
 *
 * Distinct stage numbers are never enough to infer a safe execution path.
 */
enum class PartyQuestReconciliationClassification : uint8_t
{
    ExactEquivalent,
    LocallyBehindReviewedEdge,
    LocallyAheadCompatible,
    DivergentBranchOrState,
    MissingOrNotStartedOrStopped,
    EnvironmentMismatch,
    InsufficientOrUnstableObservation,
    RecoveryRequired
};

enum class PartyQuestReconciliationOutcome : uint8_t
{
    NoOpEquivalent,
    ApplyReviewedTransition,
    ConfirmLocallyAhead,
    RetryAfterFreshSnapshot,
    RecoveryRequired,
    UnsupportedDivergence,
    EnvironmentRejected,
    MissingModOrQuest
};

/**
 * Pure planner input containing authoritative state, candidate local observation,
 * exact reviewed transition requirement, and generation/ordering context.
 */
struct PartyQuestReconciliationInput
{
    PartyQuestCampaignId CampaignId;
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    uint64_t RuntimeGeneration{};
    QuestSnapshot CanonicalSnapshot;
    std::optional<QuestSnapshot> LocalSnapshot;
    std::optional<PartyQuestReviewedTransition> ReviewedTransition;
    uint64_t ObservedEnvironmentFingerprint{};
    bool PriorTransactionPending{};
    bool RecoveryBlocked{};
    uint64_t LastCommittedWorldRevision{};
    std::vector<uint64_t> RetiredTransactions;
};

/**
 * Immutable outcome of reconciliation planning, producing either an admissible candidate
 * for the dry-run pipeline or an explicit fail-closed reason.
 */
struct PartyQuestReconciliationResult
{
    PartyQuestReconciliationClassification Classification{PartyQuestReconciliationClassification::DivergentBranchOrState};
    PartyQuestReconciliationOutcome Outcome{PartyQuestReconciliationOutcome::UnsupportedDivergence};
    std::optional<PartyQuestRuntimeApplyRequest> CandidateRequest;
    const char* Reason{"unsupported"};

    [[nodiscard]] bool IsAdmissible() const noexcept
    {
        return Outcome == PartyQuestReconciliationOutcome::NoOpEquivalent || Outcome == PartyQuestReconciliationOutcome::ApplyReviewedTransition ||
               Outcome == PartyQuestReconciliationOutcome::ConfirmLocallyAhead;
    }

    [[nodiscard]] bool RequiresRetry() const noexcept { return Outcome == PartyQuestReconciliationOutcome::RetryAfterFreshSnapshot; }

    [[nodiscard]] bool RequiresRecovery() const noexcept { return Outcome == PartyQuestReconciliationOutcome::RecoveryRequired; }
};

class PartyQuestReconciliationPlanner final
{
public:
    [[nodiscard]] static PartyQuestReconciliationResult Plan(const PartyQuestReconciliationInput& acInput) noexcept;

private:
    [[nodiscard]] static bool IsExactMatch(const QuestSnapshot& acCanonical, const QuestSnapshot& acLocal) noexcept;
    [[nodiscard]] static bool IsLocallyAheadCompatible(const QuestSnapshot& acCanonical, const QuestSnapshot& acLocal) noexcept;
    [[nodiscard]] static bool HasPriorStages(const QuestSnapshot& acLocal, const std::vector<uint16_t>& acRequiredPrior) noexcept;
    [[nodiscard]] static bool HasRequiredObjectives(const QuestSnapshot& acLocal, const std::vector<uint16_t>& acRequiredObjectives) noexcept;
};
