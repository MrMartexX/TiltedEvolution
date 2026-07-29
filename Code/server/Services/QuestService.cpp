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
        spdlog::warn("PartyQuestProtocol persistence is disabled; accepted canonical state will not survive a server restart");
        return true;
    }

    m_partyQuestStatePath = std::filesystem::path(sPartyQuestStatePath.value());
    if (m_partyQuestStatePath.empty())
    {
        spdlog::error("PartyQuestProtocol persistence path is empty; protocol messages will be rejected");
        return false;
    }

    auto loadResult = PartyQuestStatePersistence::Load(m_partyQuestStatePath);
    if (loadResult.Status == PartyQuestPersistenceStatus::FileNotFound)
    {
        spdlog::info(
            "PartyQuestProtocol persistence: no archive found at '{}'; starting a new canonical campaign",
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
        PartyQuestState restoredState = std::move(*loadResult.State);
        const uint64_t worldRevision = restoredState.GetWorldRevision();
        const size_t questCount = restoredState.GetQuestCount();
        const size_t journalEntries = restoredState.GetJournal().size();

        if (loadResult.UsedBackup)
        {
            std::error_code removeError;
            std::filesystem::remove(m_partyQuestStatePath, removeError);
            if (removeError)
            {
                spdlog::error(
                    "PartyQuestProtocol recovered a backup but could not remove the invalid primary archive '{}': {}; protocol messages will be rejected",
                    m_partyQuestStatePath.string(),
                    removeError.message());
                return false;
            }

            const auto healStatus = PartyQuestStatePersistence::SaveAtomically(m_partyQuestStatePath, restoredState);
            if (healStatus != PartyQuestPersistenceStatus::Success)
            {
                spdlog::error(
                    "PartyQuestProtocol recovered a backup but could not restore the primary archive '{}': status={}; protocol messages will be rejected",
                    m_partyQuestStatePath.string(),
                    PersistenceStatusName(healStatus));
                return false;
            }
        }

        if (!m_partyQuestCoordinator.RestoreCanonicalState(std::move(restoredState)))
        {
            spdlog::error("PartyQuestProtocol could not restore canonical state before session initialization");
            return false;
        }

        spdlog::info(
            "PartyQuestProtocol persistence loaded: path='{}' worldRevision={} quests={} journalEntries={} usedBackup={}",
            m_partyQuestStatePath.string(),
            worldRevision,
            questCount,
            journalEntries,
            loadResult.UsedBackup);
    }

    m_partyQuestCoordinator.SetDurableCommitHandler(
        [this](const PartyQuestState& acState)
        {
            return PersistPartyQuestState(acState);
        });

    return true;
}

bool QuestService::PersistPartyQuestState(const PartyQuestState& acState) noexcept
{
    if (!m_partyQuestPersistenceEnabled)
        return true;

    const PartyQuestPersistenceStatus status =
        PartyQuestStatePersistence::SaveAtomically(m_partyQuestStatePath, acState);
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
        "PartyQuestProtocol persistence saved: path='{}' worldRevision={} quests={} journalEntries={}",
        m_partyQuestStatePath.string(),
        acState.GetWorldRevision(),
        acState.GetQuestCount(),
        acState.GetJournal().size());
    return true;
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
            "PartyQuestProtocol: bound the server campaign to party {} at worldRevision {}",
            aPartyId,
            m_partyQuestCoordinator.GetCanonicalState().GetWorldRevision());
    }
    else if (*m_campaignPartyId != aPartyId)
    {
        spdlog::warn(
            "PartyQuestProtocol: rejected player {} from party {}; this server campaign is bound to party {}",
            apPlayer->GetId(),
            aPartyId,
            *m_campaignPartyId);
        return false;
    }

    if (!m_partyQuestCoordinator.IsClientConnected(apPlayer->GetId()))
        m_partyQuestCoordinator.ConnectClient(apPlayer->GetId());

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

    const auto dispatch = m_partyQuestCoordinator.HandleTransaction(pPlayer->GetId(), acMessage.Packet);

    if (dispatch.Response.RequestId != 0)
        pPlayer->Send(dispatch.Response);

    if (dispatch.Broadcast)
        SendCanonicalUpdateToCampaign(*dispatch.Broadcast, dispatch.Recipients, partyId);

    spdlog::info(
        "PartyQuestProtocol transaction: player={} request={} transaction={} status={} apply={} worldRevision={} questRevision={} broadcastRecipients={}",
        pPlayer->GetId(),
        acMessage.Packet.RequestId,
        acMessage.Packet.Transaction.TransactionId,
        static_cast<uint8_t>(dispatch.Status),
        static_cast<uint8_t>(dispatch.Response.Result.Status),
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

    const auto dispatch = m_partyQuestCoordinator.HandleReplicaReport(pPlayer->GetId(), acMessage.Packet);
    if (dispatch.Response)
        pPlayer->Send(*dispatch.Response);

    spdlog::info(
        "PartyQuestProtocol report: player={} report={} reconnect={} status={} clientWorldRevision={} plan={} planStatus={} items={}",
        pPlayer->GetId(),
        acMessage.Packet.ReportId,
        acMessage.Packet.IsReconnect,
        static_cast<uint8_t>(dispatch.Status),
        acMessage.Packet.Report.WorldRevision,
        dispatch.Response ? dispatch.Response->PlanId : 0,
        dispatch.Response ? static_cast<uint8_t>(dispatch.Response->Plan.Status) : 0,
        dispatch.Response ? dispatch.Response->Plan.Items.size() : 0);
}

void QuestService::OnPartyQuestRepairAck(const PacketEvent<RequestPartyQuestRepairAck>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    uint32_t partyId{};
    if (!PreparePartyQuestClient(pPlayer, partyId))
        return;

    const auto result = m_partyQuestCoordinator.HandleRepairAck(pPlayer->GetId(), acMessage.Packet);
    spdlog::info(
        "PartyQuestProtocol repair ack: player={} plan={} applyStatus={} status={} verification={}",
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
                     __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.Status, notify.ClientQuestType);
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
