#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
uint64_t GetQuestRevision(const PartyQuestState& acState, const GameId& acQuestId) noexcept
{
    const QuestSnapshot* pSnapshot = acState.FindQuest(acQuestId);
    return pSnapshot ? pSnapshot->Revision : 0;
}

uint64_t GetReplicaQuestRevision(const PartyQuestReplica& acReplica, const GameId& acQuestId) noexcept
{
    const QuestSnapshot* pSnapshot = acReplica.FindQuest(acQuestId);
    return pSnapshot ? pSnapshot->Revision : 0;
}

PartyQuestClientRepairStatus ToClientRepairStatus(PartyQuestReplicaApplyStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestReplicaApplyStatus::Applied: return PartyQuestClientRepairStatus::Applied;
    case PartyQuestReplicaApplyStatus::NoChanges: return PartyQuestClientRepairStatus::NoChanges;
    case PartyQuestReplicaApplyStatus::ClientAhead: return PartyQuestClientRepairStatus::ClientAhead;
    case PartyQuestReplicaApplyStatus::StalePlan: return PartyQuestClientRepairStatus::StalePlan;
    case PartyQuestReplicaApplyStatus::InvalidPlan: return PartyQuestClientRepairStatus::InvalidPlan;
    }

    return PartyQuestClientRepairStatus::InvalidPlan;
}
} // namespace

void PartyQuestProtocolCoordinator::SetDurableCommitHandler(DurableCommitHandler aHandler)
{
    m_durableCommitHandler = std::move(aHandler);
}

bool PartyQuestProtocolCoordinator::RestoreCanonicalState(PartyQuestState aState)
{
    if (!m_sessions.empty())
        return false;

    m_state = std::move(aState);
    m_nextPlanId = 1;
    return true;
}

bool PartyQuestProtocolCoordinator::ConnectClient(uint32_t aClientId)
{
    if (aClientId == 0)
        return false;

    Session& session = m_sessions[aClientId];
    session.Info.Connected = true;
    ++session.Info.ConnectionGeneration;
    return true;
}

bool PartyQuestProtocolCoordinator::DisconnectClient(uint32_t aClientId)
{
    const auto it = m_sessions.find(aClientId);
    if (it == m_sessions.end() || !it->second.Info.Connected)
        return false;

    it->second.Info.Connected = false;
    return true;
}

bool PartyQuestProtocolCoordinator::IsClientConnected(uint32_t aClientId) const noexcept
{
    const auto it = m_sessions.find(aClientId);
    return it != m_sessions.end() && it->second.Info.Connected;
}

PartyQuestProtocolCoordinator::Session* PartyQuestProtocolCoordinator::FindMutableConnectedSession(uint32_t aClientId) noexcept
{
    const auto it = m_sessions.find(aClientId);
    if (it == m_sessions.end() || !it->second.Info.Connected)
        return nullptr;

    return &it->second;
}

uint64_t PartyQuestProtocolCoordinator::AllocatePlanId() noexcept
{
    if (m_nextPlanId == 0)
        m_nextPlanId = 1;

    const uint64_t result = m_nextPlanId;
    if (m_nextPlanId == std::numeric_limits<uint64_t>::max())
        m_nextPlanId = 1;
    else
        ++m_nextPlanId;

    return result;
}

std::vector<uint32_t> PartyQuestProtocolCoordinator::BuildConnectedRecipientList() const
{
    std::vector<uint32_t> recipients;
    recipients.reserve(m_sessions.size());

    for (const auto& [clientId, session] : m_sessions)
    {
        if (session.Info.Connected)
            recipients.push_back(clientId);
    }

    std::sort(recipients.begin(), recipients.end());
    return recipients;
}

