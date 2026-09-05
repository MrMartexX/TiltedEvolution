#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>
#include <unordered_set>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Messages/ServerMessageFactory.h>
#include <Structs/Skyrim/PartyQuestProtocol.h>
#include <Structs/Skyrim/PartyQuestResourcePolicy.h>

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
    const PartyQuestCampaignId campaignId{0x1010101010101010ull, 0x2020202020202020ull};
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

    NotifyPartyQuestRepairPlan deliveredPlan = *planDispatch.Response;
    deliveredPlan.CampaignId = campaignId;
    const auto repair = client2.HandleRepairPlan(deliveredPlan);
    REQUIRE(repair.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE_FALSE(repair.CampaignChanged);
    REQUIRE(repair.Ack.ApplyStatus == PartyQuestReplicaApplyStatus::Applied);

    const auto verification = coordinator.HandleRepairAck(2, repair.Ack);
    REQUIRE(verification.Status == PartyQuestAckHandleStatus::Verified);
    REQUIRE(verification.VerificationStatus == PartyQuestRepairPlanStatus::UpToDate);
    REQUIRE(client2.GetReplica().GetWorldRevision() == coordinator.GetCanonicalState().GetWorldRevision());
    REQUIRE(client2.GetCampaignId() == campaignId);

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

    const auto duplicatePlan = client2.HandleRepairPlan(deliveredPlan);
    REQUIRE(duplicatePlan.Status == PartyQuestClientRepairStatus::Duplicate);
    REQUIRE(duplicatePlan.Ack == repair.Ack);

    auto conflictingPlan = deliveredPlan;
    ++conflictingPlan.Plan.TargetWorldRevision;
    REQUIRE(client2.HandleRepairPlan(conflictingPlan).Status == PartyQuestClientRepairStatus::PlanConflict);
}

