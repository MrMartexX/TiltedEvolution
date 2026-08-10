#include <GameServer.h>
#include <Components.h>

#include <World.h>
#include <Services/QuestService.h>

#include <Events/PlayerLeaveEvent.h>

#include <Game/Player.h>
#include <Game/PlayerManager.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/PartyQuestMessages.h>

#include <Structs/Skyrim/PartyQuestAdmission.h>
#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>
#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <Setting.h>

#include <system_error>
#include <utility>

namespace
{
Console::Setting bEnableMiscQuestSync{
    "Gameplay:bEnableMiscQuestSync",
    "(Experimental) Syncs miscellaneous quests when possible",
    false};

Console::Setting bEnablePartyQuestProtocolDiagnostics{
    "Gameplay:bEnablePartyQuestProtocolDiagnostics",
    "Enables the read-only equal-party quest protocol diagnostics. Does not apply Skyrim quest repairs or modify saves.",
    false};

Console::Setting bEnablePartyQuestStatePersistence{
    "Gameplay:bEnablePartyQuestStatePersistence",
    "Persists equal-party canonical quest state before accepted transactions are published.",
    true};

Console::Setting bEnablePartyQuestShadowPeerTest{
    "Gameplay:bEnablePartyQuestShadowPeerTest",
    "Runs an automatic server-local second replica test for missed-update and digest repair. Requires party quest diagnostics.",
    false};

Console::StringSetting sPartyQuestStatePath{
    "Gameplay:sPartyQuestStatePath",
    "Path to the equal-party canonical quest-state archive, relative to the server working directory unless absolute.",
    "state/party_quest_campaign.bin"};

const char* PersistenceStatusName(PartyQuestPersistenceStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestPersistenceStatus::Success: return "success";
    case PartyQuestPersistenceStatus::FileNotFound: return "file-not-found";
    case PartyQuestPersistenceStatus::IoError: return "io-error";
    case PartyQuestPersistenceStatus::InvalidMagic: return "invalid-magic";
    case PartyQuestPersistenceStatus::UnsupportedVersion: return "unsupported-version";
    case PartyQuestPersistenceStatus::Truncated: return "truncated";
    case PartyQuestPersistenceStatus::ChecksumMismatch: return "checksum-mismatch";
    case PartyQuestPersistenceStatus::InvalidData: return "invalid-data";
    case PartyQuestPersistenceStatus::ReplayMismatch: return "replay-mismatch";
    case PartyQuestPersistenceStatus::BackupRecoveryRequired: return "backup-recovery-required";
    }

    return "unknown";
}

const char* CampaignPersistenceStatusName(PartyQuestCampaignPersistenceStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestCampaignPersistenceStatus::Success: return "success";
    case PartyQuestCampaignPersistenceStatus::FileNotFound: return "file-not-found";
    case PartyQuestCampaignPersistenceStatus::IoError: return "io-error";
    case PartyQuestCampaignPersistenceStatus::InvalidMagic: return "invalid-magic";
    case PartyQuestCampaignPersistenceStatus::UnsupportedVersion: return "unsupported-version";
    case PartyQuestCampaignPersistenceStatus::Truncated: return "truncated";
    case PartyQuestCampaignPersistenceStatus::ChecksumMismatch: return "checksum-mismatch";
    case PartyQuestCampaignPersistenceStatus::InvalidData: return "invalid-data";
    }

    return "unknown";
}

const char* ShadowPeerFailureName(PartyQuestShadowPeerFailure aFailure) noexcept
{
    switch (aFailure)
    {
    case PartyQuestShadowPeerFailure::None: return "none";
    case PartyQuestShadowPeerFailure::InvalidCampaign: return "invalid-campaign";
    case PartyQuestShadowPeerFailure::ConnectFailed: return "connect-failed";
    case PartyQuestShadowPeerFailure::InitialSyncFailed: return "initial-sync-failed";
    case PartyQuestShadowPeerFailure::BaselineApplyFailed: return "baseline-apply-failed";
    case PartyQuestShadowPeerFailure::DisconnectFailed: return "disconnect-failed";
    case PartyQuestShadowPeerFailure::ReconnectFailed: return "reconnect-failed";
    case PartyQuestShadowPeerFailure::MissedUpdateRepairFailed: return "missed-update-repair-failed";
    case PartyQuestShadowPeerFailure::DigestMutationFailed: return "digest-mutation-failed";
    case PartyQuestShadowPeerFailure::DigestRepairFailed: return "digest-repair-failed";
    }

    return "unknown";
}

