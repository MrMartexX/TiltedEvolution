#include <Structs/Skyrim/PartyQuestRuntimeSessionStore.h>

namespace
{
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
} // namespace

PartyQuestRuntimeSessionStoreResult PartyQuestRuntimeSessionStore::BindAndLoad(
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestCoopSavePaths& acPaths) noexcept
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

        const std::filesystem::path sidecarPath = acPaths.RuntimeApplySidecar;
        aSession.SetDurableStateHandler(
            [sidecarPath](const PartyQuestRuntimeRecoveryState& acState)
            {
                return PartyQuestRuntimeApplyPersistence::SaveAtomically(
                           sidecarPath,
                           acState) ==
                    PartyQuestRuntimeApplyPersistenceStatus::Success;
            },
            PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee);

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
            // that cleanup durable immediately so a second restart cannot load
            // and discard the same old pre-mutation entry again.
            const auto cleaned = aSession.GetCoordinator().ExportRecoveryState(
                aSession.GetCampaignId(),
                aSession.GetPlayerProfileId());
            const auto persisted = PartyQuestRuntimeApplyPersistence::SaveAtomically(
                sidecarPath,
                cleaned);
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
