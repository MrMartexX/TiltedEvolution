#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApply.h>

#include <functional>

enum class PartyQuestRuntimeDurableBeginStatus : uint8_t
{
    Started,
    Deferred,
    DuplicatePending,
    DuplicateCommitted,
    TransactionConflict,
    Busy,
    RecoveryBlocked,
    InvalidRequest,
    UnsafePlan,
    PersistenceFailure
};

enum class PartyQuestRuntimeDurableTransitionStatus : uint8_t
{
    Applied,
    InvalidState,
    CheckpointRestoreRequired,
    PersistenceFailure
};

struct PartyQuestRuntimeDurableVerificationResult
{
    PartyQuestRuntimeVerificationStatus Verification{PartyQuestRuntimeVerificationStatus::InvalidState};
    bool PersistenceFailed{};
};

/**
 * Durability barrier around PartyQuestRuntimeApplyCoordinator.
 *
 * Every state-changing transition is first applied to a copy, then the complete
 * campaign/player-bound recovery state is durably persisted, and only then
 * published in memory. This is especially important for ArmRuntimeMutation():
 * the "mutation may have occurred" marker is guaranteed durable before the
 * caller is allowed to invoke any Skyrim/Papyrus mutation.
 *
 * The session still does not call Skyrim, Papyrus, save APIs or file I/O itself.
 */
class PartyQuestRuntimeApplySession final
{
public:
    using DurableStateHandler = std::function<bool(const PartyQuestRuntimeRecoveryState&)>;

    PartyQuestRuntimeApplySession(
        PartyQuestCampaignId aCampaignId,
        PartyQuestPlayerProfileId aPlayerProfileId,
        DurableStateHandler aDurableStateHandler = {});

    void SetDurableStateHandler(DurableStateHandler aDurableStateHandler);

    [[nodiscard]] PartyQuestRuntimeDurableBeginStatus Begin(
        const PartyQuestRuntimeApplyRequest& acRequest);

    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkWorldReady(uint64_t aTransactionId);
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkCheckpointCreated(uint64_t aTransactionId);

    /**
     * Persists the crash-recovery marker before returning Applied. Only after
     * Applied may the caller dispatch the real runtime mutation.
     */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus ArmRuntimeMutation(uint64_t aTransactionId);

    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkPapyrusQuiescent(
        PartyQuestPapyrusQuiescenceTracker& aTracker,
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization);

    /** Legacy compatibility surface: naked transaction assertions fail closed. */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkPapyrusQuiescent(uint64_t aTransactionId);

    [[nodiscard]] PartyQuestRuntimeDurableVerificationResult SubmitResnapshot(
        uint64_t aTransactionId,
        QuestSnapshot aObservedSnapshot);

    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus Commit(uint64_t aTransactionId);

    /** Aborts only when no runtime mutation may have occurred. */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus AbortBeforeMutation(uint64_t aTransactionId);

    /**
     * Call only after the external LastKnownGood/pre-repair checkpoint has
     * actually been restored in the same process.
     */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus CompleteLiveCheckpointRestore(
        uint64_t aTransactionId);

    [[nodiscard]] PartyQuestRuntimeRecoveryDisposition RestoreRecoveryState(
        const PartyQuestRuntimeRecoveryState& acState) noexcept;

    /**
     * Call only after an external checkpoint restore resolved a crash-recovery
     * barrier. The cleared barrier is persisted before it becomes visible.
     */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus CompleteCrashCheckpointRestore(
        uint64_t aTransactionId);

    [[nodiscard]] const PartyQuestRuntimeApplyCoordinator& GetCoordinator() const noexcept
    {
        return m_coordinator;
    }

    [[nodiscard]] const PartyQuestCampaignId& GetCampaignId() const noexcept
    {
        return m_campaignId;
    }

    [[nodiscard]] const PartyQuestPlayerProfileId& GetPlayerProfileId() const noexcept
    {
        return m_playerProfileId;
    }

private:
    [[nodiscard]] bool Persist(const PartyQuestRuntimeApplyCoordinator& acCandidate) const;
    [[nodiscard]] static PartyQuestRuntimeDurableBeginStatus TranslateBeginStatus(
        PartyQuestRuntimeApplyBeginStatus aStatus) noexcept;

    PartyQuestCampaignId m_campaignId;
    PartyQuestPlayerProfileId m_playerProfileId;
    DurableStateHandler m_durableStateHandler;
    PartyQuestRuntimeApplyCoordinator m_coordinator;
};