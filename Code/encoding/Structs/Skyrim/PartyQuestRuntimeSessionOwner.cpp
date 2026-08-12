#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeWorkspacePublicationAuthority.h>

namespace
{
PartyQuestRuntimeSessionOwnerBindResult MakeEarlyFailure(
    PartyQuestRuntimeSessionOwnerBindStatus aStatus,
    PartyQuestRuntimeSessionStoreStatus aStoreStatus) noexcept
{
    PartyQuestRuntimeSessionOwnerBindResult result;
    result.Status = aStatus;
    result.Store.Status = aStoreStatus;
    result.Store.PersistenceStatus = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
    result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
    result.GuardHeld = PartyQuestSaveGuard::GetProcessGuard().IsActive();
    result.LeaseStatus = PartyQuestReplicaWorkspaceLeaseStatus::NotAttempted;
    return result;
}
} // namespace

PartyQuestRuntimeSessionOwner::~PartyQuestRuntimeSessionOwner() noexcept
{
    // Teardown is itself a runtime-context transition. Hold the exclusive
    // generation barrier until all guarded/session/workspace authority is gone.
    auto generationInvalidation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().BeginInvalidation();
    Clear();
}

PartyQuestRuntimeSessionOwnerBindResult PartyQuestRuntimeSessionOwner::Bind(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
        {
            return MakeEarlyFailure(
                PartyQuestRuntimeSessionOwnerBindStatus::InvalidIdentity,
                PartyQuestRuntimeSessionStoreStatus::InvalidIdentity);
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return MakeEarlyFailure(
                PartyQuestRuntimeSessionOwnerBindStatus::InvalidLayout,
                PartyQuestRuntimeSessionStoreStatus::InvalidLayout);
        }

        if (IsBound())
        {
            PartyQuestRuntimeSessionOwnerBindResult result;
            result.Store = *m_storeResult;
            result.LeaseStatus = PartyQuestReplicaWorkspaceLeaseStatus::Acquired;
            result.WorkspaceRecovery = *m_workspaceRecoveryResult;

            if (m_session->GetCampaignId() != acCampaignId ||
                m_session->GetPlayerProfileId() != acPlayerProfileId ||
                *m_paths != acPaths)
            {
                result.Status = PartyQuestRuntimeSessionOwnerBindStatus::BindConflict;
                result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
                result.GuardHeld = m_guardedSession->GetSaveGuard().IsActive();
                return result;
            }

            // An idempotent exact bind also retries physical guard reconciliation.
            // This lets a previously bound-but-blocked owner recover after an
            // unrelated guard conflict is externally resolved without reloading
            // or replacing its durable session.
            const auto reconciled = m_guardedSession->ReconcileLoadedState();
            result.ReconcileStatus = reconciled.Status;
            result.GuardHeld = reconciled.GuardHeld;
            result.Status = reconciled.Status == PartyQuestRuntimeGuardStatus::Ready
                ? PartyQuestRuntimeSessionOwnerBindStatus::AlreadyBound
                : PartyQuestRuntimeSessionOwnerBindStatus::ReconcileBlocked;
            return result;
        }

        auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
        if (processGuard.IsActive())
        {
            PartyQuestRuntimeSessionOwnerBindResult result;
            result.Status = PartyQuestRuntimeSessionOwnerBindStatus::ProcessGuardBusy;
            result.ReconcileStatus = PartyQuestRuntimeGuardStatus::GuardBusy;
            result.GuardHeld = true;
            return result;
        }

        PartyQuestReplicaWorkspaceLease workspaceLease;
        const auto leaseStatus = workspaceLease.Acquire(
            acPaths,
            acCampaignId,
            acPlayerProfileId);
        if (leaseStatus != PartyQuestReplicaWorkspaceLeaseStatus::Acquired)
        {
            PartyQuestRuntimeSessionOwnerBindResult result;
            result.Status = leaseStatus == PartyQuestReplicaWorkspaceLeaseStatus::Busy
                ? PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceBusy
                : PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceLeaseFailure;
            result.Store.Status = PartyQuestRuntimeSessionStoreStatus::InvalidLayout;
            result.Store.PersistenceStatus =
                PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
            result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
            result.GuardHeld = false;
            result.LeaseStatus = leaseStatus;
            return result;
        }

        const auto workspaceRecovery =
            PartyQuestReplicaWorkspaceRecovery::QuarantineOrphanCopyTemporaries(
                acPaths,
                acCampaignId,
                acPlayerProfileId,
                workspaceLease);
        if (!workspaceRecovery.IsSuccess())
        {
            PartyQuestRuntimeSessionOwnerBindResult result;
            result.Status =
                PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceRecoveryFailure;
            result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
            result.GuardHeld = false;
            result.LeaseStatus = leaseStatus;
            result.WorkspaceRecovery = workspaceRecovery;
            return result;
        }

        // Finish all potentially throwing path copies before reconciliation can
        // acquire the physical process SaveGuard for a RecoveryRequired journal.
        PartyQuestCoopSavePaths ownedPaths = acPaths;
        auto session = std::make_unique<PartyQuestRuntimeApplySession>(
            acCampaignId,
            acPlayerProfileId);
        const auto store = PartyQuestRuntimeSessionStore::BindAndLoad(
            *session,
            acPaths);

        PartyQuestRuntimeSessionOwnerBindResult result;
        result.Store = store;
        result.LeaseStatus = leaseStatus;
        result.WorkspaceRecovery = workspaceRecovery;
        if (!store.IsUsable())
        {
            result.Status = PartyQuestRuntimeSessionOwnerBindStatus::StoreRejected;
            result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
            result.GuardHeld = processGuard.IsActive();
            return result;
        }

        auto guardedSession =
            std::make_unique<PartyQuestRuntimeGuardedSession>(*session);

        // Publish the successfully hydrated ownership graph before the noexcept
        // guard reconciliation. From this point forward, even a guard conflict
        // leaves a live owner able to retry instead of discarding durable state.
        m_paths.emplace(std::move(ownedPaths));
        m_storeResult = store;
        m_workspaceRecoveryResult = workspaceRecovery;
        m_workspaceLease = std::move(workspaceLease);
        m_session = std::move(session);
        m_guardedSession = std::move(guardedSession);

        auto publicationCapability = m_workspaceLease.CreatePublicationCapability(
            acPaths,
            acCampaignId,
            acPlayerProfileId);
        if (!publicationCapability.IsVerified() ||
            !PartyQuestRuntimeWorkspacePublicationAuthority::Bind(
                *m_session,
                acPaths,
                std::move(publicationCapability)))
        {
            result.Status =
                PartyQuestRuntimeSessionOwnerBindStatus::WorkspaceLeaseFailure;
            result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
            result.GuardHeld = processGuard.IsActive();
            Clear();
            return result;
        }

        const auto reconciled = m_guardedSession->ReconcileLoadedState();
        result.ReconcileStatus = reconciled.Status;
        result.GuardHeld = reconciled.GuardHeld;
        result.Status = reconciled.Status == PartyQuestRuntimeGuardStatus::Ready
            ? PartyQuestRuntimeSessionOwnerBindStatus::Bound
            : PartyQuestRuntimeSessionOwnerBindStatus::ReconcileBlocked;
        return result;
    }
    catch (...)
    {
        PartyQuestRuntimeSessionOwnerBindResult result;
        result.Status = IsBound()
            ? PartyQuestRuntimeSessionOwnerBindStatus::ReconcileBlocked
            : PartyQuestRuntimeSessionOwnerBindStatus::StoreRejected;
        result.ReconcileStatus = PartyQuestRuntimeGuardStatus::InvalidState;
        result.GuardHeld = PartyQuestSaveGuard::GetProcessGuard().IsActive();
        result.LeaseStatus = m_workspaceLease.IsHeld()
            ? PartyQuestReplicaWorkspaceLeaseStatus::Acquired
            : PartyQuestReplicaWorkspaceLeaseStatus::NotAttempted;
        if (m_storeResult)
            result.Store = *m_storeResult;
        if (m_workspaceRecoveryResult)
            result.WorkspaceRecovery = *m_workspaceRecoveryResult;
        return result;
    }
}

PartyQuestRuntimeLifecycleFenceResult
PartyQuestRuntimeSessionOwner::PrepareAndRelease(
    PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    // Advance and pin the process generation before touching the guarded runtime
    // owner. An in-flight executor lease drains first; no new dispatch can begin
    // until the lifecycle decision and any allowed teardown are complete.
    auto generationInvalidation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().BeginInvalidation();

    if (!IsBound())
    {
        PartyQuestRuntimeLifecycleFenceResult result;
        result.Event = aEvent;
        result.Status = PartyQuestRuntimeLifecycleFenceStatus::Allowed;
        return result;
    }

    auto result = PartyQuestRuntimeLifecycleFence::Prepare(
        *m_guardedSession,
        aEvent);
    if (result.CanProceed())
        Clear();
    return result;
}

void PartyQuestRuntimeSessionOwner::Clear() noexcept
{
    if (m_session)
        PartyQuestRuntimeWorkspacePublicationAuthority::Unbind(*m_session);
    m_guardedSession.reset();
    m_session.reset();
    m_paths.reset();
    m_storeResult.reset();
    m_workspaceRecoveryResult.reset();
    m_workspaceLease.Release();
}
