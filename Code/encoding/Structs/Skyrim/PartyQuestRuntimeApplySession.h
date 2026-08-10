#pragma once

#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
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
    ResourceLimitExceeded,
    InvalidRequest,
    UnsafePlan,
    PersistenceFailure
};

enum class PartyQuestRuntimeDurableTransitionStatus : uint8_t
{
    Applied,
    InvalidState,
    CheckpointRestoreRequired,
    PersistenceFailure,
    InsufficientDurability
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
 * campaign/player-bound recovery state is persisted through the bound handler,
 * and only then published in memory. ArmRuntimeMutation additionally requires
 * an explicitly declared guarantee meeting the PoC process-crash contract.
 * Current storage does not claim power-loss durability.
 *
 * The session still does not call Skyrim, Papyrus, save APIs or file I/O itself.
 * Production runtime integration must couple it to PartyQuestRuntimeGuardedSession
 * so the physical process SaveGuard participates in every critical transition.
 */
class PartyQuestRuntimeApplySession final
{
public:
    using DurableStateHandler = std::function<bool(const PartyQuestRuntimeRecoveryState&)>;

    PartyQuestRuntimeApplySession(
        PartyQuestCampaignId aCampaignId,
        PartyQuestPlayerProfileId aPlayerProfileId,
        DurableStateHandler aDurableStateHandler = {},
        PartyQuestPersistenceGuarantee aPersistenceGuarantee =
            PartyQuestPersistenceGuarantee::Volatile);

    void SetDurableStateHandler(
        DurableStateHandler aDurableStateHandler,
        PartyQuestPersistenceGuarantee aPersistenceGuarantee =
            PartyQuestPersistenceGuarantee::Volatile);

    [[nodiscard]] PartyQuestRuntimeDurableBeginStatus Begin(
        const PartyQuestRuntimeApplyRequest& acRequest);

    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkWorldReady(
        const PartyQuestRuntimeApplyRequest& acCurrentRequest);
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkCheckpointCreated(uint64_t aTransactionId);

    /**
     * Persists the crash-recovery marker before returning Applied. Only after
     * Applied may the caller dispatch the real runtime mutation.
     */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus ArmRuntimeMutation(uint64_t aTransactionId);

    /** Durable transition using capability-backed trusted runtime observations. */
    [[nodiscard]] PartyQuestRuntimeDurableTransitionStatus MarkPapyrusQuiescent(
        PartyQuestPapyrusRuntimeMonitor& aMonitor,
        PartyQuestPapyrusQuiescenceAuthorization&& aAuthorization);

    /**
     * Diagnostic low-level surface retained for isolated state-machine tests.
     * The process-guarded production wrapper rejects this path.
     */
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

    [[nodiscard]] PartyQuestPersistenceGuarantee GetPersistenceGuarantee() const noexcept
    {
        return m_persistenceGuarantee;
    }

private:
    [[nodiscard]] bool Persist(const PartyQuestRuntimeApplyCoordinator& acCandidate) const;
    [[nodiscard]] static PartyQuestRuntimeDurableBeginStatus TranslateBeginStatus(
        PartyQuestRuntimeApplyBeginStatus aStatus) noexcept;

    PartyQuestCampaignId m_campaignId;
    PartyQuestPlayerProfileId m_playerProfileId;
    DurableStateHandler m_durableStateHandler;
    PartyQuestPersistenceGuarantee m_persistenceGuarantee{
        PartyQuestPersistenceGuarantee::Volatile};
    PartyQuestRuntimeApplyCoordinator m_coordinator;
};