const char* AdmissionStatusName(PartyQuestAdmissionStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestAdmissionStatus::SharedProvisional: return "shared-provisional";
    case PartyQuestAdmissionStatus::BlockedServiceCandidate: return "blocked-service-candidate";
    case PartyQuestAdmissionStatus::BlockedLocalOnly: return "blocked-local-only";
    case PartyQuestAdmissionStatus::BlockedConfirmedServiceQuest: return "blocked-confirmed-service";
    }

    return "unknown";
}
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
    m_partyQuestTransactionConnection = aDispatcher.sink<PacketEvent<RequestPartyQuestTransaction>>().connect<&QuestService::OnPartyQuestTransaction>(this);
    m_partyQuestReplicaReportConnection = aDispatcher.sink<PacketEvent<RequestPartyQuestReplicaReport>>().connect<&QuestService::OnPartyQuestReplicaReport>(this);
    m_partyQuestRepairAckConnection = aDispatcher.sink<PacketEvent<RequestPartyQuestRepairAck>>().connect<&QuestService::OnPartyQuestRepairAck>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&QuestService::OnPlayerLeave>(this);

    m_partyQuestProtocolReady = InitializePartyQuestPersistence();
}

bool QuestService::InitializePartyQuestPersistence() noexcept
{
    m_partyQuestPersistenceEnabled = bEnablePartyQuestStatePersistence;
    if (!m_partyQuestPersistenceEnabled)
    {
        m_campaignId = PartyQuestCampaignPersistence::GenerateCampaignId();
        spdlog::warn(
            "PartyQuestProtocol persistence is disabled; campaign={:016X}{:016X} is ephemeral and canonical state will not survive a server restart",
            m_campaignId.High,
            m_campaignId.Low);
        return true;
    }

    m_partyQuestStatePath = std::filesystem::path(sPartyQuestStatePath.value());
    if (m_partyQuestStatePath.empty())
    {
        spdlog::error("PartyQuestProtocol persistence path is empty; protocol messages will be rejected");
        return false;
    }

    m_partyQuestCampaignIdPath = m_partyQuestStatePath;
    m_partyQuestCampaignIdPath += ".campaign-id";

    bool hadStateArchive = false;
    auto loadResult = PartyQuestStatePersistence::Load(m_partyQuestStatePath);
    if (loadResult.Status == PartyQuestPersistenceStatus::FileNotFound)
    {
        spdlog::info(
            "PartyQuestProtocol persistence: no state archive found at '{}'; validating campaign metadata before bootstrap",
            m_partyQuestStatePath.string());
    }
    else if (loadResult.Status != PartyQuestPersistenceStatus::Success || !loadResult.State)
    {
        spdlog::error(
            "PartyQuestProtocol persistence load failed: path='{}' status={}; protocol messages will be rejected to avoid overwriting recoverable state",
            m_partyQuestStatePath.string(),
            PersistenceStatusName(loadResult.Status));
        return false;
    }
    else
    {
        hadStateArchive = true;
        PartyQuestState restoredState = std::move(*loadResult.State);
        const uint64_t worldRevision = restoredState.GetWorldRevision();
        const size_t questCount = restoredState.GetQuestCount();
        const size_t journalEntries = restoredState.GetJournal().size();

        size_t quarantinedQuestCount = 0;
        for (const GameId& questId : PartyQuestAdmissionPolicy::GetConfirmedServiceQuestIds())
        {
            if (restoredState.FindQuest(questId))
                ++quarantinedQuestCount;
        }

        if (!m_partyQuestCoordinator.RestoreCanonicalState(std::move(restoredState)))
        {
            spdlog::error("PartyQuestProtocol could not restore canonical state before session initialization");
            return false;
        }

        spdlog::info(
            "PartyQuestProtocol persistence loaded: path='{}' worldRevision={} quests={} journalEntries={} usedTemporary={}",
            m_partyQuestStatePath.string(),
            worldRevision,
            questCount,
            journalEntries,
            loadResult.UsedTemporary);

        if (quarantinedQuestCount != 0)
        {
            spdlog::info(
                "PartyQuestProtocol admission migration: worldRevision={} storedQuests={} quarantinedConfirmedService={} sharedRepairSurface={} journalEntries={} historyPreserved=true",
                worldRevision,
                questCount,
                quarantinedQuestCount,
                questCount - quarantinedQuestCount,
                journalEntries);
        }
    }

    bool initializeCanonicalArchive = false;
    bool publishCampaignMetadata = false;
    if (!InitializePartyQuestCampaignIdentity(
            hadStateArchive,
            loadResult.CampaignId,
            initializeCanonicalArchive,
            publishCampaignMetadata))
        return false;

    if (initializeCanonicalArchive || (hadStateArchive && !loadResult.CampaignId))
    {
        const auto migrationStatus = PartyQuestStatePersistence::SaveAtomically(
            m_partyQuestStatePath,
            m_campaignId,
            m_partyQuestCoordinator.GetCanonicalState());
        if (migrationStatus != PartyQuestPersistenceStatus::Success)
        {
            spdlog::error(
                "PartyQuestProtocol could not bind legacy canonical state to campaign={:016X}{:016X}: path='{}' status={}; protocol messages will be rejected",
                m_campaignId.High,
                m_campaignId.Low,
                m_partyQuestStatePath.string(),
                PersistenceStatusName(migrationStatus));
            return false;
        }

        spdlog::info(
            "PartyQuestProtocol published campaign-bound canonical archive: campaign={:016X}{:016X} path='{}' initializedEmpty={} migratedLegacy={}",
            m_campaignId.High,
            m_campaignId.Low,
            m_partyQuestStatePath.string(),
            initializeCanonicalArchive,
            hadStateArchive && !loadResult.CampaignId);
    }

    if (publishCampaignMetadata)
    {
        const auto metadataStatus = PartyQuestCampaignPersistence::SaveAtomically(
            m_partyQuestCampaignIdPath,
            m_campaignId);
        if (metadataStatus != PartyQuestCampaignPersistenceStatus::Success)
        {
            spdlog::error(
                "PartyQuestProtocol could not publish archive-required campaign metadata '{}': status={}; protocol messages will be rejected",
                m_partyQuestCampaignIdPath.string(),
                CampaignPersistenceStatusName(metadataStatus));
            return false;
        }

        spdlog::info(
            "PartyQuestProtocol campaign metadata now requires canonical archive: campaign={:016X}{:016X} path='{}'",
            m_campaignId.High,
            m_campaignId.Low,
            m_partyQuestCampaignIdPath.string());
    }

    m_partyQuestCoordinator.SetDurableCommitHandler(
        [this](const PartyQuestState& acState)
        {
            return PersistPartyQuestState(acState);
        });

    return true;
}

