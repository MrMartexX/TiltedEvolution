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

void QuestService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    const bool retainedReplica = m_partyQuestSession.has_value();
    const uint32_t previousPlayerId = m_partyQuestSession ? m_partyQuestSession->GetClientId() : 0;

    m_localPlayerId = acEvent.PlayerId;
    ++m_connectionGeneration;
    m_partyQuestProtocolVerified = false;
    m_partyQuestSubmissions.RequeueInFlight();
    m_requestTransactions.clear();

    if (!m_partyQuestSession)
    {
        m_partyQuestSession.emplace(acEvent.PlayerId);
        m_partyQuestSubmissions.Clear();
    }
    else if (!m_partyQuestSession->RebindClientId(acEvent.PlayerId))
    {
        spdlog::error("PartyQuestProtocol could not rebind retained client replica to player={}", acEvent.PlayerId);
        m_partyQuestSession.emplace(acEvent.PlayerId);
        m_partyQuestSubmissions.Clear();
    }

    const PartyQuestCampaignId campaignId = m_partyQuestSession->GetCampaignId();
    spdlog::info(
        "PartyQuestProtocol client connected: player={} previousPlayer={} generation={} retainedReplica={} retainedCampaign={:016X}{:016X} retainedWorldRevision={} idNamespace={:016X} queuedSubmissions={}",
        m_localPlayerId,
        previousPlayerId,
        m_connectionGeneration,
        retainedReplica,
        campaignId.High,
        campaignId.Low,
        m_partyQuestSession->GetReplica().GetWorldRevision(),
        m_partyQuestIds.GetNamespace(),
        m_partyQuestSubmissions.GetQueuedCount());

    // The legacy quest-selection behavior remains intentionally disabled.
}

void QuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_partyQuestProtocolVerified = false;
    m_partyQuestSubmissions.RequeueInFlight();
    m_requestTransactions.clear();

    spdlog::info(
        "PartyQuestProtocol client disconnected: player={} retainedWorldRevision={} queuedSubmissions={}",
        m_localPlayerId,
        m_partyQuestSession ? m_partyQuestSession->GetReplica().GetWorldRevision() : 0,
        m_partyQuestSubmissions.GetQueuedCount());
}

void QuestService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    m_partyQuestProtocolVerified = false;
    SendPartyQuestReplicaReport(m_connectionGeneration > 1, "party-joined");
}

void QuestService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_partyQuestProtocolVerified = false;
    m_partyQuestSubmissions.RequeueInFlight();
    m_requestTransactions.clear();

    spdlog::info(
        "PartyQuestProtocol party left: player={} retainedWorldRevision={} queuedSubmissions={}",
        m_localPlayerId,
        m_partyQuestSession ? m_partyQuestSession->GetReplica().GetWorldRevision() : 0,
        m_partyQuestSubmissions.GetQueuedCount());
}

void QuestService::SendPartyQuestReplicaReport(bool aReconnect, const char* acReason) noexcept
{
    if (!m_partyQuestSession || m_localPlayerId == 0 || !m_world.GetPartyService().IsInParty())
        return;

    const uint64_t reportId = m_partyQuestIds.Allocate();
    RequestPartyQuestReplicaReport report = m_partyQuestSession->BuildReplicaReport(reportId, aReconnect);
    m_world.GetTransport().Send(report);

    spdlog::info(
        "PartyQuestProtocol report sent: reason={} player={} report={} campaign={:016X}{:016X} reconnect={} worldRevision={} quests={} queuedSubmissions={}",
        acReason,
        m_localPlayerId,
        report.ReportId,
        report.CampaignId.High,
        report.CampaignId.Low,
        report.IsReconnect,
        report.Report.WorldRevision,
        report.Report.Quests.size(),
        m_partyQuestSubmissions.GetQueuedCount());
}

