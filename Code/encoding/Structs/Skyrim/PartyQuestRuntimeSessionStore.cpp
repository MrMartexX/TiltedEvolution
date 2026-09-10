#include <Structs/Skyrim/PartyQuestRuntimeSessionStore.h>

#include <functional>

namespace
{
using PersistenceWriter = std::function<PartyQuestRuntimeApplyPersistenceStatus(
    const PartyQuestRuntimeRecoveryState&)>;

PartyQuestRuntimeSessionStoreResult MakeStoreResult(
    PartyQuestRuntimeSessionStoreStatus aStatus,
    PartyQuestRuntimeApplyPersistenceStatus aPersistenceStatus,
    PartyQuestRuntimeRecoveryDisposition aDisposition =
        PartyQuestRuntimeRecoveryDisposition::InvalidState)
{
    PartyQuestRuntimeSessionStoreResult result;
    result.Status = aStatus;
    result.PersistenceStatus = aPersistenceStatus;
    result.RecoveryDisposition = aDisposition;
    return result;
}

PartyQuestRuntimeSessionStoreResult FailClosed(
    PartyQuestRuntimeApplySession& aSession,
    PartyQuestRuntimeSessionStoreStatus aStatus,
    PartyQuestRuntimeApplyPersistenceStatus aPersistenceStatus,
    PartyQuestRuntimeRecoveryDisposition aDisposition =
        PartyQuestRuntimeRecoveryDisposition::InvalidState)
{
    aSession.SetDurableStateHandler({});
    return MakeStoreResult(aStatus, aPersistenceStatus, aDisposition);
}

PartyQuestRuntimeSessionStoreResult BindAndLoadInternal(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaWorkspacePublicationCapability* apCapability) noexcept
{
    try
    {
        if (!aSession.GetCampaignId().IsValid() ||
            !aSession.GetPlayerProfileId().IsValid())
        {
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::InvalidIdentity,
                PartyQuestRuntimeApplyPersistenceStatus::InvalidData);
        }

        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::InvalidLayout,
                PartyQuestRuntimeApplyPersistenceStatus::InvalidData);
        }

        if (apCapability &&
            !apCapability->Protects(
                acPaths,
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId()))
        {
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::InvalidLayout,
                PartyQuestRuntimeApplyPersistenceStatus::InvalidData);
        }

        const std::filesystem::path sidecarPath = acPaths.RuntimeApplySidecar;
        PartyQuestPersistenceGuarantee persistenceGuarantee =
            PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee;
        bool powerLossDurableNamespace = false;
        if (apCapability)
        {
            powerLossDurableNamespace =
                apCapability->PreparePowerLossDurableRuntimeNamespace(
                    acPaths,
                    aSession.GetCampaignId(),
                    aSession.GetPlayerProfileId());
            if (powerLossDurableNamespace)
            {
                persistenceGuarantee =
                    PartyQuestPersistenceGuarantee::PowerLossDurable;
            }
        }

        PersistenceWriter persist;
        if (apCapability)
        {
            auto capability = *apCapability;
            const PartyQuestCoopSavePaths protectedPaths = acPaths;
            const PartyQuestCampaignId campaignId = aSession.GetCampaignId();
            const PartyQuestPlayerProfileId playerProfileId =
                aSession.GetPlayerProfileId();
            persist = [
                sidecarPath,
                protectedPaths,
                campaignId,
                playerProfileId,
                capability = std::move(capability),
                powerLossDurableNamespace](
                    const PartyQuestRuntimeRecoveryState& acState)
                -> PartyQuestRuntimeApplyPersistenceStatus
            {
                if (!capability.Protects(
                        protectedPaths,
                        campaignId,
                        playerProfileId))
                {
                    return PartyQuestRuntimeApplyPersistenceStatus::IoError;
                }

                return powerLossDurableNamespace
                    ? PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                          sidecarPath,
                          acState)
                    : PartyQuestRuntimeApplyPersistence::SaveAtomically(
                          sidecarPath,
                          acState);
            };
        }
        else
        {
            persist = [sidecarPath](const PartyQuestRuntimeRecoveryState& acState)
                -> PartyQuestRuntimeApplyPersistenceStatus
            {
                return PartyQuestRuntimeApplyPersistence::SaveAtomically(
                    sidecarPath,
                    acState);
            };
        }

        aSession.SetDurableStateHandler(
            [persist](const PartyQuestRuntimeRecoveryState& acState)
            {
                return persist(acState) ==
                    PartyQuestRuntimeApplyPersistenceStatus::Success;
            },
            persistenceGuarantee);

        const auto loaded = PartyQuestRuntimeApplyPersistence::Load(sidecarPath);
        if (loaded.Status == PartyQuestRuntimeApplyPersistenceStatus::FileNotFound)
        {
            return MakeStoreResult(
                PartyQuestRuntimeSessionStoreStatus::NewSession,
                loaded.Status);
        }
        if (loaded.Status ==
            PartyQuestRuntimeApplyPersistenceStatus::BackupRecoveryRequired)
        {
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::JournalRecoveryRequired,
                loaded.Status);
        }
        if (loaded.Status != PartyQuestRuntimeApplyPersistenceStatus::Success ||
            !loaded.State)
        {
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::JournalInvalid,
                loaded.Status);
        }

        const auto disposition = aSession.RestoreRecoveryState(*loaded.State);
        switch (disposition)
        {
        case PartyQuestRuntimeRecoveryDisposition::Clean:
            return MakeStoreResult(
                PartyQuestRuntimeSessionStoreStatus::Clean,
                loaded.Status,
                disposition);

        case PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired:
        {
            // The coordinator deliberately dropped the stale active repair. Make
            // that cleanup durable immediately through the exact same selected
            // writer as all later transitions. A strong bound session therefore
            // cannot hide a process-crash-only restart cleanup.
            const auto cleaned = aSession.GetCoordinator().ExportRecoveryState(
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId());
            const auto persisted = persist(cleaned);
            if (persisted != PartyQuestRuntimeApplyPersistenceStatus::Success)
            {
                return FailClosed(
                    aSession,
                    PartyQuestRuntimeSessionStoreStatus::CleanupPersistenceFailed,
                    persisted,
                    disposition);
            }

            return MakeStoreResult(
                PartyQuestRuntimeSessionStoreStatus::PreMutationRestarted,
                persisted,
                disposition);
        }

        case PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired:
            // Keep the durable handler bound: successful physical checkpoint
            // recovery must persist removal of this barrier through the same
            // player-scoped sidecar before the session can become usable.
            return MakeStoreResult(
                PartyQuestRuntimeSessionStoreStatus::RecoveryRequired,
                loaded.Status,
                disposition);

        case PartyQuestRuntimeRecoveryDisposition::CampaignMismatch:
        case PartyQuestRuntimeRecoveryDisposition::PlayerProfileMismatch:
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::JournalIdentityMismatch,
                loaded.Status,
                disposition);

        case PartyQuestRuntimeRecoveryDisposition::InvalidState:
            return FailClosed(
                aSession,
                PartyQuestRuntimeSessionStoreStatus::JournalInvalid,
                loaded.Status,
                disposition);
        }
    }
    catch (...)
    {
        return FailClosed(
            aSession,
            PartyQuestRuntimeSessionStoreStatus::JournalInvalid,
            PartyQuestRuntimeApplyPersistenceStatus::IoError);
    }

    return FailClosed(
        aSession,
        PartyQuestRuntimeSessionStoreStatus::JournalInvalid,
        PartyQuestRuntimeApplyPersistenceStatus::InvalidData);
}
} // namespace

PartyQuestRuntimeSessionStoreResult PartyQuestRuntimeSessionStore::BindAndLoad(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths) noexcept
{
    return BindAndLoadInternal(aSession, acPaths, nullptr);
}

PartyQuestRuntimeSessionStoreResult PartyQuestRuntimeSessionStore::BindAndLoad(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaWorkspacePublicationCapability& acCapability) noexcept
{
    return BindAndLoadInternal(aSession, acPaths, &acCapability);
}
