#pragma once

#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/PartyQuestPlayerProfile.h>
#include <Structs/Skyrim/PartyQuestRuntimeSafety.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

enum class PartyQuestRuntimeApplyState : uint8_t
{
    DeferredWorld,
    AwaitingCheckpoint,
    ReadyToApply,
    WaitingForPapyrus,
    Verifying,
    ReadyToCommit
};

enum class PartyQuestRuntimeApplyBeginStatus : uint8_t
{
    Started,
    Deferred,
    DuplicatePending,
    DuplicateCommitted,
    TransactionConflict,
    Busy,
    RecoveryBlocked,
    InvalidRequest,
    UnsafePlan
};

enum class PartyQuestRuntimeVerificationStatus : uint8_t
{
    InvalidState,
    Diverged,
    NeedsStableSample,
    Stable
};

enum class PartyQuestRuntimeRecoveryDisposition : uint8_t
{
    InvalidState,
    CampaignMismatch,
    PlayerProfileMismatch,
    Clean,
    DeferredRestored,
    PreMutationRestartRequired,
    CheckpointRestoreRequired
};

struct PartyQuestRuntimeApplyRequest
{
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};

    /**
     * Deterministic fingerprint of the authoritative sidecar requirement set
     * selected before Begin(). Zero means no checkpoint-sidecar contract was
     * bound and is invalid even when the intended manifest is empty.
     */
    uint64_t SidecarManifestFingerprint{};

    QuestSnapshot CanonicalSnapshot;
    PartyQuestApplyPlan Plan;
};

struct PartyQuestRuntimeApplyEntry
{
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    GameId QuestId{};
    uint64_t CanonicalDigest{};
    uint64_t SidecarManifestFingerprint{};
    PartyQuestApplyAction Actions{PartyQuestApplyAction::None};
    PartyQuestRuntimeApplyState State{PartyQuestRuntimeApplyState::AwaitingCheckpoint};
    bool SaveGuardActive{};
    bool CheckpointCreated{};
    bool RuntimeMutationMayHaveOccurred{};
    uint64_t LastObservedDigest{};
    uint32_t StableCanonicalSamples{};

    bool operator==(const PartyQuestRuntimeApplyEntry&) const noexcept = default;
};

struct PartyQuestRuntimeCommittedRecord
{
    uint64_t TransactionId{};
    uint64_t TargetWorldRevision{};
    GameId QuestId{};
    uint64_t CanonicalDigest{};
    uint64_t SidecarManifestFingerprint{};
    PartyQuestApplyAction Actions{PartyQuestApplyAction::None};

    bool operator==(const PartyQuestRuntimeCommittedRecord&) const noexcept = default;
};

struct PartyQuestRuntimeRecoveryState
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    std::vector<PartyQuestRuntimeCommittedRecord> Committed;
    std::optional<PartyQuestRuntimeApplyEntry> Active;

    bool operator==(const PartyQuestRuntimeRecoveryState&) const noexcept = default;
};

/**
 * Game-independent lifecycle for a future critical Skyrim repair transaction.
 *
 * It intentionally has no TESQuest/Papyrus/save callbacks. Tests drive the
 * lifecycle explicitly so sequencing, idempotency, deferred-world behavior,
 * save guarding, quiescence and verification can be validated before a Skyrim
 * executor is connected.
 */
class PartyQuestRuntimeApplyCoordinator final
{
public:
    [[nodiscard]] PartyQuestRuntimeApplyBeginStatus Begin(
        const PartyQuestRuntimeApplyRequest& acRequest) noexcept;

    /** Called when all deferred world/cell targets are available. */
    bool MarkWorldReady(uint64_t aTransactionId) noexcept;

    /** Records that a pre-repair checkpoint exists while saving is guarded. */
    bool MarkCheckpointCreated(uint64_t aTransactionId) noexcept;

    /** Records dispatch of the future runtime mutation; does not execute it. */
    bool MarkApplyDispatched(uint64_t aTransactionId) noexcept;

    /** Papyrus/event queue must settle before verification samples are accepted. */
    bool MarkPapyrusQuiescent(uint64_t aTransactionId) noexcept;

    /** Requires two consecutive canonical digests before commit is allowed. */
    [[nodiscard]] PartyQuestRuntimeVerificationStatus SubmitResnapshot(
        uint64_t aTransactionId,
        QuestSnapshot aObservedSnapshot) noexcept;

    /** Commits only after stable canonical verification and releases the save guard. */
    bool Commit(uint64_t aTransactionId) noexcept;

    /** Aborts the active repair and releases the save guard. */
    bool Abort(uint64_t aTransactionId) noexcept;

    /** Exports committed-id journal plus any in-progress recovery marker. */
    [[nodiscard]] PartyQuestRuntimeRecoveryState ExportRecoveryState(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) const;

    /**
     * Restores durable metadata into a fresh coordinator. Deferred work may be
     * resumed. Pre-mutation work is intentionally restarted. Any record where
     * mutation may have occurred blocks new work until checkpoint restoration
     * is explicitly acknowledged. Recovery data from another campaign or local
     * player profile is never accepted.
     */
    [[nodiscard]] PartyQuestRuntimeRecoveryDisposition RestoreRecoveryState(
        const PartyQuestRuntimeRecoveryState& acState,
        const PartyQuestCampaignId& acExpectedCampaignId,
        const PartyQuestPlayerProfileId& acExpectedPlayerProfileId) noexcept;

    /** Clears a crash-recovery barrier after the external checkpoint is restored. */
    bool AcknowledgeCheckpointRestored(uint64_t aTransactionId) noexcept;

    [[nodiscard]] const PartyQuestRuntimeApplyEntry* GetActive() const noexcept;
    [[nodiscard]] bool IsCommitted(uint64_t aTransactionId) const noexcept;
    [[nodiscard]] bool IsSaveGuardActive() const noexcept;
    [[nodiscard]] bool IsRecoveryBlocked() const noexcept { return m_recoveryBlocked; }
    [[nodiscard]] const PartyQuestRuntimeApplyEntry* GetRecoveryRecord() const noexcept
    {
        return m_recoveryRecord ? &*m_recoveryRecord : nullptr;
    }
    [[nodiscard]] bool LastAbortRequiresCheckpointRestore() const noexcept
    {
        return m_lastAbortRequiresCheckpointRestore;
    }

private:
    struct Fingerprint
    {
        GameId QuestId{};
        uint64_t TargetWorldRevision{};
        uint64_t CanonicalDigest{};
        uint64_t SidecarManifestFingerprint{};
        PartyQuestApplyAction Actions{PartyQuestApplyAction::None};

        bool operator==(const Fingerprint&) const noexcept = default;
    };

    [[nodiscard]] static std::optional<Fingerprint> ValidateAndFingerprint(
        const PartyQuestRuntimeApplyRequest& acRequest) noexcept;
    [[nodiscard]] static Fingerprint FingerprintActive(
        const PartyQuestRuntimeApplyEntry& acEntry) noexcept;
    [[nodiscard]] static bool ValidateRecoveryEntry(
        const PartyQuestRuntimeApplyEntry& acEntry) noexcept;

    std::optional<PartyQuestRuntimeApplyEntry> m_active;
    std::optional<PartyQuestRuntimeApplyEntry> m_recoveryRecord;
    std::unordered_map<uint64_t, Fingerprint> m_committed;
    bool m_recoveryBlocked{};
    bool m_lastAbortRequiresCheckpointRestore{};
};