void QuestService::SubmitPartyQuestSnapshot(const QuestSnapshot& acSnapshot, const char* acReason) noexcept
{
    if (!m_partyQuestProtocolVerified || !m_partyQuestSession || m_localPlayerId == 0 ||
        !m_world.GetPartyService().IsInParty())
    {
        return;
    }

    const QuestSnapshot* pKnownSnapshot = m_partyQuestSession->GetReplica().FindQuest(acSnapshot.QuestId);

    RequestPartyQuestTransaction request;
    request.RequestId = m_partyQuestIds.Allocate();
    request.Transaction.TransactionId = m_partyQuestIds.Allocate();
    request.Transaction.InitiatorPlayerId = m_localPlayerId;
    request.Transaction.QuestId = acSnapshot.QuestId;
    request.Transaction.ExpectedQuestRevision = pKnownSnapshot ? pKnownSnapshot->Revision : 0;
    request.Transaction.ProposedSnapshot = acSnapshot;

    if (!m_partyQuestSubmissions.MarkInFlight(request.Transaction.TransactionId, acSnapshot))
    {
        spdlog::warn(
            "PartyQuestProtocol transaction not sent: quest={:016X} transaction={} already has an in-flight submission",
            acSnapshot.QuestId.LogFormat(),
            request.Transaction.TransactionId);
        return;
    }

    m_requestTransactions.emplace(request.RequestId, request.Transaction.TransactionId);
    m_world.GetTransport().Send(request);

    spdlog::info(
        "PartyQuestProtocol transaction sent: reason={} player={} request={} transaction={} quest={:016X} expectedRevision={} stage={} digest={:016X} inFlight={} queued={}",
        acReason,
        m_localPlayerId,
        request.RequestId,
        request.Transaction.TransactionId,
        request.Transaction.QuestId.LogFormat(),
        request.Transaction.ExpectedQuestRevision,
        request.Transaction.ProposedSnapshot.CurrentStage,
        request.Transaction.ProposedSnapshot.ComputeDigest(),
        m_partyQuestSubmissions.GetInFlightCount(),
        m_partyQuestSubmissions.GetQueuedCount());
}

void QuestService::FlushQueuedPartyQuestSnapshots(const char* acReason) noexcept
{
    if (!m_partyQuestProtocolVerified || !m_partyQuestSession || m_localPlayerId == 0 ||
        !m_world.GetPartyService().IsInParty())
    {
        return;
    }

    auto ready = m_partyQuestSubmissions.TakeReady(m_partyQuestSession->GetReplica());
    if (!ready.empty())
    {
        spdlog::info(
            "PartyQuestProtocol flushing queued snapshots: reason={} count={}",
            acReason,
            ready.size());
    }

    for (const QuestSnapshot& snapshot : ready)
        SubmitPartyQuestSnapshot(snapshot, acReason);
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

    if (!m_partyQuestProtocolVerified)
    {
        const PartyQuestClientSubmissionStatus status = m_partyQuestSubmissions.QueueLatest(*snapshot);
        if (status != PartyQuestClientSubmissionStatus::Duplicate)
        {
            spdlog::info(
                "PartyQuestProtocol snapshot deferred pending campaign verification: reason={} quest={:016X} stage={} status={} queued={}",
                acReason,
                snapshot->QuestId.LogFormat(),
                snapshot->CurrentStage,
                static_cast<unsigned>(status),
                m_partyQuestSubmissions.GetQueuedCount());
        }
        return;
    }

    const PartyQuestClientSubmissionDecision decision =
        m_partyQuestSubmissions.Observe(*snapshot, m_partyQuestSession->GetReplica());

    switch (decision.Status)
    {
    case PartyQuestClientSubmissionStatus::Ready:
        if (decision.ReadySnapshot)
            SubmitPartyQuestSnapshot(*decision.ReadySnapshot, acReason);
        break;
    case PartyQuestClientSubmissionStatus::Queued:
    case PartyQuestClientSubmissionStatus::ReplacedQueued:
        spdlog::info(
            "PartyQuestProtocol snapshot coalesced: reason={} quest={:016X} stage={} status={} queued={}",
            acReason,
            snapshot->QuestId.LogFormat(),
            snapshot->CurrentStage,
            static_cast<unsigned>(decision.Status),
            m_partyQuestSubmissions.GetQueuedCount());
        break;
    case PartyQuestClientSubmissionStatus::Duplicate:
        spdlog::debug(
            "PartyQuestProtocol duplicate snapshot suppressed: reason={} quest={:016X} stage={}",
            acReason,
            snapshot->QuestId.LogFormat(),
            snapshot->CurrentStage);
        break;
    case PartyQuestClientSubmissionStatus::InvalidSnapshot:
        spdlog::warn("PartyQuestProtocol invalid local snapshot suppressed: reason={}", acReason);
        break;
    }
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

    uint64_t transactionId{};
    const auto requestIt = m_requestTransactions.find(acResult.RequestId);
    if (requestIt != m_requestTransactions.end())
    {
        transactionId = requestIt->second;
        m_requestTransactions.erase(requestIt);
    }

    if (!acResult.IsValid)
    {
        if (transactionId != 0)
            m_partyQuestSubmissions.Reject(transactionId);
        m_partyQuestProtocolVerified = false;
        SendPartyQuestReplicaReport(false, "invalid-transaction-result");
        return;
    }

    if (acResult.Result.Status == PartyQuestApplyStatus::Accepted)
        return;

    if (transactionId != 0)
        m_partyQuestSubmissions.Reject(transactionId);

    if (acResult.Result.Status == PartyQuestApplyStatus::Duplicate ||
        acResult.Result.Status == PartyQuestApplyStatus::RevisionMismatch ||
        acResult.Result.Status == PartyQuestApplyStatus::TransactionConflict ||
        acResult.Result.Status == PartyQuestApplyStatus::QuestIdMismatch ||
        acResult.Result.Status == PartyQuestApplyStatus::InvalidTransactionId)
    {
        m_partyQuestProtocolVerified = false;
        SendPartyQuestReplicaReport(false, "transaction-rejected");
    }
}

