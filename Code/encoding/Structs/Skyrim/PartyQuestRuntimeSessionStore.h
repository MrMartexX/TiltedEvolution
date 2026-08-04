#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>
#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>

#include <cstdint>

enum class PartyQuestRuntimeSessionStoreStatus : uint8_t
{
    NewSession,
    Clean,
    DeferredRestored,
    PreMutationRestarted,
    RecoveryRequired,
    InvalidIdentity,
    InvalidLayout,
    JournalIdentityMismatch,
    JournalRecoveryRequired,
    JournalInvalid,
    CleanupPersistenceFailed
};

struct PartyQuestRuntimeSessionStoreResult
{
    PartyQuestRuntimeSessionStoreStatus Status{
        PartyQuestRuntimeSessionStoreStatus::JournalInvalid};
    PartyQuestRuntimeApplyPersistenceStatus PersistenceStatus{
        PartyQuestRuntimeApplyPersistenceStatus::InvalidData};
    PartyQuestRuntimeRecoveryDisposition RecoveryDisposition{
        PartyQuestRuntimeRecoveryDisposition::InvalidState};

    [[nodiscard]] bool IsUsable() const noexcept
    {
        return Status == PartyQuestRuntimeSessionStoreStatus::NewSession ||
            Status == PartyQuestRuntimeSessionStoreStatus::Clean ||
            Status == PartyQuestRuntimeSessionStoreStatus::DeferredRestored ||
            Status == PartyQuestRuntimeSessionStoreStatus::PreMutationRestarted ||
            Status == PartyQuestRuntimeSessionStoreStatus::RecoveryRequired;
    }
};

/**
 * Binds PartyQuestRuntimeApplySession to the player-scoped durable runtime
 * sidecar and restores its previous control-plane state.
 *
 * This is a startup/bootstrap primitive only. It does not call Skyrim, Papyrus,
 * save/load APIs or the checkpoint restore executor.
 *
 * Unusable journals fail closed by removing the session's durable handler, so
 * later runtime transitions cannot accidentally overwrite recovery evidence.
 * RecoveryRequired intentionally keeps the handler bound because clearing that
 * barrier must be persisted through this same player-scoped sidecar.
 *
 * A stale pre-mutation active entry is intentionally discarded by
 * PartyQuestRuntimeApplyCoordinator. This store immediately persists that
 * cleaned state so another process restart does not repeatedly resurrect the
 * discarded entry from disk.
 */
class PartyQuestRuntimeSessionStore final
{
public:
    [[nodiscard]] static PartyQuestRuntimeSessionStoreResult BindAndLoad(
        PartyQuestRuntimeApplySession& aSession,
        const PartyQuestCoopSavePaths& acPaths) noexcept;
};
