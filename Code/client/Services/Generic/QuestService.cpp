#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Services/QuestService.h>
#include <Services/QuestSnapshotCollector.h>
#include <Services/ImguiService.h>

#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/PartyQuestMessages.h>

static TESQuest* FindQuestByNameId(const String& name)
{
    auto& questRegistry = ModManager::Get()->quests;
    auto it = std::find_if(questRegistry.begin(), questRegistry.end(), [name](auto* it) { return std::strcmp(it->idName.AsAscii(), name.c_str()); });

    return it != questRegistry.end() ? *it : nullptr;
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&QuestService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&QuestService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&QuestService::OnPartyLeft>(this);

    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);
    m_partyQuestTransactionResultConnection = aDispatcher.sink<NotifyPartyQuestTransactionResult>().connect<&QuestService::OnPartyQuestTransactionResult>(this);
    m_partyQuestRepairPlanConnection = aDispatcher.sink<NotifyPartyQuestRepairPlan>().connect<&QuestService::OnPartyQuestRepairPlan>(this);
    m_partyQuestCanonicalUpdateConnection = aDispatcher.sink<NotifyPartyQuestCanonicalUpdate>().connect<&QuestService::OnPartyQuestCanonicalUpdate>(this);

    // A note about the Gameevents:
    // TESQuestStageItemDoneEvent gets fired too late, we instead use TESQuestStageEvent, because it responds immediately.
    // TESQuestInitEvent can be instead managed by start stop quest management.
    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
}

uint64_t QuestService::AllocateScopedId(uint64_t& aSequence) noexcept
{
    if (aSequence == 0 || (aSequence & 0xFFFFFFFFull) == 0)
        aSequence = 1;

    const uint64_t id = (static_cast<uint64_t>(m_localPlayerId) << 32) | (aSequence & 0xFFFFFFFFull);
    ++aSequence;
    return id != 0 ? id : 1;
}

void QuestService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    const bool samePlayer = m_partyQuestSession && m_partyQuestSession->GetClientId() == acEvent.PlayerId;
    m_localPlayerId = acEvent.PlayerId;
    ++m_connectionGeneration;

    if (!samePlayer)
    {
        m_partyQuestSession.emplace(acEvent.PlayerId);
        m_nextRequestSequence = 1;
        m_nextReportSequence = 1;
        m_nextTransactionSequence = 1;
    }

    spdlog::info(
        "PartyQuestProtocol client connected: player={} generation={} retainedReplica={}",
        m_localPlayerId,
        m_connectionGeneration,
        samePlayer);

    // The legacy quest-selection behavior remains intentionally disabled.
}

void QuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    spdlog::info(
        "PartyQuestProtocol client disconnected: player={} retainedWorldRevision={}",
        m_localPlayerId,
        m_partyQuestSession ? m_partyQuestSession->GetReplica().GetWorldRevision() : 0);
}

void QuestService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    SendPartyQuestReplicaReport(m_connectionGeneration > 1, "party-joined");
}

void QuestService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    spdlog::info(
        "PartyQuestProtocol party left: player={} retainedWorldRevision={}",
        m_localPlayerId,
        m_partyQuestSession ? m_partyQuestSession->GetReplica().GetWorldRevision() : 0);
}

void QuestService::SendPartyQuestReplicaReport(bool aReconnect, const char* acReason) noexcept
{
    if (!m_partyQuestSession || m_localPlayerId == 0 || !m_world.GetPartyService().IsInParty())
        return;

    const uint64_t reportId = AllocateScopedId(m_nextReportSequence);
    RequestPartyQuestReplicaReport report = m_partyQuestSession->BuildReplicaReport(reportId, aReconnect);
    m_world.GetTransport().Send(report);

    spdlog::info(
        "PartyQuestProtocol report sent: reason={} player={} report={} reconnect={} worldRevision={} quests={}",
        acReason,
        m_localPlayerId,
        report.ReportId,
        report.IsReconnect,
        report.Report.WorldRevision,
        report.Report.Quests.size());
}

void QuestService::CollectLogAndSubmitPartyQuestSnapshot(uint32_t aFormId, const char* acReason) noexcept
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!pQuest)
        return;

    const auto snapshot = QuestSnapshotCollector::Collect(pQuest, m_world.GetModSystem());
    if (!snapshot)
        return;

    QuestSnapshotCollector::Log(pQuest, *snapshot, acReason);

    if (!m_partyQuestSession || m_localPlayerId == 0 || !m_world.GetPartyService().IsInParty())
        return;

    const QuestSnapshot* pKnownSnapshot = m_partyQuestSession->GetReplica().FindQuest(snapshot->QuestId);

    RequestPartyQuestTransaction request;
    request.RequestId = AllocateScopedId(m_nextRequestSequence);
    request.Transaction.TransactionId = AllocateScopedId(m_nextTransactionSequence);
    request.Transaction.InitiatorPlayerId = m_localPlayerId;
    request.Transaction.QuestId = snapshot->QuestId;
    request.Transaction.ExpectedQuestRevision = pKnownSnapshot ? pKnownSnapshot->Revision : 0;
    request.Transaction.ProposedSnapshot = *snapshot;

    m_world.GetTransport().Send(request);

    spdlog::info(
        "PartyQuestProtocol transaction sent: reason={} player={} request={} transaction={} quest={:016X} expectedRevision={} stage={} digest={:016X}",
        acReason,
        m_localPlayerId,
        request.RequestId,
        request.Transaction.TransactionId,
        request.Transaction.QuestId.LogFormat(),
        request.Transaction.ExpectedQuestRevision,
        request.Transaction.ProposedSnapshot.CurrentStage,
        request.Transaction.ProposedSnapshot.ComputeDigest());
}

