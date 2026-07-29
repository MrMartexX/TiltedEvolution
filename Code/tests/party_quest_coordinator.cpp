#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Messages/ServerMessageFactory.h>
#include <Structs/Skyrim/PartyQuestProtocol.h>

#include <catch2/catch.hpp>

using namespace TiltedPhoques;

namespace
{
QuestSnapshot BuildCoordinatorSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}};
    return snapshot;
}

RequestPartyQuestTransaction BuildCoordinatorRequest(
    uint64_t aRequestId,
    uint64_t aTransactionId,
    uint32_t aClientId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    RequestPartyQuestTransaction request;
    request.RequestId = aRequestId;
    request.Transaction.TransactionId = aTransactionId;
    request.Transaction.InitiatorPlayerId = aClientId;
    request.Transaction.QuestId = aQuestId;
    request.Transaction.ExpectedQuestRevision = aExpectedRevision;
    request.Transaction.ProposedSnapshot = BuildCoordinatorSnapshot(aQuestId, aStage);
    return request;
}

template <class T>
UniquePtr<T> RoundTripCoordinatorServerMessage(const T& acMessage)
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

TEST_CASE("Coordinator correlates transaction requests and emits one canonical broadcast", "[quest.party-state.coordinator]")
{
    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(20));
    REQUIRE(coordinator.ConnectClient(10));

    PartyQuestClientSession client10(10);
    PartyQuestClientSession client20(20);
    const GameId questId(2, 0x1000);

    const auto request = BuildCoordinatorRequest(100, 500, 10, questId, 0, 20);
    const auto dispatch = coordinator.HandleTransaction(10, request);
    REQUIRE(dispatch.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(dispatch.Response.Result.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(dispatch.Broadcast.has_value());
    const std::vector<uint32_t> expectedRecipients{10, 20};
    REQUIRE(dispatch.Recipients == expectedRecipients);

    auto decodedUpdate = RoundTripCoordinatorServerMessage(*dispatch.Broadcast);
    REQUIRE(decodedUpdate->IsValid);
    REQUIRE(*decodedUpdate == *dispatch.Broadcast);
    REQUIRE(client10.HandleCanonicalUpdate(*decodedUpdate) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(client20.HandleCanonicalUpdate(*decodedUpdate) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(client10.GetReplica().GetWorldRevision() == 1);
    REQUIRE(client20.GetReplica().GetWorldRevision() == 1);

    const auto repeated = coordinator.HandleTransaction(10, request);
    REQUIRE(repeated.Status == PartyQuestTransactionHandleStatus::DuplicateRequest);
    REQUIRE(repeated.Response == dispatch.Response);
    REQUIRE_FALSE(repeated.Broadcast.has_value());
    REQUIRE(repeated.Recipients.empty());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 1);

    auto conflictingRequest = request;
    conflictingRequest.Transaction.TransactionId = 501;
    conflictingRequest.Transaction.ProposedSnapshot.CurrentStage = 30;
    const auto conflict = coordinator.HandleTransaction(10, conflictingRequest);
    REQUIRE(conflict.Status == PartyQuestTransactionHandleStatus::RequestIdConflict);
    REQUIRE(conflict.Response.Result.Status == PartyQuestApplyStatus::TransactionConflict);
    REQUIRE_FALSE(conflict.Broadcast.has_value());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 1);

    REQUIRE(client10.HandleCanonicalUpdate(*decodedUpdate) == PartyQuestClientCanonicalStatus::Duplicate);
}

TEST_CASE("Reconnect report repair and acknowledgement are cached and verified", "[quest.party-state.coordinator]")
{
    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(1));
    REQUIRE(coordinator.ConnectClient(2));

    PartyQuestClientSession client1(1);
    PartyQuestClientSession client2(2);
    const GameId questId(3, 0x2000);

    const auto first = coordinator.HandleTransaction(
        1,
        BuildCoordinatorRequest(1000, 2000, 1, questId, 0, 10));
    REQUIRE(first.Broadcast.has_value());
    REQUIRE(client1.HandleCanonicalUpdate(*first.Broadcast) == PartyQuestClientCanonicalStatus::Applied);
    REQUIRE(client2.HandleCanonicalUpdate(*first.Broadcast) == PartyQuestClientCanonicalStatus::Applied);

    REQUIRE(coordinator.DisconnectClient(2));
    const auto second = coordinator.HandleTransaction(
        1,
        BuildCoordinatorRequest(1001, 2001, 1, questId, 1, 40));
    REQUIRE(second.Broadcast.has_value());
    REQUIRE(second.Recipients == std::vector<uint32_t>{1});
    REQUIRE(client1.HandleCanonicalUpdate(*second.Broadcast) == PartyQuestClientCanonicalStatus::Applied);

    REQUIRE(coordinator.ConnectClient(2));
    const auto report = client2.BuildReplicaReport(3000, true);
    const auto planDispatch = coordinator.HandleReplicaReport(2, report);
    REQUIRE(planDispatch.Status == PartyQuestReportHandleStatus::Generated);
    REQUIRE(planDispatch.Response.has_value());
    REQUIRE(planDispatch.Response->Plan.Status == PartyQuestRepairPlanStatus::RepairRequired);
    REQUIRE(planDispatch.Response->Plan.Items.size() == 1);

    const auto repeatedReport = coordinator.HandleReplicaReport(2, report);
    REQUIRE(repeatedReport.Status == PartyQuestReportHandleStatus::DuplicateReport);
    REQUIRE(repeatedReport.Response == planDispatch.Response);

    auto conflictingReport = report;
    conflictingReport.IsReconnect = false;
    REQUIRE(coordinator.HandleReplicaReport(2, conflictingReport).Status ==
            PartyQuestReportHandleStatus::ReportIdConflict);

    const auto repair = client2.HandleRepairPlan(*planDispatch.Response);
    REQUIRE(repair.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE(repair.Ack.ApplyStatus == PartyQuestReplicaApplyStatus::Applied);

    const auto verification = coordinator.HandleRepairAck(2, repair.Ack);
    REQUIRE(verification.Status == PartyQuestAckHandleStatus::Verified);
    REQUIRE(verification.VerificationStatus == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(client2.GetReplica().GetWorldRevision() == coordinator.GetCanonicalState().GetWorldRevision());

    const PartyQuestCoordinatorSessionInfo* pSession = coordinator.FindSession(2);
    REQUIRE(pSession);
    REQUIRE(pSession->Connected);
    REQUIRE(pSession->ConnectionGeneration == 2);
    REQUIRE(pSession->LastReportWasReconnect);
    REQUIRE(pSession->LastVerifiedWorldRevision == 2);
    REQUIRE(pSession->PendingPlanId == 0);

    const auto duplicateAck = coordinator.HandleRepairAck(2, repair.Ack);
    REQUIRE(duplicateAck.Status == PartyQuestAckHandleStatus::DuplicateAck);

    auto conflictingAck = repair.Ack;
    conflictingAck.PostApplyReport.WorldRevision = 1;
    REQUIRE(coordinator.HandleRepairAck(2, conflictingAck).Status == PartyQuestAckHandleStatus::AckConflict);

    const auto duplicatePlan = client2.HandleRepairPlan(*planDispatch.Response);
    REQUIRE(duplicatePlan.Status == PartyQuestClientRepairStatus::Duplicate);
    REQUIRE(duplicatePlan.Ack == repair.Ack);

    auto conflictingPlan = *planDispatch.Response;
    ++conflictingPlan.Plan.TargetWorldRevision;
    REQUIRE(client2.HandleRepairPlan(conflictingPlan).Status == PartyQuestClientRepairStatus::PlanConflict);
}

TEST_CASE("Coordinator rejects spoofed initiators and repairs a client after a broadcast gap", "[quest.party-state.coordinator]")
{
    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(7));
    REQUIRE(coordinator.ConnectClient(8));

    const GameId questId(4, 0x3000);
    auto spoofed = BuildCoordinatorRequest(4000, 5000, 99, questId, 0, 10);
    const auto spoofedResult = coordinator.HandleTransaction(7, spoofed);
    REQUIRE(spoofedResult.Status == PartyQuestTransactionHandleStatus::InvalidMessage);
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 0);

    const auto first = coordinator.HandleTransaction(
        7,
        BuildCoordinatorRequest(4001, 5001, 7, questId, 0, 10));
    const auto second = coordinator.HandleTransaction(
        7,
        BuildCoordinatorRequest(4002, 5002, 7, questId, 1, 30));
    REQUIRE(first.Broadcast.has_value());
    REQUIRE(second.Broadcast.has_value());

    PartyQuestClientSession client8(8);
    REQUIRE(client8.HandleCanonicalUpdate(*second.Broadcast) == PartyQuestClientCanonicalStatus::RevisionGap);
    REQUIRE(client8.GetReplica().GetWorldRevision() == 0);

    const auto report = client8.BuildReplicaReport(6000, false);
    const auto repairPlan = coordinator.HandleReplicaReport(8, report);
    REQUIRE(repairPlan.Status == PartyQuestReportHandleStatus::Generated);
    REQUIRE(repairPlan.Response.has_value());
    REQUIRE(repairPlan.Response->Plan.Status == PartyQuestRepairPlanStatus::RepairRequired);

    const auto repair = client8.HandleRepairPlan(*repairPlan.Response);
    REQUIRE(repair.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE(coordinator.HandleRepairAck(8, repair.Ack).Status == PartyQuestAckHandleStatus::Verified);
    REQUIRE(client8.GetReplica().GetWorldRevision() == 2);
    REQUIRE(client8.GetReplica().FindQuest(questId)->CurrentStage == 30);

    RequestPartyQuestRepairAck unknownAck;
    unknownAck.PlanId = 999999;
    unknownAck.ApplyStatus = PartyQuestReplicaApplyStatus::NoChanges;
    unknownAck.PostApplyReport = client8.GetReplica().BuildReport();
    REQUIRE(coordinator.HandleRepairAck(8, unknownAck).Status == PartyQuestAckHandleStatus::UnknownPlan);
}

TEST_CASE("Client submission queue suppresses canonical-equivalent and duplicate observations", "[quest.party-state.coordinator.submission]")
{
    PartyQuestClientSubmissionQueue queue;
    PartyQuestReplica replica;
    const GameId questId(5, 0x4000);

    QuestSnapshot canonical = BuildCoordinatorSnapshot(questId, 20);
    canonical.Revision = 7;
    canonical.InitiatorPlayerId = 42;
    replica.ObserveLocalSnapshot(canonical);
    replica.SetObservedWorldRevision(7);

    QuestSnapshot localEquivalent = BuildCoordinatorSnapshot(questId, 20);
    const auto duplicateCanonical = queue.Observe(localEquivalent, replica);
    REQUIRE(duplicateCanonical.Status == PartyQuestClientSubmissionStatus::Duplicate);
    REQUIRE_FALSE(duplicateCanonical.ReadySnapshot.has_value());

    QuestSnapshot changed = BuildCoordinatorSnapshot(questId, 30);
    const auto ready = queue.Observe(changed, replica);
    REQUIRE(ready.Status == PartyQuestClientSubmissionStatus::Ready);
    REQUIRE(ready.ReadySnapshot.has_value());
    REQUIRE(queue.MarkInFlight(7000, *ready.ReadySnapshot));

    const auto duplicateInFlight = queue.Observe(changed, replica);
    REQUIRE(duplicateInFlight.Status == PartyQuestClientSubmissionStatus::Duplicate);
    REQUIRE(queue.GetInFlightCount() == 1);
    REQUIRE(queue.GetQueuedCount() == 0);
}

TEST_CASE("Client submission queue coalesces rapid stages to the latest snapshot", "[quest.party-state.coordinator.submission]")
{
    PartyQuestClientSubmissionQueue queue;
    PartyQuestReplica replica;
    const GameId questId(6, 0x5000);

    const QuestSnapshot first = BuildCoordinatorSnapshot(questId, 10);
    const auto ready = queue.Observe(first, replica);
    REQUIRE(ready.Status == PartyQuestClientSubmissionStatus::Ready);
    REQUIRE(queue.MarkInFlight(8000, *ready.ReadySnapshot));

    const QuestSnapshot second = BuildCoordinatorSnapshot(questId, 20);
    REQUIRE(queue.Observe(second, replica).Status == PartyQuestClientSubmissionStatus::Queued);

    const QuestSnapshot latest = BuildCoordinatorSnapshot(questId, 40);
    REQUIRE(queue.Observe(latest, replica).Status == PartyQuestClientSubmissionStatus::ReplacedQueued);
    REQUIRE(queue.Observe(latest, replica).Status == PartyQuestClientSubmissionStatus::Duplicate);
    REQUIRE(queue.GetInFlightCount() == 1);
    REQUIRE(queue.GetQueuedCount() == 1);

    QuestSnapshot firstCanonical = first;
    firstCanonical.Revision = 1;
    firstCanonical.InitiatorPlayerId = 1;
    const auto coalesced = queue.Complete(8000, firstCanonical);
    REQUIRE(coalesced.has_value());
    REQUIRE(coalesced->CurrentStage == 40);
    REQUIRE(queue.GetInFlightCount() == 0);
    REQUIRE(queue.GetQueuedCount() == 0);

    REQUIRE(queue.MarkInFlight(8001, *coalesced));
    QuestSnapshot latestCanonical = latest;
    latestCanonical.Revision = 2;
    latestCanonical.InitiatorPlayerId = 1;
    REQUIRE_FALSE(queue.Complete(8001, latestCanonical).has_value());
    REQUIRE(queue.GetInFlightCount() == 0);
}

TEST_CASE("Rejected and disconnected submissions wait for replica repair before retry", "[quest.party-state.coordinator.submission]")
{
    PartyQuestClientSubmissionQueue queue;
    PartyQuestReplica replica;
    const GameId questId(7, 0x6000);

    const QuestSnapshot first = BuildCoordinatorSnapshot(questId, 10);
    const auto ready = queue.Observe(first, replica);
    REQUIRE(queue.MarkInFlight(9000, *ready.ReadySnapshot));

    const QuestSnapshot newer = BuildCoordinatorSnapshot(questId, 20);
    REQUIRE(queue.Observe(newer, replica).Status == PartyQuestClientSubmissionStatus::Queued);
    REQUIRE(queue.Reject(9000));
    REQUIRE(queue.GetInFlightCount() == 0);
    REQUIRE(queue.GetQueuedCount() == 1);

    QuestSnapshot repairedCanonical = newer;
    repairedCanonical.Revision = 1;
    repairedCanonical.InitiatorPlayerId = 9;
    replica.ObserveLocalSnapshot(repairedCanonical);
    replica.SetObservedWorldRevision(1);

    REQUIRE(queue.TakeReady(replica).empty());
    REQUIRE(queue.GetQueuedCount() == 0);

    const QuestSnapshot afterRepair = BuildCoordinatorSnapshot(questId, 30);
    const auto next = queue.Observe(afterRepair, replica);
    REQUIRE(next.Status == PartyQuestClientSubmissionStatus::Ready);
    REQUIRE(queue.MarkInFlight(9001, *next.ReadySnapshot));

    queue.RequeueInFlight();
    REQUIRE(queue.GetInFlightCount() == 0);
    REQUIRE(queue.GetQueuedCount() == 1);

    const auto retry = queue.TakeReady(replica);
    REQUIRE(retry.size() == 1);
    REQUIRE(retry.front().CurrentStage == 30);
    REQUIRE(queue.GetQueuedCount() == 0);
}
