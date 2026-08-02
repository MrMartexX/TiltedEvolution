#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
PartyQuestAdmissionDecision BuildRecoveryAdmission(GameId aQuestId)
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(admission.IsAdmitted());
    return admission;
}

PartyQuestRuntimeApplyRequest BuildRecoveryRequest(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aTargetWorldRevision,
    bool aDeferred = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 5;
    snapshot.InitiatorPlayerId = 4;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    if (aDeferred)
        snapshot.ReferenceAliases = {{1, GameId(0, 0x1234), false}};
    snapshot.Canonicalize();

    PartyQuestRuntimeSafetyProfile profile;
    profile.HasVerifiedNativeAdapter = true;

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aTargetWorldRevision;
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRecoveryAdmission(aQuestId),
        snapshot,
        profile);
    REQUIRE(request.Plan.Safety.IsRuntimeSafe());
    return request;
}

void CommitRecoveryRequest(
    PartyQuestRuntimeApplyCoordinator& aCoordinator,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    const auto begin = aCoordinator.Begin(acRequest);
    if (begin == PartyQuestRuntimeApplyBeginStatus::Deferred)
        REQUIRE(aCoordinator.MarkWorldReady(acRequest.TransactionId));
    else
        REQUIRE(begin == PartyQuestRuntimeApplyBeginStatus::Started);

    REQUIRE(aCoordinator.MarkCheckpointCreated(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkApplyDispatched(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkPapyrusQuiescent(acRequest.TransactionId));
    REQUIRE(aCoordinator.SubmitResnapshot(acRequest.TransactionId, acRequest.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(aCoordinator.SubmitResnapshot(acRequest.TransactionId, acRequest.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(aCoordinator.Commit(acRequest.TransactionId));
}

void RemoveRuntimeApplyArchive(const std::filesystem::path& acPath)
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

TEST_CASE("Runtime apply recovery state encodes deterministically and round-trips", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    const auto committed = BuildRecoveryRequest(10001, GameId(11, 0x1000), 80);
    CommitRecoveryRequest(coordinator, committed);

    const auto deferred = BuildRecoveryRequest(10002, GameId(11, 0x2000), 81, true);
    REQUIRE(coordinator.Begin(deferred) == PartyQuestRuntimeApplyBeginStatus::Deferred);

    const PartyQuestRuntimeRecoveryState original = coordinator.ExportRecoveryState();
    REQUIRE(original.Committed.size() == 1);
    REQUIRE(original.Active.has_value());
    REQUIRE(original.Active->State == PartyQuestRuntimeApplyState::DeferredWorld);

    const auto first = PartyQuestRuntimeApplyPersistence::Encode(original);
    const auto second = PartyQuestRuntimeApplyPersistence::Encode(original);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);

    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(first);
    REQUIRE(decoded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(decoded.State.has_value());
    REQUIRE(*decoded.State == original);
}

TEST_CASE("Runtime apply persistence rejects corruption truncation and unsupported versions", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    CommitRecoveryRequest(coordinator, BuildRecoveryRequest(20001, GameId(12, 0x1000), 90));
    const auto encoded = PartyQuestRuntimeApplyPersistence::Encode(coordinator.ExportRecoveryState());
    REQUIRE(encoded.size() > 24);

    auto corrupted = encoded;
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(corrupted).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(truncated).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(unsupported).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::UnsupportedVersion);
}

TEST_CASE("Runtime apply persistence atomically saves and recovers its previous sidecar", "[quest.party-state.runtime-apply.persistence]")
{
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_runtime_apply_" + std::to_string(suffix) + ".bin");
    RemoveRuntimeApplyArchive(path);

    PartyQuestRuntimeApplyCoordinator firstCoordinator;
    CommitRecoveryRequest(firstCoordinator, BuildRecoveryRequest(30001, GameId(13, 0x1000), 100));
    const auto firstState = firstCoordinator.ExportRecoveryState();
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(path, firstState) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    PartyQuestRuntimeApplyCoordinator secondCoordinator;
    REQUIRE(secondCoordinator.RestoreRecoveryState(firstState) == PartyQuestRuntimeRecoveryDisposition::Clean);
    CommitRecoveryRequest(secondCoordinator, BuildRecoveryRequest(30002, GameId(13, 0x2000), 101));
    const auto secondState = secondCoordinator.ExportRecoveryState();
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(path, secondState) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    const auto recovered = PartyQuestRuntimeApplyPersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.UsedBackup);
    REQUIRE(*recovered.State == firstState);

    RemoveRuntimeApplyArchive(path);
}

TEST_CASE("Committed runtime transaction stays idempotent after restart", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(40001, GameId(14, 0x1000), 110);
    PartyQuestRuntimeApplyCoordinator beforeRestart;
    CommitRecoveryRequest(beforeRestart, request);

    const auto encoded = PartyQuestRuntimeApplyPersistence::Encode(beforeRestart.ExportRecoveryState());
    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(encoded);
    REQUIRE(decoded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(decoded.State.has_value());

    PartyQuestRuntimeApplyCoordinator afterRestart;
    REQUIRE(afterRestart.RestoreRecoveryState(*decoded.State) == PartyQuestRuntimeRecoveryDisposition::Clean);
    REQUIRE(afterRestart.IsCommitted(request.TransactionId));
    REQUIRE(afterRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted);
}

TEST_CASE("Crash after mutation blocks all new apply work until checkpoint restoration", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(50001, GameId(15, 0x1000), 120);
    PartyQuestRuntimeApplyCoordinator beforeCrash;
    REQUIRE(beforeCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(beforeCrash.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(beforeCrash.MarkApplyDispatched(request.TransactionId));

    const auto encoded = PartyQuestRuntimeApplyPersistence::Encode(beforeCrash.ExportRecoveryState());
    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(encoded);
    REQUIRE(decoded.State.has_value());

    PartyQuestRuntimeApplyCoordinator afterCrash;
    REQUIRE(afterCrash.RestoreRecoveryState(*decoded.State) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(afterCrash.IsRecoveryBlocked());
    REQUIRE(afterCrash.GetRecoveryRecord() != nullptr);
    REQUIRE_FALSE(afterCrash.IsSaveGuardActive());

    const auto unrelated = BuildRecoveryRequest(50002, GameId(15, 0x2000), 121);
    REQUIRE(afterCrash.Begin(unrelated) == PartyQuestRuntimeApplyBeginStatus::RecoveryBlocked);
    REQUIRE_FALSE(afterCrash.AcknowledgeCheckpointRestored(99999));
    REQUIRE(afterCrash.AcknowledgeCheckpointRestored(request.TransactionId));
    REQUIRE_FALSE(afterCrash.IsRecoveryBlocked());
    REQUIRE(afterCrash.GetRecoveryRecord() == nullptr);

    REQUIRE(afterCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
}

TEST_CASE("Deferred world work resumes after restart without falsely holding a save guard", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(60001, GameId(16, 0x1000), 130, true);
    PartyQuestRuntimeApplyCoordinator beforeRestart;
    REQUIRE(beforeRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Deferred);
    REQUIRE_FALSE(beforeRestart.IsSaveGuardActive());

    const auto encoded = PartyQuestRuntimeApplyPersistence::Encode(beforeRestart.ExportRecoveryState());
    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(encoded);
    REQUIRE(decoded.State.has_value());

    PartyQuestRuntimeApplyCoordinator afterRestart;
    REQUIRE(afterRestart.RestoreRecoveryState(*decoded.State) ==
        PartyQuestRuntimeRecoveryDisposition::DeferredRestored);
    REQUIRE(afterRestart.GetActive() != nullptr);
    REQUIRE(afterRestart.GetActive()->State == PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(afterRestart.IsSaveGuardActive());
    REQUIRE(afterRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::DuplicatePending);
    REQUIRE(afterRestart.MarkWorldReady(request.TransactionId));
    REQUIRE(afterRestart.IsSaveGuardActive());
}

TEST_CASE("Pre-mutation crash discards stale apply entry and requests a fresh plan", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(70001, GameId(17, 0x1000), 140);
    PartyQuestRuntimeApplyCoordinator beforeCrash;
    REQUIRE(beforeCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(beforeCrash.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(beforeCrash.GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE_FALSE(beforeCrash.GetActive()->RuntimeMutationMayHaveOccurred);

    const auto state = beforeCrash.ExportRecoveryState();
    PartyQuestRuntimeApplyCoordinator afterCrash;
    REQUIRE(afterCrash.RestoreRecoveryState(state) ==
        PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired);
    REQUIRE(afterCrash.GetActive() == nullptr);
    REQUIRE_FALSE(afterCrash.IsRecoveryBlocked());
    REQUIRE(afterCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
}

TEST_CASE("Inconsistent recovery markers fail closed", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeRecoveryState state;
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = 80001;
    active.TargetWorldRevision = 150;
    active.QuestId = GameId(18, 0x1000);
    active.CanonicalDigest = 0x12345678;
    active.Actions = PartyQuestApplyAction::AdapterManaged |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = false; // impossible after apply dispatch
    active.RuntimeMutationMayHaveOccurred = true;
    state.Active = active;

    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.RestoreRecoveryState(state) == PartyQuestRuntimeRecoveryDisposition::InvalidState);
    REQUIRE_FALSE(coordinator.IsRecoveryBlocked());
}
