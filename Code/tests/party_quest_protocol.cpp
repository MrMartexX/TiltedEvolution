#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/PartyQuestMessages.h>
#include <Messages/ServerMessageFactory.h>

#include <Structs/Skyrim/PartyQuestRepair.h>

#include <catch2/catch.hpp>

using namespace TiltedPhoques;

namespace
{
QuestSnapshot BuildProtocolSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {aStage, 10};
    snapshot.Objectives = {
        {20, QuestObjectiveState::Displayed},
        {10, QuestObjectiveState::Completed}
    };
    snapshot.ReferenceAliases = {
        {2, std::nullopt, true},
        {1, GameId(3, 0x120), false}
    };
    snapshot.LocationAliases = {
        {4, GameId(3, 0x500)}
    };
    snapshot.CreatedReferences = {
        GameId(3, 0x900),
        GameId(3, 0x800)
    };
    return snapshot;
}

PartyQuestTransaction BuildProtocolTransaction(
    uint64_t aTransactionId,
    uint32_t aPlayerId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = aPlayerId;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildProtocolSnapshot(aQuestId, aStage);
    return transaction;
}

template <class T>
UniquePtr<T> RoundTripClientMessage(const T& acMessage)
{
    Buffer buffer(1024 * 1024);
    Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);

    Buffer::Reader reader(&buffer);
    ClientMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);
    REQUIRE(message->GetOpcode() == acMessage.GetOpcode());
    return CastUnique<T>(std::move(message));
}

template <class T>
UniquePtr<T> RoundTripServerMessage(const T& acMessage)
{
    Buffer buffer(1024 * 1024);
    Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);

    Buffer::Reader reader(&buffer);
    ServerMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);
    REQUIRE(message->GetOpcode() == acMessage.GetOpcode());
    return CastUnique<T>(std::move(message));
}
} // namespace

TEST_CASE("Party quest protocol messages round-trip through the real factories", "[quest.party-state.protocol]")
{
    const PartyQuestCampaignId campaignId{0x1111222233334444ull, 0xAAAABBBBCCCCDDDDull};
    const GameId questId(3, 0x1000);
    PartyQuestState server;
    REQUIRE(server.Apply(BuildProtocolTransaction(1001, 41, questId, 0, 20)).Status == PartyQuestApplyStatus::Accepted);

    RequestPartyQuestTransaction transactionRequest;
    transactionRequest.RequestId = 7001;
    transactionRequest.Transaction = BuildProtocolTransaction(1002, 41, questId, 1, 30);
    transactionRequest.SyncFacts.QuestType = 4;
    transactionRequest.SyncFacts.HasStages = true;
    transactionRequest.SyncFacts.IsDisplayedInHud = true;
    transactionRequest.SyncFacts.HasDisplayName = true;
    auto decodedTransactionRequest = RoundTripClientMessage(transactionRequest);
    REQUIRE(decodedTransactionRequest->IsValid);
    REQUIRE(*decodedTransactionRequest == transactionRequest);

    RequestPartyQuestReplicaReport reportRequest;
    reportRequest.ReportId = 7002;
    reportRequest.CampaignId = campaignId;
    reportRequest.IsReconnect = true;
    reportRequest.Report = PartyQuestReplica::FromCanonical(server).BuildReport();
    auto decodedReportRequest = RoundTripClientMessage(reportRequest);
    REQUIRE(decodedReportRequest->IsValid);
    REQUIRE(*decodedReportRequest == reportRequest);

    NotifyPartyQuestRepairPlan repairMessage;
    repairMessage.ReportId = reportRequest.ReportId;
    repairMessage.PlanId = 8001;
    repairMessage.CampaignId = campaignId;
    repairMessage.Plan = PartyQuestRepairPlanner::Build(server, reportRequest.Report);
    repairMessage.Plan.Status = PartyQuestRepairPlanStatus::RepairRequired;
    repairMessage.Plan.RemovedQuestIds = {GameId(0, 0x000C7919)};
    auto decodedRepairMessage = RoundTripServerMessage(repairMessage);
    REQUIRE(decodedRepairMessage->IsValid);
    REQUIRE(*decodedRepairMessage == repairMessage);

    RequestPartyQuestRepairAck repairAck;
    repairAck.PlanId = repairMessage.PlanId;
    repairAck.ApplyStatus = PartyQuestReplicaApplyStatus::NoChanges;
    repairAck.PostApplyReport = reportRequest.Report;
    auto decodedRepairAck = RoundTripClientMessage(repairAck);
    REQUIRE(decodedRepairAck->IsValid);
    REQUIRE(*decodedRepairAck == repairAck);

    NotifyPartyQuestTransactionResult transactionResult;
    transactionResult.RequestId = transactionRequest.RequestId;
    transactionResult.Result = {PartyQuestApplyStatus::Accepted, 2, 2};
    QuestSnapshot canonical = transactionRequest.Transaction.ProposedSnapshot;
    canonical.Revision = 2;
    canonical.InitiatorPlayerId = 41;
    canonical.Canonicalize();
    transactionResult.CanonicalSnapshot = canonical;
    auto decodedTransactionResult = RoundTripServerMessage(transactionResult);
    REQUIRE(decodedTransactionResult->IsValid);
    REQUIRE(*decodedTransactionResult == transactionResult);

    NotifyPartyQuestTransactionResult admissionRejected;
    admissionRejected.RequestId = 7010;
    admissionRejected.Result = {PartyQuestApplyStatus::AdmissionRejected, 2, 0};
    auto decodedAdmissionRejected = RoundTripServerMessage(admissionRejected);
    REQUIRE(decodedAdmissionRejected->IsValid);
    REQUIRE(*decodedAdmissionRejected == admissionRejected);
}

