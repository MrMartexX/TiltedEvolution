#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeLifecycleFence.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceLease.h>
#include <Structs/Skyrim/PartyQuestReplicaWorkspaceRecovery.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionStore.h>

#include <cstdint>
#include <memory>
#include <optional>

enum class PartyQuestRuntimeSessionOwnerBindStatus : uint8_t
{
    Bound,
    AlreadyBound,
    BindConflict,
    ProcessGuardBusy,
    InvalidIdentity,
    InvalidLayout,
    StoreRejected,
    ReconcileBlocked,
    WorkspaceBusy,
    WorkspaceLeaseFailure,
    WorkspaceRecoveryFailure
};

struct PartyQuestRuntimeSessionOwnerBindResult
{
    PartyQuestRuntimeSessionOwnerBindStatus Status{
        PartyQuestRuntimeSessionOwnerBindStatus::StoreRejected};
    PartyQuestRuntimeSessionStoreResult Store;
    PartyQuestRuntimeGuardStatus ReconcileStatus{
        PartyQuestRuntimeGuardStatus::InvalidState};
    bool GuardHeld{};
    PartyQuestReplicaWorkspaceLeaseStatus LeaseStatus{
        PartyQuestReplicaWorkspaceLeaseStatus::NotAttempted};
    PartyQuestReplicaWorkspaceRecoveryResult WorkspaceRecovery;

    [[nodiscard]] bool IsBound() const noexcept
    {
        return Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound ||
            Status == PartyQuestRuntimeSessionOwnerBindStatus::AlreadyBound ||
            Status == PartyQuestRuntimeSessionOwnerBindStatus::ReconcileBlocked;
    }

    [[nodiscard]] bool RecoveryRequired() const noexcept
    {
        return Store.Status == PartyQuestRuntimeSessionStoreStatus::RecoveryRequired;
    }
};

/**
 * Process-local owner for one campaign/player runtime repair session.
 *
 * The owner is deliberately inactive until an external integration supplies an
 * already verified CampaignId, stable local PlayerProfileId and their exact
 * PartyQuestCoopSavePaths. It never generates either identity and therefore
 * cannot silently collapse independent characters into one runtime journal.
 *
 * Bind() first acquires the exact kernel-backed workspace lease and quarantines
 * only unpublished replica copy temporaries. It then hydrates through
 * PartyQuestRuntimeSessionStore, preserving the committed TransactionId journal
 * and any recovery barrier before publishing a guarded session.
 * RecoveryRequired reconstructs the real process SaveGuard via
 * ReconcileLoadedState(). A usable hydrated session is retained even when that
 * reconciliation is blocked, so durable recovery evidence is never discarded
 * just because another process-local guard appeared concurrently.
 *
 * Rebinding to another campaign/profile/root is forbidden. Call
 * PrepareAndRelease() first; it delegates to PartyQuestRuntimeLifecycleFence and
 * only destroys ownership after the requested lifecycle transition is proven
 * safe. Post-mutation/recovery-blocked state therefore remains owned and locked.
 *
 * This is a bootstrap/lifetime primitive only. It does not hook Skyrim load/new
 * game/main menu, network disconnect, party leave or shutdown entrypoints.
 */
class PartyQuestRuntimeSessionOwner final
{
public:
    PartyQuestRuntimeSessionOwner() noexcept = default;
    ~PartyQuestRuntimeSessionOwner() = default;

    PartyQuestRuntimeSessionOwner(const PartyQuestRuntimeSessionOwner&) = delete;
    PartyQuestRuntimeSessionOwner& operator=(const PartyQuestRuntimeSessionOwner&) = delete;
    PartyQuestRuntimeSessionOwner(PartyQuestRuntimeSessionOwner&&) = delete;
    PartyQuestRuntimeSessionOwner& operator=(PartyQuestRuntimeSessionOwner&&) = delete;

    [[nodiscard]] PartyQuestRuntimeSessionOwnerBindResult Bind(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId,
        const PartyQuestCoopSavePaths& acPaths) noexcept;

    /**
     * Fence the requested lifecycle transition and release ownership only when
     * the fence says the transition may proceed.
     */
    [[nodiscard]] PartyQuestRuntimeLifecycleFenceResult PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept;

    [[nodiscard]] bool IsBound() const noexcept
    {
        return m_workspaceLease.IsHeld() && m_session != nullptr &&
            m_guardedSession != nullptr && m_paths.has_value() &&
            m_workspaceRecoveryResult.has_value() &&
            m_workspaceRecoveryResult->IsSuccess();
    }

    [[nodiscard]] bool IsRecoveryBlocked() const noexcept
    {
        return m_session && m_session->GetCoordinator().IsRecoveryBlocked();
    }

    [[nodiscard]] PartyQuestRuntimeGuardedSession* GetGuardedSession() noexcept
    {
        return m_guardedSession.get();
    }

    [[nodiscard]] const PartyQuestRuntimeGuardedSession* GetGuardedSession() const noexcept
    {
        return m_guardedSession.get();
    }

    [[nodiscard]] const PartyQuestRuntimeApplySession* GetRuntimeSession() const noexcept
    {
        return m_session.get();
    }

    [[nodiscard]] const PartyQuestCoopSavePaths* GetPaths() const noexcept
    {
        return m_paths ? &*m_paths : nullptr;
    }

    [[nodiscard]] const PartyQuestRuntimeSessionStoreResult* GetStoreResult() const noexcept
    {
        return m_storeResult ? &*m_storeResult : nullptr;
    }

private:
    void Clear() noexcept;

    PartyQuestReplicaWorkspaceLease m_workspaceLease;
    std::unique_ptr<PartyQuestRuntimeApplySession> m_session;
    std::unique_ptr<PartyQuestRuntimeGuardedSession> m_guardedSession;
    std::optional<PartyQuestCoopSavePaths> m_paths;
    std::optional<PartyQuestRuntimeSessionStoreResult> m_storeResult;
    std::optional<PartyQuestReplicaWorkspaceRecoveryResult>
        m_workspaceRecoveryResult;
};