TEST_CASE("Coordinator rejects spoofed initiators and repairs a client after a broadcast gap", "[quest.party-state.coordinator]")
{
    const PartyQuestCampaignId campaignId{0x3030303030303030ull, 0x4040404040404040ull};
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

    NotifyPartyQuestRepairPlan deliveredPlan = *repairPlan.Response;
    deliveredPlan.CampaignId = campaignId;
    const auto repair = client8.HandleRepairPlan(deliveredPlan);
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

TEST_CASE("Client protocol ids survive transient PlayerId reuse without deterministic collisions", "[quest.party-state.coordinator.reconnect]")
{
    PartyQuestClientIdAllocator allocator(0xA5A5A5A5A5A5A5A5ull);
    std::unordered_set<uint64_t> issued;

    for (size_t i = 0; i < 4096; ++i)
    {
        const uint64_t id = allocator.Allocate();
        REQUIRE(id != 0);
        REQUIRE(issued.insert(id).second);
    }

    PartyQuestClientIdAllocator secondProcess(0x5A5A5A5A5A5A5A5Aull);
    for (size_t i = 0; i < 256; ++i)
        REQUIRE_FALSE(issued.contains(secondProcess.Allocate()));
}

TEST_CASE("Client submission tracking fails closed at its quest bound", "[quest.party-state.coordinator.submission][resource-budget]")
{
    PartyQuestClientSubmissionQueue queue;
    PartyQuestReplica replica;

    for (size_t i = 0;
         i < PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests;
         ++i)
    {
        const auto snapshot = BuildCoordinatorSnapshot(
            GameId(12, static_cast<uint32_t>(0xB000 + i)),
            10);
        REQUIRE(queue.QueueLatest(snapshot) ==
            PartyQuestClientSubmissionStatus::Queued);
    }
    REQUIRE(queue.GetQueuedCount() ==
        PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests);

    const auto overflow = BuildCoordinatorSnapshot(GameId(12, 0xF000), 20);
    REQUIRE(queue.QueueLatest(overflow) ==
        PartyQuestClientSubmissionStatus::ResourceLimitExceeded);
    REQUIRE(queue.Observe(overflow, replica).Status ==
        PartyQuestClientSubmissionStatus::ResourceLimitExceeded);

    const auto retained = BuildCoordinatorSnapshot(GameId(12, 0xB000), 30);
    REQUIRE(queue.QueueLatest(retained) ==
        PartyQuestClientSubmissionStatus::ReplacedQueued);
    REQUIRE(queue.GetQueuedCount() ==
        PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests);

    PartyQuestClientSubmissionQueue inFlight;
    for (size_t i = 0;
         i < PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests;
         ++i)
    {
        const auto snapshot = BuildCoordinatorSnapshot(
            GameId(13, static_cast<uint32_t>(0x10000 + i)),
            10);
        REQUIRE(inFlight.MarkInFlight(i + 1, snapshot));
    }
    REQUIRE_FALSE(inFlight.MarkInFlight(
        PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests + 1,
        BuildCoordinatorSnapshot(GameId(13, 0x20000), 20)));
    REQUIRE(inFlight.GetInFlightCount() ==
        PartyQuestProtocolResourcePolicy::MaxClientTrackedQuests);

    auto oversized = BuildCoordinatorSnapshot(GameId(13, 0x30000), 30);
    oversized.CompletedStages.resize(
        PartyQuestResourcePolicy::MaxSnapshotCollectionEntries + 1);
    PartyQuestClientSubmissionQueue oversizedQueue;
    REQUIRE(oversizedQueue.QueueLatest(oversized) ==
        PartyQuestClientSubmissionStatus::InvalidSnapshot);
    REQUIRE(oversizedQueue.Observe(oversized, replica).Status ==
        PartyQuestClientSubmissionStatus::InvalidSnapshot);
    REQUIRE_FALSE(oversizedQueue.MarkInFlight(1, oversized));
}

TEST_CASE("Terminal transport identities release bounded server sessions", "[quest.party-state.coordinator][resource-budget]")
{
    PartyQuestProtocolCoordinator coordinator;

    REQUIRE(coordinator.ConnectClient(1));
    REQUIRE_FALSE(coordinator.ForgetDisconnectedClient(1));
    REQUIRE(coordinator.DisconnectClient(1));
    REQUIRE(coordinator.FindSession(1));
    REQUIRE(coordinator.ForgetDisconnectedClient(1));
    REQUIRE_FALSE(coordinator.FindSession(1));

    for (uint32_t clientId = 2;
         clientId <= PartyQuestProtocolResourcePolicy::MaxSessions + 2;
         ++clientId)
    {
        REQUIRE(coordinator.ConnectClient(clientId));
        REQUIRE(coordinator.DisconnectClient(clientId));
        REQUIRE(coordinator.ForgetDisconnectedClient(clientId));
    }
}

TEST_CASE("Protocol identity caches fail closed without losing retained conflict semantics", "[quest.party-state.coordinator][resource-budget]")
{
    SECTION("server session and transaction bounds")
    {
        PartyQuestProtocolCoordinator coordinator;
        for (uint32_t clientId = 1;
             clientId <= PartyQuestProtocolResourcePolicy::MaxSessions;
             ++clientId)
        {
            REQUIRE(coordinator.ConnectClient(clientId));
        }
        REQUIRE_FALSE(coordinator.ConnectClient(
            static_cast<uint32_t>(PartyQuestProtocolResourcePolicy::MaxSessions + 1)));
        REQUIRE(coordinator.ConnectClient(1));

        const GameId questId(11, 0xA000);
        for (uint64_t i = 0;
             i < PartyQuestProtocolResourcePolicy::MaxTransactionsPerSession;
             ++i)
        {
            const auto result = coordinator.HandleTransaction(
                1,
                BuildCoordinatorRequest(i + 1, i + 10000, 1, questId, 1, 10));
            REQUIRE(result.Status == PartyQuestTransactionHandleStatus::Processed);
            REQUIRE(result.Response.Result.Status ==
                PartyQuestApplyStatus::RevisionMismatch);
        }

        const auto overflowRequest = BuildCoordinatorRequest(
            PartyQuestProtocolResourcePolicy::MaxTransactionsPerSession + 1,
            999999,
            1,
            questId,
            0,
            10);
        const auto overflow = coordinator.HandleTransaction(1, overflowRequest);
        REQUIRE(overflow.Status ==
            PartyQuestTransactionHandleStatus::ResourceLimitExceeded);
        REQUIRE(overflow.Response.Result.Status ==
            PartyQuestApplyStatus::ResourceLimitExceeded);
        REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 0);

        const auto first = BuildCoordinatorRequest(1, 10000, 1, questId, 1, 10);
        REQUIRE(coordinator.HandleTransaction(1, first).Status ==
            PartyQuestTransactionHandleStatus::DuplicateRequest);
        auto conflict = first;
        conflict.Transaction.TransactionId = 10001;
        REQUIRE(coordinator.HandleTransaction(1, conflict).Status ==
            PartyQuestTransactionHandleStatus::RequestIdConflict);
    }

    SECTION("server report and plan bounds")
    {
        PartyQuestProtocolCoordinator coordinator;
        REQUIRE(coordinator.ConnectClient(1));
        PartyQuestClientSession client(1);

        for (uint64_t i = 1;
             i <= PartyQuestProtocolResourcePolicy::MaxReportsAndPlansPerSession;
             ++i)
        {
            REQUIRE(coordinator.HandleReplicaReport(
                1, client.BuildReplicaReport(i, false)).Status ==
                PartyQuestReportHandleStatus::Generated);
        }

        REQUIRE(coordinator.HandleReplicaReport(
            1,
            client.BuildReplicaReport(
                PartyQuestProtocolResourcePolicy::MaxReportsAndPlansPerSession + 1,
                false)).Status == PartyQuestReportHandleStatus::ResourceLimitExceeded);
        REQUIRE(coordinator.HandleReplicaReport(
            1, client.BuildReplicaReport(1, false)).Status ==
            PartyQuestReportHandleStatus::DuplicateReport);
    }

    SECTION("applied transactions renew the bounded reply cache")
    {
        PartyQuestProtocolCoordinator coordinator;
        REQUIRE(coordinator.ConnectClient(1));

        const GameId questId(11, 0xA100);
        const auto applied = BuildCoordinatorRequest(1, 10000, 1, questId, 0, 10);
        const auto accepted = coordinator.HandleTransaction(1, applied);
        REQUIRE(accepted.Response.Result.Status == PartyQuestApplyStatus::Accepted);

        for (uint64_t i = 1;
             i < PartyQuestProtocolResourcePolicy::MaxTransactionsPerSession;
             ++i)
        {
            const auto rejected = coordinator.HandleTransaction(
                1,
                BuildCoordinatorRequest(i + 1, i + 10000, 1, questId, 99, 20));
            REQUIRE(rejected.Response.Result.Status ==
                PartyQuestApplyStatus::RevisionMismatch);
        }

        const auto renewed = coordinator.HandleTransaction(
            1,
            BuildCoordinatorRequest(
                PartyQuestProtocolResourcePolicy::MaxTransactionsPerSession + 1,
                999999,
                1,
                questId,
                1,
                30));
        REQUIRE(renewed.Status == PartyQuestTransactionHandleStatus::Processed);
        REQUIRE(renewed.Response.Result.Status ==
            PartyQuestApplyStatus::Accepted);

        const auto durableDuplicate = coordinator.HandleTransaction(1, applied);
        REQUIRE(durableDuplicate.Status == PartyQuestTransactionHandleStatus::Processed);
        REQUIRE(durableDuplicate.Response.Result.Status == PartyQuestApplyStatus::Duplicate);
        REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 2);
    }

    SECTION("client canonical and repair bounds")
    {
        PartyQuestClientSession client(1);
        const GameId questId(12, 0xB000);

        for (uint64_t i = 1;
             i <= PartyQuestProtocolResourcePolicy::MaxClientCanonicalUpdates;
             ++i)
        {
            NotifyPartyQuestCanonicalUpdate update;
            update.TransactionId = i;
            update.WorldRevision = i;
            update.InitiatorPlayerId = 1;
            update.CanonicalSnapshot = BuildCoordinatorSnapshot(
                questId, static_cast<uint16_t>(i));
            update.CanonicalSnapshot.Revision = i;
            update.CanonicalSnapshot.InitiatorPlayerId = 1;
            REQUIRE(client.HandleCanonicalUpdate(update) ==
                PartyQuestClientCanonicalStatus::Applied);
        }

        NotifyPartyQuestCanonicalUpdate overflow;
        overflow.TransactionId =
            PartyQuestProtocolResourcePolicy::MaxClientCanonicalUpdates + 1;
        overflow.WorldRevision = overflow.TransactionId;
        overflow.InitiatorPlayerId = 1;
        overflow.CanonicalSnapshot = BuildCoordinatorSnapshot(questId, 1);
        overflow.CanonicalSnapshot.Revision = overflow.WorldRevision;
        overflow.CanonicalSnapshot.InitiatorPlayerId = 1;
        REQUIRE(client.HandleCanonicalUpdate(overflow) ==
            PartyQuestClientCanonicalStatus::ResourceLimitExceeded);

        PartyQuestClientSession repairClient(1);
        const PartyQuestCampaignId campaign{0xCAFE, 0xBABE};
        NotifyPartyQuestRepairPlan firstPlan;
        for (uint64_t i = 1;
             i <= PartyQuestProtocolResourcePolicy::MaxClientRepairs;
             ++i)
        {
            NotifyPartyQuestRepairPlan plan;
            plan.ReportId = i;
            plan.PlanId = i;
            plan.CampaignId = campaign;
            plan.Plan.Status = PartyQuestRepairPlanStatus::UpToDate;
            if (i == 1)
                firstPlan = plan;
            REQUIRE(repairClient.HandleRepairPlan(plan).Status ==
                PartyQuestClientRepairStatus::NoChanges);
        }

        auto overflowPlan = firstPlan;
        overflowPlan.ReportId =
            PartyQuestProtocolResourcePolicy::MaxClientRepairs + 1;
        overflowPlan.PlanId = overflowPlan.ReportId;
        REQUIRE(repairClient.HandleRepairPlan(overflowPlan).Status ==
            PartyQuestClientRepairStatus::ResourceLimitExceeded);
        REQUIRE(repairClient.HandleRepairPlan(firstPlan).Status ==
            PartyQuestClientRepairStatus::Duplicate);
    }
}