TEST_CASE("Reconnect report repair and acknowledgement converge without a second game client", "[quest.party-state.protocol]")
{
    const PartyQuestCampaignId campaignId{0x1234, 0x5678};
    const GameId questId(4, 0x2000);
    PartyQuestState server;
    REQUIRE(server.Apply(BuildProtocolTransaction(2001, 12, questId, 0, 10)).Status == PartyQuestApplyStatus::Accepted);

    PartyQuestReplica reconnectingClient = PartyQuestReplica::FromCanonical(server);

    REQUIRE(server.Apply(BuildProtocolTransaction(2002, 27, questId, 1, 40)).Status == PartyQuestApplyStatus::Accepted);

    RequestPartyQuestReplicaReport reportRequest;
    reportRequest.ReportId = 9001;
    reportRequest.CampaignId = campaignId;
    reportRequest.IsReconnect = true;
    reportRequest.Report = reconnectingClient.BuildReport();
    auto decodedReport = RoundTripClientMessage(reportRequest);
    REQUIRE(decodedReport->IsValid);

    NotifyPartyQuestRepairPlan repairMessage;
    repairMessage.ReportId = decodedReport->ReportId;
    repairMessage.PlanId = 9002;
    repairMessage.CampaignId = campaignId;
    repairMessage.Plan = PartyQuestRepairPlanner::Build(server, decodedReport->Report);
    REQUIRE(repairMessage.Plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(repairMessage.Plan.Items.size() == 1);

    auto decodedPlan = RoundTripServerMessage(repairMessage);
    REQUIRE(decodedPlan->IsValid);
    const auto applyStatus = reconnectingClient.Apply(decodedPlan->Plan);
    REQUIRE(applyStatus == PartyQuestReplicaApplyStatus::Applied);

    RequestPartyQuestRepairAck ack;
    ack.PlanId = decodedPlan->PlanId;
    ack.ApplyStatus = applyStatus;
    ack.PostApplyReport = reconnectingClient.BuildReport();
    auto decodedAck = RoundTripClientMessage(ack);
    REQUIRE(decodedAck->IsValid);
    REQUIRE(decodedAck->PlanId == repairMessage.PlanId);
    REQUIRE(decodedAck->ApplyStatus == PartyQuestReplicaApplyStatus::Applied);

    const auto verification = PartyQuestRepairPlanner::Build(server, decodedAck->PostApplyReport);
    REQUIRE(verification.Status == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(verification.Items.empty());
    REQUIRE(verification.RemovedQuestIds.empty());
    REQUIRE(reconnectingClient.GetWorldRevision() == server.GetWorldRevision());
    REQUIRE(*reconnectingClient.FindQuest(questId) == *server.FindQuest(questId));
}

TEST_CASE("Duplicate network delivery stays idempotent after decoding", "[quest.party-state.protocol]")
{
    const GameId questId(5, 0x3000);
    PartyQuestState server;

    RequestPartyQuestTransaction request;
    request.RequestId = 10001;
    request.Transaction = BuildProtocolTransaction(3001, 33, questId, 0, 20);
    request.SyncFacts = {4, true, true, true};

    auto firstDelivery = RoundTripClientMessage(request);
    auto repeatedDelivery = RoundTripClientMessage(request);
    REQUIRE(firstDelivery->IsValid);
    REQUIRE(repeatedDelivery->IsValid);

    const auto firstResult = server.Apply(firstDelivery->Transaction);
    const auto duplicateResult = server.Apply(repeatedDelivery->Transaction);
    REQUIRE(firstResult.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(duplicateResult.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE(server.GetWorldRevision() == 1);
    REQUIRE(server.GetJournal().size() == 1);

    NotifyPartyQuestTransactionResult response;
    response.RequestId = request.RequestId;
    response.Result = duplicateResult;
    response.CanonicalSnapshot = *server.FindQuest(questId);

    auto decodedResponse = RoundTripServerMessage(response);
    REQUIRE(decodedResponse->IsValid);
    REQUIRE(decodedResponse->Result.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE(decodedResponse->CanonicalSnapshot.has_value());
    REQUIRE(*decodedResponse->CanonicalSnapshot == *server.FindQuest(questId));
}