void QuestService::OnPartyQuestTransactionResult(const NotifyPartyQuestTransactionResult& acResult) noexcept
{
    spdlog::info(
        "PartyQuestProtocol transaction result: request={} valid={} status={} worldRevision={} questRevision={} canonical={}",
        acResult.RequestId,
        acResult.IsValid,
        static_cast<unsigned>(acResult.Result.Status),
        acResult.Result.WorldRevision,
        acResult.Result.QuestRevision,
        acResult.CanonicalSnapshot.has_value());

    if (!acResult.IsValid)
        return;

    if (acResult.Result.Status == PartyQuestApplyStatus::RevisionMismatch ||
        acResult.Result.Status == PartyQuestApplyStatus::TransactionConflict ||
        acResult.Result.Status == PartyQuestApplyStatus::QuestIdMismatch)
    {
        SendPartyQuestReplicaReport(false, "transaction-rejected");
    }
}

void QuestService::OnPartyQuestRepairPlan(const NotifyPartyQuestRepairPlan& acPlan) noexcept
{
    if (!m_partyQuestSession || !m_world.GetPartyService().IsInParty())
        return;

    const PartyQuestClientRepairResult result = m_partyQuestSession->HandleRepairPlan(acPlan);
    if (result.Ack.IsValid)
        m_world.GetTransport().Send(result.Ack);

    spdlog::info(
        "PartyQuestProtocol repair plan: report={} plan={} valid={} planStatus={} items={} clientStatus={} ackStatus={} worldRevision={}",
        acPlan.ReportId,
        acPlan.PlanId,
        acPlan.IsValid,
        static_cast<unsigned>(acPlan.Plan.Status),
        acPlan.Plan.Items.size(),
        static_cast<unsigned>(result.Status),
        static_cast<unsigned>(result.Ack.ApplyStatus),
        result.Ack.PostApplyReport.WorldRevision);
}

void QuestService::OnPartyQuestCanonicalUpdate(const NotifyPartyQuestCanonicalUpdate& acUpdate) noexcept
{
    if (!m_partyQuestSession || !m_world.GetPartyService().IsInParty())
        return;

    const PartyQuestClientCanonicalStatus status = m_partyQuestSession->HandleCanonicalUpdate(acUpdate);
    spdlog::info(
        "PartyQuestProtocol canonical update: transaction={} valid={} status={} worldRevision={} quest={:016X} questRevision={} stage={} initiator={}",
        acUpdate.TransactionId,
        acUpdate.IsValid,
        static_cast<unsigned>(status),
        acUpdate.WorldRevision,
        acUpdate.CanonicalSnapshot.QuestId.LogFormat(),
        acUpdate.CanonicalSnapshot.Revision,
        acUpdate.CanonicalSnapshot.CurrentStage,
        acUpdate.InitiatorPlayerId);

    if (status == PartyQuestClientCanonicalStatus::RevisionGap ||
        status == PartyQuestClientCanonicalStatus::TransactionConflict)
    {
        SendPartyQuestReplicaReport(false, "canonical-gap");
    }
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest start/stop event: {:X}", apEvent->formId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(), pQuest->currentStage, pQuest->IsStopped() ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [this, formId = pQuest->formID, stageId = pQuest->currentStage, stopped = pQuest->IsStopped(), type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                }

                CollectLogAndSubmitPartyQuestSnapshot(formId, stopped ? "local-stop" : "local-start");
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest stage event: {:X}, stage: {}", apEvent->formId, apEvent->stageId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(), pQuest->currentStage,
                             RequestQuestUpdate::StageUpdate,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [this, formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = RequestQuestUpdate::StageUpdate;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                }

                CollectLogAndSubmitPartyQuestSnapshot(formId, "local-stage");
            });
    }

    return BSTEventResult::kOk;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    uint32_t formId = modSystem.GetGameId(aUpdate.Id);
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest)
    {
        spdlog::error("Failed to find quest, base id: {:X}, mod id: {:X}", aUpdate.Id.BaseId, aUpdate.Id.ModId);
        return;
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
    }

    bool bResult = false;
    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
    {
        pQuest->ScriptSetStage(aUpdate.Stage);
        pQuest->SetActive(true);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        pQuest->ScriptSetStage(aUpdate.Stage);
        bResult = true;
        spdlog::info("Remote quest updated: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default: break;
    }

    if (!bResult)
    {
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);
        return;
    }

    // This is the existing stage-only STR path. We only collect the resulting
    // state for diagnostics; the new canonical protocol does not apply it.
    TESQuest* pUpdatedQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (pUpdatedQuest)
    {
        const auto snapshot = QuestSnapshotCollector::Collect(pUpdatedQuest, m_world.GetModSystem());
        if (snapshot)
            QuestSnapshotCollector::Log(pUpdatedQuest, *snapshot, "remote-update");
    }
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        pQuest->SetActive(false);
        pQuest->SetStopped();
        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    // 0xFE014801,  // Unknown dynamic ID, kept as note, maybe lookup correct ID this game?
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    // Quests with no quest stages are never synced. Most TESQuest::Type quests should
    // be synced, including Type::None and Type::Miscellaneous, but there are a few
    // known exceptions that should be excluded that are in the table.
    return apQuest->stages.Empty()
        || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), apQuest->formID) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    auto& quests = ModManager::Get()->quests;
    for (TESQuest* pQuest : quests)
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, pQuest->idName.AsAscii());
}