PartyQuestTransactionDispatch PartyQuestProtocolCoordinator::HandleTransaction(
    uint32_t aClientId,
    const RequestPartyQuestTransaction& acRequest)
{
    PartyQuestTransactionDispatch dispatch;
    dispatch.Response.RequestId = acRequest.RequestId;

    Session* pSession = FindMutableConnectedSession(aClientId);
    if (!pSession)
    {
        dispatch.Status = PartyQuestTransactionHandleStatus::UnknownClient;
        return dispatch;
    }

    if (!acRequest.IsValid || acRequest.RequestId == 0 ||
        acRequest.Transaction.InitiatorPlayerId != aClientId)
    {
        dispatch.Status = PartyQuestTransactionHandleStatus::InvalidMessage;
        dispatch.Response.Result = {
            PartyQuestApplyStatus::InvalidTransactionId,
            m_state.GetWorldRevision(),
            GetQuestRevision(m_state, acRequest.Transaction.QuestId)};
        return dispatch;
    }

    const auto cachedIt = pSession->Transactions.find(acRequest.RequestId);
    if (cachedIt != pSession->Transactions.end())
    {
        if (cachedIt->second.Transaction == acRequest.Transaction)
        {
            dispatch.Status = PartyQuestTransactionHandleStatus::DuplicateRequest;
            dispatch.Response = cachedIt->second.Response;
            return dispatch;
        }

        dispatch.Status = PartyQuestTransactionHandleStatus::RequestIdConflict;
        dispatch.Response.Result = {
            PartyQuestApplyStatus::TransactionConflict,
            m_state.GetWorldRevision(),
            GetQuestRevision(m_state, acRequest.Transaction.QuestId)};
        if (const QuestSnapshot* pCanonical = m_state.FindQuest(acRequest.Transaction.QuestId))
            dispatch.Response.CanonicalSnapshot = *pCanonical;
        return dispatch;
    }

    PartyQuestState candidateState = m_state;
    const PartyQuestApplyResult candidateResult = candidateState.Apply(acRequest.Transaction);

    if (candidateResult.Status == PartyQuestApplyStatus::Accepted && m_durableCommitHandler)
    {
        bool committed = false;
        try
        {
            committed = m_durableCommitHandler(candidateState);
        }
        catch (...)
        {
            committed = false;
        }

        if (!committed)
        {
            dispatch.Status = PartyQuestTransactionHandleStatus::PersistenceFailure;
            dispatch.Response.Result = {
                PartyQuestApplyStatus::TransactionConflict,
                m_state.GetWorldRevision(),
                GetQuestRevision(m_state, acRequest.Transaction.QuestId)};
            if (const QuestSnapshot* pCanonical = m_state.FindQuest(acRequest.Transaction.QuestId))
                dispatch.Response.CanonicalSnapshot = *pCanonical;
            return dispatch;
        }
    }

    if (candidateResult.Status == PartyQuestApplyStatus::Accepted)
        m_state = std::move(candidateState);

    dispatch.Status = PartyQuestTransactionHandleStatus::Processed;
    dispatch.Response.Result = candidateResult;
    if (const QuestSnapshot* pCanonical = m_state.FindQuest(acRequest.Transaction.QuestId))
        dispatch.Response.CanonicalSnapshot = *pCanonical;

    pSession->Transactions.emplace(
        acRequest.RequestId,
        TransactionCacheEntry{acRequest.Transaction, dispatch.Response});
    if (dispatch.Response.Result.Status == PartyQuestApplyStatus::Accepted &&
        dispatch.Response.CanonicalSnapshot)
    {
        NotifyPartyQuestCanonicalUpdate update;
        update.TransactionId = acRequest.Transaction.TransactionId;
        update.WorldRevision = dispatch.Response.Result.WorldRevision;
        update.InitiatorPlayerId = aClientId;
        update.CanonicalSnapshot = *dispatch.Response.CanonicalSnapshot;
        dispatch.Broadcast = std::move(update);
        dispatch.Recipients = BuildConnectedRecipientList();
    }

    return dispatch;
}