bool QuestService::InitializePartyQuestCampaignIdentity(
    bool aHadStateArchive,
    const std::optional<PartyQuestCampaignId>& acEmbeddedCampaignId,
    bool& aInitializeCanonicalArchive,
    bool& aPublishCampaignMetadata) noexcept
{
    aInitializeCanonicalArchive = false;
    aPublishCampaignMetadata = false;
    auto loadResult = PartyQuestCampaignPersistence::Load(m_partyQuestCampaignIdPath);
    if (loadResult.Status == PartyQuestCampaignPersistenceStatus::FileNotFound)
    {
        m_campaignId = acEmbeddedCampaignId
            ? *acEmbeddedCampaignId
            : PartyQuestCampaignPersistence::GenerateCampaignId();
        aInitializeCanonicalArchive = !aHadStateArchive;
        aPublishCampaignMetadata = true;
        return true;
    }

    if (loadResult.Status != PartyQuestCampaignPersistenceStatus::Success || !loadResult.CampaignId)
    {
        spdlog::error(
            "PartyQuestProtocol campaign identity load failed: path='{}' status={}; protocol messages will be rejected to avoid attaching state to a different campaign",
            m_partyQuestCampaignIdPath.string(),
            CampaignPersistenceStatusName(loadResult.Status));
        return false;
    }

    m_campaignId = *loadResult.CampaignId;
    if (acEmbeddedCampaignId && *acEmbeddedCampaignId != m_campaignId)
    {
        spdlog::error(
            "PartyQuestProtocol campaign identity mismatch: metadata={:016X}{:016X} canonicalArchive={:016X}{:016X}; protocol messages will be rejected",
            m_campaignId.High,
            m_campaignId.Low,
            acEmbeddedCampaignId->High,
            acEmbeddedCampaignId->Low);
        return false;
    }
    if (loadResult.CanonicalArchiveRequired && !aHadStateArchive)
    {
        spdlog::error(
            "PartyQuestProtocol canonical state archive is missing but campaign metadata requires it: campaign={:016X}{:016X} statePath='{}'; protocol messages will be rejected",
            m_campaignId.High,
            m_campaignId.Low,
            m_partyQuestStatePath.string());
        return false;
    }

    aInitializeCanonicalArchive = !aHadStateArchive;
    aPublishCampaignMetadata =
        loadResult.UsedBackup ||
        loadResult.BackupRefreshRequired ||
        !loadResult.CanonicalArchiveRequired;

    spdlog::info(
        "PartyQuestProtocol campaign identity loaded: campaign={:016X}{:016X} path='{}' usedBackup={} canonicalArchiveRequired={} backupRefreshRequired={}",
        m_campaignId.High,
        m_campaignId.Low,
        m_partyQuestCampaignIdPath.string(),
        loadResult.UsedBackup,
        loadResult.CanonicalArchiveRequired,
        loadResult.BackupRefreshRequired);
    return true;
}