TEST_CASE("Acknowledged repair epochs renew the bounded server cache", "[quest.party-state.coordinator][resource-budget][renewal]")
{
    PartyQuestProtocolCoordinator coordinator;
    REQUIRE(coordinator.ConnectClient(1));
    PartyQuestClientSession client(1);
    const PartyQuestCampaignId campaign{0x1111, 0x2222};
    RequestPartyQuestReplicaReport firstReport;

    for (uint64_t i = 1;
         i <= PartyQuestProtocolResourcePolicy::MaxReportsAndPlansPerSession;
         ++i)
    {
        auto report = client.BuildReplicaReport(i, false);
        if (i == 1)
            firstReport = report;
        const auto dispatch = coordinator.HandleReplicaReport(1, report);
        REQUIRE(dispatch.Status == PartyQuestReportHandleStatus::Generated);
        REQUIRE(dispatch.Response.has_value());

        auto plan = *dispatch.Response;
        plan.CampaignId = campaign;
        const auto repair = client.HandleRepairPlan(plan);
        REQUIRE(repair.Status == PartyQuestClientRepairStatus::NoChanges);
        REQUIRE(coordinator.HandleRepairAck(1, repair.Ack).Status ==
            PartyQuestAckHandleStatus::Verified);
    }

    const auto renewed = coordinator.HandleReplicaReport(
        1,
        client.BuildReplicaReport(
            PartyQuestProtocolResourcePolicy::MaxReportsAndPlansPerSession + 1,
            false));
    REQUIRE(renewed.Status == PartyQuestReportHandleStatus::Generated);
    REQUIRE(renewed.Response.has_value());

    RequestPartyQuestRepairAck evictedAck;
    evictedAck.PlanId = 1;
    REQUIRE(coordinator.HandleRepairAck(1, evictedAck).Status ==
        PartyQuestAckHandleStatus::UnknownPlan);
    REQUIRE(coordinator.HandleReplicaReport(1, firstReport).Status ==
        PartyQuestReportHandleStatus::Generated);
}