PartyQuestReportDispatch PartyQuestProtocolCoordinator::HandleReplicaReport(
    uint32_t aClientId,
    const RequestPartyQuestReplicaReport& acRequest)
{
    PartyQuestReportDispatch dispatch;

    Session* pSession = FindMutableConnectedSession(aClientId);
    if (!pSession)
    {
        dispatch.Status = PartyQuestReportHandleStatus::UnknownClient;
        return dispatch;
    }

    if (!acRequest.IsValid || acRequest.ReportId == 0)
    {
        dispatch.Status = PartyQuestReportHandleStatus::InvalidMessage;
        return dispatch;
    }

    const auto cachedIt = pSession->Reports.find(acRequest.ReportId);
    if (cachedIt != pSession->Reports.end())
    {
        if (cachedIt->second.IsReconnect == acRequest.IsReconnect &&
            cachedIt->second.Report == acRequest.Report)
        {
            dispatch.Status = PartyQuestReportHandleStatus::DuplicateReport;
            dispatch.Response = cachedIt->second.Response;
            return dispatch;
        }

        dispatch.Status = PartyQuestReportHandleStatus::ReportIdConflict;
        return dispatch;
    }

    NotifyPartyQuestRepairPlan response;
    response.ReportId = acRequest.ReportId;
    response.PlanId = AllocatePlanId();
    response.Plan = PartyQuestRepairPlanner::Build(m_state, acRequest.Report);

    pSession->Info.LastReportedWorldRevision = acRequest.Report.WorldRevision;
    pSession->Info.LastReportWasReconnect = acRequest.IsReconnect;
    pSession->Info.PendingPlanId = response.PlanId;

    pSession->Reports.emplace(
        acRequest.ReportId,
        ReportCacheEntry{acRequest.IsReconnect, acRequest.Report, response});
    pSession->Plans.emplace(
        response.PlanId,
        PlanCacheEntry{acRequest.ReportId, response.Plan, std::nullopt, {}});

    dispatch.Status = PartyQuestReportHandleStatus::Generated;
    dispatch.Response = std::move(response);
    return dispatch;
}

PartyQuestAckResult PartyQuestProtocolCoordinator::HandleRepairAck(
    uint32_t aClientId,
    const RequestPartyQuestRepairAck& acAck)
{
    Session* pSession = FindMutableConnectedSession(aClientId);
    if (!pSession)
        return {PartyQuestAckHandleStatus::UnknownClient, PartyQuestRepairPlanStatus::RepairRequired};

    if (!acAck.IsValid || acAck.PlanId == 0)
        return {PartyQuestAckHandleStatus::InvalidMessage, PartyQuestRepairPlanStatus::RepairRequired};

    const auto planIt = pSession->Plans.find(acAck.PlanId);
    if (planIt == pSession->Plans.end())
        return {PartyQuestAckHandleStatus::UnknownPlan, PartyQuestRepairPlanStatus::RepairRequired};

    PlanCacheEntry& planEntry = planIt->second;
    if (planEntry.Ack)
    {
        if (*planEntry.Ack == acAck)
            return {PartyQuestAckHandleStatus::DuplicateAck, planEntry.AckResult.VerificationStatus};

        return {PartyQuestAckHandleStatus::AckConflict, planEntry.AckResult.VerificationStatus};
    }

    const PartyQuestRepairPlan verification = PartyQuestRepairPlanner::Build(m_state, acAck.PostApplyReport);
    PartyQuestAckResult result;
    result.VerificationStatus = verification.Status;

    if (verification.Status == PartyQuestRepairPlanStatus::ClientAhead ||
        acAck.ApplyStatus == PartyQuestReplicaApplyStatus::ClientAhead)
    {
        result.Status = PartyQuestAckHandleStatus::ClientAhead;
    }
    else if ((acAck.ApplyStatus == PartyQuestReplicaApplyStatus::Applied ||
              acAck.ApplyStatus == PartyQuestReplicaApplyStatus::NoChanges) &&
             verification.Status == PartyQuestRepairPlanStatus::UpToDate)
    {
        result.Status = PartyQuestAckHandleStatus::Verified;
        pSession->Info.LastVerifiedWorldRevision = acAck.PostApplyReport.WorldRevision;
        if (pSession->Info.PendingPlanId == acAck.PlanId)
            pSession->Info.PendingPlanId = 0;
    }
    else
    {
        result.Status = PartyQuestAckHandleStatus::RepairRejected;
    }

    planEntry.Ack = acAck;
    planEntry.AckResult = result;
    return result;
}