bool QuestService::PersistPartyQuestState(const PartyQuestState& acState) noexcept
{
    if (!m_partyQuestPersistenceEnabled)
        return true;

    const PartyQuestPersistenceStatus status =
        PartyQuestStatePersistence::SaveAtomically(
            m_partyQuestStatePath,
            m_campaignId,
            acState);
    if (status != PartyQuestPersistenceStatus::Success)
    {
        spdlog::error(
            "PartyQuestProtocol persistence save failed: path='{}' status={} candidateWorldRevision={}; canonical commit rejected",
            m_partyQuestStatePath.string(),
            PersistenceStatusName(status),
            acState.GetWorldRevision());
        return false;
    }

    spdlog::debug(
        "PartyQuestProtocol persistence saved: campaign={:016X}{:016X} path='{}' worldRevision={} quests={} journalEntries={}",
        m_campaignId.High,
        m_campaignId.Low,
        m_partyQuestStatePath.string(),
        acState.GetWorldRevision(),
        acState.GetQuestCount(),
        acState.GetJournal().size());
    return true;
}

bool QuestService::IsPartyActive(uint32_t aPartyId) const noexcept
{
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (!pPlayer)
            continue;

        const auto& party = pPlayer->GetParty();
        if (party.JoinedPartyId && *party.JoinedPartyId == aPartyId)
            return true;
    }

    return false;
}