TEST_CASE("Client replica survives PlayerId rebind and resets only when CampaignId changes", "[quest.party-state.coordinator.reconnect]")
{
    const PartyQuestCampaignId campaignA{0xAAAA, 0x1111};
    const PartyQuestCampaignId campaignB{0xBBBB, 0x2222};
    const GameId questA(8, 0x7000);
    const GameId questB(9, 0x8000);

    PartyQuestState serverA;
    REQUIRE(serverA.Apply(BuildCoordinatorRequest(1, 100, 1, questA, 0, 20).Transaction).Status ==
            PartyQuestApplyStatus::Accepted);

    PartyQuestClientSession client(1);
    NotifyPartyQuestRepairPlan initialPlan;
    initialPlan.ReportId = 1;
    initialPlan.PlanId = 1;
    initialPlan.CampaignId = campaignA;
    initialPlan.Plan = PartyQuestRepairPlanner::Build(serverA, client.GetReplica().BuildReport());

    const auto initialRepair = client.HandleRepairPlan(initialPlan);
    REQUIRE(initialRepair.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE_FALSE(initialRepair.CampaignChanged);
    REQUIRE(client.GetCampaignId() == campaignA);
    REQUIRE(client.GetReplica().GetWorldRevision() == 1);
    REQUIRE(client.GetReplica().FindQuest(questA));

    REQUIRE(client.RebindClientId(2));
    REQUIRE(client.GetClientId() == 2);
    REQUIRE(client.GetCampaignId() == campaignA);
    REQUIRE(client.GetReplica().GetWorldRevision() == 1);
    REQUIRE(client.BuildReplicaReport(55, true).CampaignId == campaignA);

    PartyQuestState serverB;
    REQUIRE(serverB.Apply(BuildCoordinatorRequest(2, 200, 2, questB, 0, 30).Transaction).Status ==
            PartyQuestApplyStatus::Accepted);

    NotifyPartyQuestRepairPlan replacementPlan;
    replacementPlan.ReportId = 2;
    replacementPlan.PlanId = 1; // Reused server-local plan id after reconnect is valid.
    replacementPlan.CampaignId = campaignB;
    replacementPlan.Plan = PartyQuestRepairPlanner::Build(serverB, PartyQuestReplicaReport{});

    const auto replacementRepair = client.HandleRepairPlan(replacementPlan);
    REQUIRE(replacementRepair.Status == PartyQuestClientRepairStatus::Applied);
    REQUIRE(replacementRepair.CampaignChanged);
    REQUIRE(client.GetCampaignId() == campaignB);
    REQUIRE(client.GetReplica().GetWorldRevision() == 1);
    REQUIRE_FALSE(client.GetReplica().FindQuest(questA));
    REQUIRE(client.GetReplica().FindQuest(questB));
}

TEST_CASE("Snapshots observed before campaign verification stay coalesced and unsent", "[quest.party-state.coordinator.submission]")
{
    PartyQuestClientSubmissionQueue queue;
    PartyQuestReplica replica;
    const GameId questId(10, 0x9000);

    const QuestSnapshot first = BuildCoordinatorSnapshot(questId, 10);
    const QuestSnapshot second = BuildCoordinatorSnapshot(questId, 20);
    const QuestSnapshot latest = BuildCoordinatorSnapshot(questId, 40);

    REQUIRE(queue.QueueLatest(first) == PartyQuestClientSubmissionStatus::Queued);
    REQUIRE(queue.QueueLatest(second) == PartyQuestClientSubmissionStatus::ReplacedQueued);
    REQUIRE(queue.QueueLatest(latest) == PartyQuestClientSubmissionStatus::ReplacedQueued);
    REQUIRE(queue.QueueLatest(latest) == PartyQuestClientSubmissionStatus::Duplicate);
    REQUIRE(queue.GetInFlightCount() == 0);
    REQUIRE(queue.GetQueuedCount() == 1);

    const auto ready = queue.TakeReady(replica);
    REQUIRE(ready.size() == 1);
    REQUIRE(ready.front().CurrentStage == 40);
    REQUIRE(queue.GetQueuedCount() == 0);
}