const PartyQuestCoordinatorSessionInfo* PartyQuestProtocolCoordinator::FindSession(uint32_t aClientId) const noexcept
{
    const auto it = m_sessions.find(aClientId);
    return it != m_sessions.end() ? &it->second.Info : nullptr;
}

RequestPartyQuestReplicaReport PartyQuestClientSession::BuildReplicaReport(uint64_t aReportId, bool aReconnect) const
{
    RequestPartyQuestReplicaReport request;
    request.ReportId = aReportId;
    request.IsReconnect = aReconnect;
    request.Report = m_replica.BuildReport();
    request.IsValid = aReportId != 0;
    return request;
}

PartyQuestClientCanonicalStatus PartyQuestClientSession::HandleCanonicalUpdate(
    const NotifyPartyQuestCanonicalUpdate& acUpdate)
{
    if (!acUpdate.IsValid || acUpdate.TransactionId == 0 || acUpdate.WorldRevision == 0 ||
        acUpdate.InitiatorPlayerId == 0 || !acUpdate.CanonicalSnapshot.QuestId ||
        acUpdate.CanonicalSnapshot.Revision == 0 ||
        acUpdate.CanonicalSnapshot.InitiatorPlayerId != acUpdate.InitiatorPlayerId)
    {
        return PartyQuestClientCanonicalStatus::InvalidMessage;
    }

    const auto cachedIt = m_canonicalUpdates.find(acUpdate.TransactionId);
    if (cachedIt != m_canonicalUpdates.end())
    {
        return cachedIt->second.Update == acUpdate
            ? PartyQuestClientCanonicalStatus::Duplicate
            : PartyQuestClientCanonicalStatus::TransactionConflict;
    }

    if (acUpdate.WorldRevision != m_replica.GetWorldRevision() + 1)
        return PartyQuestClientCanonicalStatus::RevisionGap;

    const uint64_t currentQuestRevision = GetReplicaQuestRevision(m_replica, acUpdate.CanonicalSnapshot.QuestId);
    if (acUpdate.CanonicalSnapshot.Revision != currentQuestRevision + 1)
        return PartyQuestClientCanonicalStatus::RevisionGap;

    m_replica.ObserveLocalSnapshot(acUpdate.CanonicalSnapshot);
    m_replica.SetObservedWorldRevision(acUpdate.WorldRevision);
    m_canonicalUpdates.emplace(acUpdate.TransactionId, CanonicalCacheEntry{acUpdate});
    return PartyQuestClientCanonicalStatus::Applied;
}

PartyQuestClientRepairResult PartyQuestClientSession::HandleRepairPlan(const NotifyPartyQuestRepairPlan& acPlan)
{
    PartyQuestClientRepairResult result;
    result.Ack.PlanId = acPlan.PlanId;

    const auto cachedIt = m_repairs.find(acPlan.PlanId);
    if (cachedIt != m_repairs.end())
    {
        if (cachedIt->second.Plan == acPlan)
        {
            result.Status = PartyQuestClientRepairStatus::Duplicate;
            result.Ack = cachedIt->second.Ack;
            return result;
        }

        result.Status = PartyQuestClientRepairStatus::PlanConflict;
        result.Ack.ApplyStatus = PartyQuestReplicaApplyStatus::InvalidPlan;
        result.Ack.PostApplyReport = m_replica.BuildReport();
        result.Ack.IsValid = acPlan.PlanId != 0;
        return result;
    }

    PartyQuestReplicaApplyStatus applyStatus = PartyQuestReplicaApplyStatus::InvalidPlan;
    if (acPlan.IsValid && acPlan.ReportId != 0 && acPlan.PlanId != 0)
        applyStatus = m_replica.Apply(acPlan.Plan);

    result.Status = ToClientRepairStatus(applyStatus);
    result.Ack.ApplyStatus = applyStatus;
    result.Ack.PostApplyReport = m_replica.BuildReport();
    result.Ack.IsValid = acPlan.IsValid && acPlan.PlanId != 0;

    if (acPlan.PlanId != 0)
    {
        m_repairs.emplace(
            acPlan.PlanId,
            RepairCacheEntry{acPlan, result.Ack, result.Status});
    }

    return result;
}
