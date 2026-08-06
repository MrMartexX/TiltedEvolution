#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <cstdint>

enum class PartyQuestRuntimeLifecycleEvent : uint8_t
{
    LoadGame,
    NewGame,
    MainMenu,
    ProfileSwitch,
    CampaignSwitch,
    Disconnect,
    Shutdown
};

enum class PartyQuestRuntimeLifecycleFenceStatus : uint8_t
{
    Allowed,
    SafeAbortApplied,
    CheckpointRestoreRequired,
    RecoveryBlocked,
    PersistenceFailure,
    GuardMismatch,
    GuardReleaseFailed,
    InvalidState
};

struct PartyQuestRuntimeLifecycleFenceResult
{
    PartyQuestRuntimeLifecycleEvent Event{PartyQuestRuntimeLifecycleEvent::LoadGame};
    PartyQuestRuntimeLifecycleFenceStatus Status{
        PartyQuestRuntimeLifecycleFenceStatus::InvalidState};
    uint64_t TransactionId{};
    bool GuardHeld{};

    [[nodiscard]] bool CanProceed() const noexcept
    {
        return Status == PartyQuestRuntimeLifecycleFenceStatus::Allowed ||
            Status == PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied;
    }
};

/**
 * Game-independent lifecycle fence for a guarded runtime repair.
 *
 * The future Skyrim integration must call Prepare() before Load Game, New Game,
 * Main Menu, profile/campaign switch, disconnect and orderly shutdown. This
 * policy does not itself hook those engine events.
 *
 * Pre-mutation work is durably aborted before the lifecycle transition may
 * proceed. Once RuntimeMutationMayHaveOccurred is set, the transition is denied
 * until the exact PreRepair checkpoint is restored. A crash-recovery barrier is
 * likewise never treated as safe to cross.
 *
 * Process termination that bypasses this API cannot be prevented here; the
 * durable mutation marker remains the restart fail-closed mechanism for that
 * case.
 */
class PartyQuestRuntimeLifecycleFence final
{
public:
    [[nodiscard]] static PartyQuestRuntimeLifecycleFenceResult Prepare(
        PartyQuestRuntimeGuardedSession& aGuardedSession,
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept;
};