void QuestService::MaybeStartPartyQuestShadowPeer() noexcept
{
    if (!bEnablePartyQuestShadowPeerTest ||
        m_partyQuestShadowPeer.GetState() != PartyQuestShadowPeerState::Idle)
    {
        return;
    }

    if (!m_partyQuestShadowPeer.Start(m_partyQuestCoordinator, m_campaignId))
    {
        spdlog::error(
            "PartyQuestShadowPeer TEST FAIL during startup: campaign={:016X}{:016X} failure={}",
            m_campaignId.High,
            m_campaignId.Low,
            ShadowPeerFailureName(m_partyQuestShadowPeer.GetFailure()));
        return;
    }

    const auto& metrics = m_partyQuestShadowPeer.GetMetrics();
    spdlog::info(
        "PartyQuestShadowPeer TEST START: campaign={:016X}{:016X} syntheticPlayer={} worldRevision={} initialRepairItems={} initialRemovals={} missing={} revisionMismatch={} digestMismatch={} clientOnly={}; advance quests normally, the test will finish automatically after two accepted canonical updates",
        m_campaignId.High,
        m_campaignId.Low,
        PartyQuestShadowPeerHarness::kClientId,
        metrics.StartWorldRevision,
        metrics.InitialSyncSummary.RepairItemCount(),
        metrics.InitialSyncSummary.QuarantinedQuestRemovalCount,
        metrics.InitialSyncSummary.MissingQuestCount,
        metrics.InitialSyncSummary.RevisionMismatchCount,
        metrics.InitialSyncSummary.DigestMismatchCount,
        metrics.InitialSyncSummary.ClientOnlyQuestCount);
}

void QuestService::HandlePartyQuestShadowPeerCanonicalUpdate(
    const NotifyPartyQuestCanonicalUpdate& acUpdate) noexcept
{
    if (!bEnablePartyQuestShadowPeerTest)
        return;

    const PartyQuestShadowPeerState before = m_partyQuestShadowPeer.GetState();
    if (before == PartyQuestShadowPeerState::Idle ||
        before == PartyQuestShadowPeerState::Passed ||
        before == PartyQuestShadowPeerState::Failed)
    {
        return;
    }

    m_partyQuestShadowPeer.HandleCanonicalUpdate(m_partyQuestCoordinator, acUpdate);
    const PartyQuestShadowPeerState after = m_partyQuestShadowPeer.GetState();
    const auto& metrics = m_partyQuestShadowPeer.GetMetrics();

    if (after == PartyQuestShadowPeerState::Failed)
    {
        spdlog::error(
            "PartyQuestShadowPeer TEST FAIL: campaign={:016X}{:016X} failure={} baselineWorldRevision={} missedWorldRevision={} canonicalWorldRevision={}",
            m_campaignId.High,
            m_campaignId.Low,
            ShadowPeerFailureName(m_partyQuestShadowPeer.GetFailure()),
            metrics.BaselineWorldRevision,
            metrics.MissedWorldRevision,
            m_partyQuestCoordinator.GetCanonicalState().GetWorldRevision());
        return;
    }

    if (before == PartyQuestShadowPeerState::WaitingForBaseline &&
        after == PartyQuestShadowPeerState::WaitingForMissedUpdate)
    {
        spdlog::info(
            "PartyQuestShadowPeer STEP 1 PASS: baseline canonical update applied at worldRevision={}; synthetic peer is now deliberately disconnected so the next accepted update is missed",
            metrics.BaselineWorldRevision);
        return;
    }

    if (after == PartyQuestShadowPeerState::Passed)
    {
        const auto& missed = metrics.MissedUpdateRepairSummary;
        const auto& digest = metrics.DigestRepairSummary;
        spdlog::info(
            "PartyQuestShadowPeer TEST PASS: campaign={:016X}{:016X} baselineWorldRevision={} missedWorldRevision={} finalWorldRevision={} missedRepairItems={} missedRemovals={} missing={} revisionMismatch={} digestMismatch={} digestRepairItems={} digestRepairRemovals={} digestRepairDigestMismatch={} shadowPeerDisconnected={}",
            m_campaignId.High,
            m_campaignId.Low,
            metrics.BaselineWorldRevision,
            metrics.MissedWorldRevision,
            metrics.FinalWorldRevision,
            missed.RepairItemCount(),
            missed.QuarantinedQuestRemovalCount,
            missed.MissingQuestCount,
            missed.RevisionMismatchCount,
            missed.DigestMismatchCount,
            digest.RepairItemCount(),
            digest.QuarantinedQuestRemovalCount,
            digest.DigestMismatchCount,
            !m_partyQuestCoordinator.IsClientConnected(PartyQuestShadowPeerHarness::kClientId));
    }
}

