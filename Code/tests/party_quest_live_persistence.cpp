#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestProtocol.h>
#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kLivePersistenceCampaign{
    0xABCDEF0123456789ull,
    0x9876543210ABCDEFull};

QuestSnapshot BuildLivePersistenceSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{10, QuestObjectiveState::Completed}};
    return snapshot;
}

RequestPartyQuestTransaction BuildLivePersistenceRequest(
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
    request.Transaction.ProposedSnapshot = BuildLivePersistenceSnapshot(aQuestId, aStage);
    return request;
}

std::filesystem::path MakeLivePersistencePath()
{
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("tp_party_quest_live_" + std::to_string(suffix) + ".bin");
}

void RemoveLivePersistenceFiles(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);

    auto backup = acPath;
    backup += ".bak";
    std::filesystem::remove(backup, ec);

    auto temporary = acPath;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
}
} // namespace

TEST_CASE("Durable coordinator state survives restart and keeps transaction deduplication", "[quest.party-state.persistence.live]")
{
    const std::filesystem::path path = MakeLivePersistencePath();
    RemoveLivePersistenceFiles(path);

    const GameId questId(8, 0x7000);
    const auto firstRequest = BuildLivePersistenceRequest(100, 500, 1, questId, 0, 10);

    PartyQuestProtocolCoordinator firstCoordinator;
    firstCoordinator.SetDurableCommitHandler(
        [&path](const PartyQuestState& acState)
        {
            return PartyQuestStatePersistence::SaveAtomically(
                       path, kLivePersistenceCampaign, acState) ==
                PartyQuestPersistenceStatus::Success;
        });
    REQUIRE(firstCoordinator.ConnectClient(1));

    const auto firstDispatch = firstCoordinator.HandleTransaction(1, firstRequest);
    REQUIRE(firstDispatch.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(firstDispatch.Response.Result.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(firstDispatch.Broadcast.has_value());
    REQUIRE(firstCoordinator.GetCanonicalState().GetWorldRevision() == 1);

    auto loaded = PartyQuestStatePersistence::Load(path);
    REQUIRE(loaded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(loaded.CampaignId == kLivePersistenceCampaign);
    REQUIRE(loaded.State.has_value());
    REQUIRE(loaded.State->GetWorldRevision() == 1);
    REQUIRE(loaded.State->GetJournal().size() == 1);

    PartyQuestProtocolCoordinator restartedCoordinator;
    REQUIRE(restartedCoordinator.RestoreCanonicalState(std::move(*loaded.State)));
    restartedCoordinator.SetDurableCommitHandler(
        [&path](const PartyQuestState& acState)
        {
            return PartyQuestStatePersistence::SaveAtomically(
                       path, kLivePersistenceCampaign, acState) ==
                PartyQuestPersistenceStatus::Success;
        });
    REQUIRE(restartedCoordinator.ConnectClient(1));

    auto redelivered = firstRequest;
    redelivered.RequestId = 101;
    const auto duplicateDispatch = restartedCoordinator.HandleTransaction(1, redelivered);
    REQUIRE(duplicateDispatch.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(duplicateDispatch.Response.Result.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE_FALSE(duplicateDispatch.Broadcast.has_value());
    REQUIRE(restartedCoordinator.GetCanonicalState().GetWorldRevision() == 1);

    const auto secondDispatch = restartedCoordinator.HandleTransaction(
        1,
        BuildLivePersistenceRequest(102, 501, 1, questId, 1, 30));
    REQUIRE(secondDispatch.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(secondDispatch.Response.Result.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(secondDispatch.Broadcast.has_value());
    REQUIRE(restartedCoordinator.GetCanonicalState().GetWorldRevision() == 2);

    auto reloaded = PartyQuestStatePersistence::Load(path);
    REQUIRE(reloaded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(reloaded.CampaignId == kLivePersistenceCampaign);
    REQUIRE(reloaded.State.has_value());
    REQUIRE(reloaded.State->GetWorldRevision() == 2);
    REQUIRE(reloaded.State->GetJournal().size() == 2);
    REQUIRE(reloaded.State->FindQuest(questId));
    REQUIRE(reloaded.State->FindQuest(questId)->CurrentStage == 30);

    RemoveLivePersistenceFiles(path);
}

TEST_CASE("Failed durable commit is not published cached or applied", "[quest.party-state.persistence.live]")
{
    PartyQuestProtocolCoordinator coordinator;
    bool allowCommit = false;
    size_t commitAttempts = 0;
    coordinator.SetDurableCommitHandler(
        [&allowCommit, &commitAttempts](const PartyQuestState&)
        {
            ++commitAttempts;
            return allowCommit;
        });
    REQUIRE(coordinator.ConnectClient(4));

    const GameId questId(9, 0x8000);
    const auto request = BuildLivePersistenceRequest(200, 600, 4, questId, 0, 20);

    const auto failed = coordinator.HandleTransaction(4, request);
    REQUIRE(failed.Status == PartyQuestTransactionHandleStatus::PersistenceFailure);
    REQUIRE(failed.Response.Result.Status == PartyQuestApplyStatus::TransactionConflict);
    REQUIRE(failed.Response.Result.WorldRevision == 0);
    REQUIRE_FALSE(failed.Broadcast.has_value());
    REQUIRE(failed.Recipients.empty());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 0);
    REQUIRE(coordinator.GetCanonicalState().FindQuest(questId) == nullptr);
    REQUIRE(commitAttempts == 1);

    allowCommit = true;
    const auto retried = coordinator.HandleTransaction(4, request);
    REQUIRE(retried.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(retried.Response.Result.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(retried.Broadcast.has_value());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 1);
    REQUIRE(commitAttempts == 2);

    const auto cached = coordinator.HandleTransaction(4, request);
    REQUIRE(cached.Status == PartyQuestTransactionHandleStatus::DuplicateRequest);
    REQUIRE(cached.Response == retried.Response);
    REQUIRE(commitAttempts == 2);
}

TEST_CASE("Production durability policy rejects an authoritative commit without a persistence barrier", "[quest.party-state.persistence.live][durability-required]")
{
    PartyQuestProtocolCoordinator coordinator;
    coordinator.RequireDurableCommits();
    REQUIRE(coordinator.ConnectClient(4));

    const GameId questId(9, 0x8100);
    const auto request = BuildLivePersistenceRequest(210, 610, 4, questId, 0, 20);

    const auto unavailable = coordinator.HandleTransaction(4, request);
    REQUIRE(unavailable.Status == PartyQuestTransactionHandleStatus::PersistenceFailure);
    REQUIRE(unavailable.Response.Result.Status ==
        PartyQuestApplyStatus::TransactionConflict);
    REQUIRE_FALSE(unavailable.Broadcast.has_value());
    REQUIRE(unavailable.Recipients.empty());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 0);
    REQUIRE(coordinator.GetCanonicalState().FindQuest(questId) == nullptr);

    size_t commitAttempts{};
    coordinator.SetDurableCommitHandler(
        [&commitAttempts](const PartyQuestState&)
        {
            ++commitAttempts;
            return true;
        });
    const auto retried = coordinator.HandleTransaction(4, request);
    REQUIRE(retried.Status == PartyQuestTransactionHandleStatus::Processed);
    REQUIRE(retried.Response.Result.Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(retried.Broadcast.has_value());
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 1);
    REQUIRE(commitAttempts == 1);

    coordinator.SetDurableCommitHandler({});
    const auto duplicate = coordinator.HandleTransaction(4, request);
    REQUIRE(duplicate.Status == PartyQuestTransactionHandleStatus::DuplicateRequest);
    REQUIRE(duplicate.Response == retried.Response);
    REQUIRE(coordinator.GetCanonicalState().GetWorldRevision() == 1);
}

TEST_CASE("Canonical restore is only allowed before coordinator sessions exist", "[quest.party-state.persistence.live]")
{
    const GameId questId(10, 0x9000);
    PartyQuestState restoredState;
    REQUIRE(restoredState.Apply(
        BuildLivePersistenceRequest(300, 700, 5, questId, 0, 10).Transaction).Status ==
        PartyQuestApplyStatus::Accepted);

    PartyQuestProtocolCoordinator cleanCoordinator;
    REQUIRE(cleanCoordinator.RestoreCanonicalState(restoredState));
    REQUIRE(cleanCoordinator.GetCanonicalState().GetWorldRevision() == 1);

    PartyQuestProtocolCoordinator activeCoordinator;
    REQUIRE(activeCoordinator.ConnectClient(5));
    REQUIRE_FALSE(activeCoordinator.RestoreCanonicalState(std::move(restoredState)));
    REQUIRE(activeCoordinator.GetCanonicalState().GetWorldRevision() == 0);
}