void QuestService::OnPartyQuestRepairPlan(const NotifyPartyQuestRepairPlan& acPlan) noexcept
{
    if (!m_partyQuestSession || !m_world.GetPartyService().IsInParty())
        return;

    const PartyQuestClientRepairResult result = m_partyQuestSession->HandleRepairPlan(acPlan);
    if (result.CampaignChanged)
    {
        m_partyQuestSubmissions.Clear();
        m_requestTransactions.clear();
    }

    if (result.Ack.IsValid)
        m_world.GetTransport().Send(result.Ack);

    spdlog::info(
        "PartyQuestProtocol repair plan: report={} plan={} campaign={:016X}{:016X} campaignChanged={} valid={} planStatus={} items={} clientStatus={} ackStatus={} worldRevision={}",
        acPlan.ReportId,
        acPlan.PlanId,
        acPlan.CampaignId.High,
        acPlan.CampaignId.Low,
        result.CampaignChanged,
        acPlan.IsValid,
        static_cast<unsigned>(acPlan.Plan.Status),
        acPlan.Plan.Items.size(),
        static_cast<unsigned>(result.Status),
        static_cast<unsigned>(result.Ack.ApplyStatus),
        result.Ack.PostApplyReport.WorldRevision);

    if (result.Ack.IsValid &&
        (result.Ack.ApplyStatus == PartyQuestReplicaApplyStatus::Applied ||
         result.Ack.ApplyStatus == PartyQuestReplicaApplyStatus::NoChanges))
    {
        m_partyQuestProtocolVerified = true;
        FlushQueuedPartyQuestSnapshots("after-repair");
    }
    else
    {
        m_partyQuestProtocolVerified = false;
    }
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

    if (status == PartyQuestClientCanonicalStatus::Applied ||
        status == PartyQuestClientCanonicalStatus::Duplicate)
    {
        const auto ready = m_partyQuestSubmissions.Complete(
            acUpdate.TransactionId,
            acUpdate.CanonicalSnapshot);
        if (ready)
            SubmitPartyQuestSnapshot(*ready, "coalesced-after-canonical");
        return;
    }

    if (status == PartyQuestClientCanonicalStatus::RevisionGap ||
        status == PartyQuestClientCanonicalStatus::TransactionConflict)
    {
        m_partyQuestProtocolVerified = false;
        m_partyQuestSubmissions.RequeueInFlight();
        m_requestTransactions.clear();
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