bool QuestService::PreparePartyQuestClient(Player* apPlayer, uint32_t& aPartyId) noexcept
{
    if (!bEnablePartyQuestProtocolDiagnostics || !apPlayer)
        return false;

    if (!m_partyQuestProtocolReady)
    {
        spdlog::warn(
            "PartyQuestProtocol: rejected player {} because canonical persistence initialization failed",
            apPlayer->GetId());
        return false;
    }

    const auto& partyComponent = apPlayer->GetParty();
    if (!partyComponent.JoinedPartyId)
    {
        spdlog::warn("PartyQuestProtocol: player {} sent a protocol message outside a party", apPlayer->GetId());
        return false;
    }

    aPartyId = *partyComponent.JoinedPartyId;
    if (!m_campaignPartyId)
    {
        m_campaignPartyId = aPartyId;
        spdlog::info(
            "PartyQuestProtocol: bound campaign={:016X}{:016X} to active party {} at worldRevision {}",
            m_campaignId.High,
            m_campaignId.Low,
            aPartyId,
            m_partyQuestCoordinator.GetCanonicalState().GetWorldRevision());
    }
    else if (*m_campaignPartyId != aPartyId)
    {
        const uint32_t previousPartyId = *m_campaignPartyId;
        if (IsPartyActive(previousPartyId))
        {
            spdlog::warn(
                "PartyQuestProtocol: rejected player {} from party {}; campaign={:016X}{:016X} currently has active party {}",
                apPlayer->GetId(),
                aPartyId,
                m_campaignId.High,
                m_campaignId.Low,
                previousPartyId);
            return false;
        }

        m_campaignPartyId = aPartyId;
        spdlog::info(
            "PartyQuestProtocol: rebound campaign={:016X}{:016X} from inactive party {} to recreated party {} at worldRevision {}",
            m_campaignId.High,
            m_campaignId.Low,
            previousPartyId,
            aPartyId,
            m_partyQuestCoordinator.GetCanonicalState().GetWorldRevision());
    }

    if (!m_partyQuestCoordinator.IsClientConnected(apPlayer->GetId()))
        m_partyQuestCoordinator.ConnectClient(apPlayer->GetId());

    MaybeStartPartyQuestShadowPeer();
    return true;
}

void QuestService::SendCanonicalUpdateToCampaign(
    const NotifyPartyQuestCanonicalUpdate& acUpdate,
    const std::vector<uint32_t>& acRecipients,
    uint32_t aPartyId) const noexcept
{
    auto& playerManager = m_world.GetPlayerManager();
    for (uint32_t playerId : acRecipients)
    {
        Player* pRecipient = playerManager.GetById(playerId);
        if (!pRecipient)
            continue;

        const auto& party = pRecipient->GetParty();
        if (!party.JoinedPartyId || *party.JoinedPartyId != aPartyId)
            continue;

        pRecipient->Send(acUpdate);
    }
}

void QuestService::OnPartyQuestTransaction(const PacketEvent<RequestPartyQuestTransaction>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    uint32_t partyId{};
    if (!PreparePartyQuestClient(pPlayer, partyId))
        return;

    PartyQuestAdmissionDecision admission;
    bool hasAdmissionDecision = false;
    if (acMessage.Packet.IsValid)
    {
        admission = PartyQuestAdmissionPolicy::Evaluate(
            acMessage.Packet.Transaction.QuestId,
            acMessage.Packet.SyncFacts);
        hasAdmissionDecision = true;

        if (!admission.IsAdmitted())
        {
            NotifyPartyQuestTransactionResult response;
            response.RequestId = acMessage.Packet.RequestId;
            response.Result = {
                PartyQuestApplyStatus::AdmissionRejected,
                m_partyQuestCoordinator.GetCanonicalState().GetWorldRevision(),
                0};
            pPlayer->Send(response);

            spdlog::info(
                "PartyQuestProtocol admission rejected: campaign={:016X}{:016X} party={} player={} request={} transaction={} quest={:016X} admission={} syncClass={} syncReason={} questType={} hasStages={} hud={} named={} worldRevision={}",
                m_campaignId.High,
                m_campaignId.Low,
                partyId,
                pPlayer->GetId(),
                acMessage.Packet.RequestId,
                acMessage.Packet.Transaction.TransactionId,
                acMessage.Packet.Transaction.QuestId.LogFormat(),
                AdmissionStatusName(admission.Status),
                static_cast<uint8_t>(admission.Classification.Class),
                static_cast<uint8_t>(admission.Classification.Reason),
                acMessage.Packet.SyncFacts.QuestType,
                acMessage.Packet.SyncFacts.HasStages,
                acMessage.Packet.SyncFacts.IsDisplayedInHud,
                acMessage.Packet.SyncFacts.HasDisplayName,
                response.Result.WorldRevision);
            return;
        }
    }

    const auto dispatch = m_partyQuestCoordinator.HandleTransaction(pPlayer->GetId(), acMessage.Packet);

    if (dispatch.Response.RequestId != 0)
        pPlayer->Send(dispatch.Response);

    if (dispatch.Broadcast)
    {
        SendCanonicalUpdateToCampaign(*dispatch.Broadcast, dispatch.Recipients, partyId);
        HandlePartyQuestShadowPeerCanonicalUpdate(*dispatch.Broadcast);
    }

    spdlog::info(
        "PartyQuestProtocol transaction: campaign={:016X}{:016X} party={} player={} request={} transaction={} status={} apply={} admission={} worldRevision={} questRevision={} broadcastRecipients={}",
        m_campaignId.High,
        m_campaignId.Low,
        partyId,
        pPlayer->GetId(),
        acMessage.Packet.RequestId,
        acMessage.Packet.Transaction.TransactionId,
        static_cast<uint8_t>(dispatch.Status),
        static_cast<uint8_t>(dispatch.Response.Result.Status),
        hasAdmissionDecision ? AdmissionStatusName(admission.Status) : "invalid-message",
        dispatch.Response.Result.WorldRevision,
        dispatch.Response.Result.QuestRevision,
        dispatch.Recipients.size());
}

void QuestService::OnPartyQuestReplicaReport(const PacketEvent<RequestPartyQuestReplicaReport>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    uint32_t partyId{};
    if (!PreparePartyQuestClient(pPlayer, partyId))
        return;

    const PartyQuestCampaignId clientCampaignId = acMessage.Packet.CampaignId;
    const bool campaignMismatch = clientCampaignId.IsValid() && clientCampaignId != m_campaignId;

    RequestPartyQuestReplicaReport request = acMessage.Packet;
    if (campaignMismatch)
    {
        spdlog::warn(
            "PartyQuestProtocol report campaign mismatch: player={} clientCampaign={:016X}{:016X} serverCampaign={:016X}{:016X}; planning repair from an empty replica",
            pPlayer->GetId(),
            clientCampaignId.High,
            clientCampaignId.Low,
            m_campaignId.High,
            m_campaignId.Low);
        request.CampaignId = m_campaignId;
        request.Report = {};
    }

    auto dispatch = m_partyQuestCoordinator.HandleReplicaReport(pPlayer->GetId(), request);
    if (dispatch.Response)
    {
        dispatch.Response->CampaignId = m_campaignId;
        pPlayer->Send(*dispatch.Response);
    }

    spdlog::info(
        "PartyQuestProtocol report: campaign={:016X}{:016X} clientCampaign={:016X}{:016X} campaignMismatch={} party={} player={} report={} reconnect={} status={} clientWorldRevision={} plan={} planStatus={} items={} removals={}",
        m_campaignId.High,
        m_campaignId.Low,
        clientCampaignId.High,
        clientCampaignId.Low,
        campaignMismatch,
        partyId,
        pPlayer->GetId(),
        acMessage.Packet.ReportId,
        acMessage.Packet.IsReconnect,
        static_cast<uint8_t>(dispatch.Status),
        acMessage.Packet.Report.WorldRevision,
        dispatch.Response ? dispatch.Response->PlanId : 0,
        dispatch.Response ? static_cast<uint8_t>(dispatch.Response->Plan.Status) : 0,
        dispatch.Response ? dispatch.Response->Plan.Items.size() : 0,
        dispatch.Response ? dispatch.Response->Plan.RemovedQuestIds.size() : 0);
}

void QuestService::OnPartyQuestRepairAck(const PacketEvent<RequestPartyQuestRepairAck>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    uint32_t partyId{};
    if (!PreparePartyQuestClient(pPlayer, partyId))
        return;

    const auto result = m_partyQuestCoordinator.HandleRepairAck(pPlayer->GetId(), acMessage.Packet);
    spdlog::info(
        "PartyQuestProtocol repair ack: campaign={:016X}{:016X} party={} player={} plan={} applyStatus={} status={} verification={}",
        m_campaignId.High,
        m_campaignId.Low,
        partyId,
        pPlayer->GetId(),
        acMessage.Packet.PlanId,
        static_cast<uint8_t>(acMessage.Packet.ApplyStatus),
        static_cast<uint8_t>(result.Status),
        static_cast<uint8_t>(result.VerificationStatus));
}

void QuestService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (acEvent.pPlayer)
        m_partyQuestCoordinator.DisconnectClient(acEvent.pPlayer->GetId());
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;

    auto* pPlayer = acMessage.pPlayer;

    auto& questComponent = pPlayer->GetQuestLogComponent();
    auto& entries = questComponent.QuestContent.Entries;

    auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& e) { return e.Id == message.Id; });

    NotifyQuestUpdate notify{};
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    notify.Status = message.Status;
    notify.ClientQuestType = message.ClientQuestType;

    if (notify.ClientQuestType == 0 || notify.ClientQuestType == 6) // Types None or Miscellaneous. Hard-coded to avoid client header file.
    {
        if (!bEnableMiscQuestSync)
            return;
        spdlog::info("{}: syncing type none/misc quest to party, gameId {:X} questStage {} questStatus {} questType {}",
                     __FUNCTION__, notify.Id.LogFormat(), message.Stage, message.Status, message.ClientQuestType);
    }

    if (message.Status == RequestQuestUpdate::Started || message.Status == RequestQuestUpdate::StageUpdate)
    {
        // in order to prevent bugs when a quest is in progress
        // and being updated we add it as a new quest record to
        // maintain a proper remote questlog state.
        if (questIt == entries.end())
        {
            auto& newQuest = entries.emplace_back();
            newQuest.Id = message.Id;
            newQuest.Stage = message.Stage;

            if (message.Status == RequestQuestUpdate::Started)
            {
                spdlog::debug("Started quest: {:X} stage: {}", message.Id.LogFormat(), message.Stage);

                notify.Status = NotifyQuestUpdate::Started;
            }
            else
            {
                notify.Status = NotifyQuestUpdate::StageUpdate;
            }
        }
        else
        {
            spdlog::debug("Updated quest: {:X}, stage: {}", message.Id.LogFormat(), message.Stage);

            auto& record = *questIt;
            record.Id = message.Id;
            record.Stage = message.Stage;

            notify.Status = NotifyQuestUpdate::StageUpdate;
        }
    }
    else if (message.Status == RequestQuestUpdate::Stopped)
    {
        spdlog::debug("Stopped quest: {:X}, stage: {}", message.Id.LogFormat(), message.Stage);

        if (questIt != entries.end())
            entries.erase(questIt);
        else
            spdlog::warn("Unable to delete quest object {:X}", message.Id.LogFormat());

        notify.Status = NotifyQuestUpdate::Stopped;
    }

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    if (!partyComponent.JoinedPartyId.has_value())
        return;

    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}
