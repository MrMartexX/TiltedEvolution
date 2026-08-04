#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <vector>

namespace
{
const PartyQuestCampaignId kGuardCampaign{0x3132333435363738ull, 0x4142434445464748ull};
const PartyQuestPlayerProfileId kGuardPlayer{0x5152535455565758ull, 0x6162636465666768ull};

PartyQuestRuntimeApplyRequest BuildGuardRequest(
    uint64_t aTransactionId,
    bool aDeferred = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(71, static_cast<uint32_t>(0x1000 + aTransactionId));
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 50;
    snapshot.Revision = 5;
    snapshot.InitiatorPlayerId = 9;
    snapshot.CompletedStages = {10, 30, 50};
    snapshot.Objectives = {{50, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 1800 + aTransactionId;
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason = PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    if (aDeferred)
        request.Plan.Actions |= PartyQuestApplyAction::WaitForWorldTargets;
    return request;
}

struct GuardDurability
{
    PartyQuestSaveGuard* Guard{};
    bool Allow{true};
    std::vector<bool> GuardActiveDuringPersist;
    std::vector<uint64_t> GuardTransactionDuringPersist;

    bool Persist(const PartyQuestRuntimeRecoveryState&)
    {
        GuardActiveDuringPersist.push_back(Guard && Guard->IsActive());
        GuardTransactionDuringPersist.push_back(
            Guard && Guard->IsActive() ? Guard->GetTransactionId() : 0);
        return Allow;
    }
};

PartyQuestRuntimeApplySession BuildGuardSession(GuardDurability& aDurability)
{
    return PartyQuestRuntimeApplySession(
        kGuardCampaign,
        kGuardPlayer,
        [&aDurability](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aDurability.Persist(acState);
        });
}

void AdvanceToReadyToCommit(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeApplySession& aSession,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    REQUIRE(aSession.MarkCheckpointCreated(acRequest.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(aGuarded.ArmRuntimeMutation(acRequest.TransactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(aGuarded.MarkPapyrusQuiescent(acRequest.TransactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(aGuarded.SubmitResnapshot(
                acRequest.TransactionId,
                acRequest.CanonicalSnapshot).Verification ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(aGuarded.SubmitResnapshot(
                acRequest.TransactionId,
                acRequest.CanonicalSnapshot).Verification ==
        PartyQuestRuntimeVerificationStatus::Stable);
}

struct GuardSandbox
{
    std::filesystem::path Root;

    GuardSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_guarded_session_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~GuardSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};
} // namespace

TEST_CASE("Immediate runtime begin acquires physical save guard before durable publication", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24001);

    const auto result = guarded.Begin(request);
    REQUIRE(result.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(result.GuardHeld);
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
    REQUIRE_FALSE(guarded.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE_FALSE(guarded.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE_FALSE(guarded.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guarded.CanSave(PartyQuestSaveKind::ControlledCheckpoint));
    REQUIRE(durability.GuardActiveDuringPersist.size() == 1);
    REQUIRE(durability.GuardActiveDuringPersist[0]);
    REQUIRE(durability.GuardTransactionDuringPersist[0] == request.TransactionId);
}

TEST_CASE("Failed immediate begin persistence releases only the newly acquired lease", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    durability.Allow = false;
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24002);

    const auto result = guarded.Begin(request);
    REQUIRE(result.Status == PartyQuestRuntimeGuardStatus::PersistenceFailure);
    REQUIRE(result.BeginStatus == PartyQuestRuntimeDurableBeginStatus::PersistenceFailure);
    REQUIRE_FALSE(saveGuard.IsActive());
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE(durability.GuardActiveDuringPersist.size() == 1);
    REQUIRE(durability.GuardActiveDuringPersist[0]);
}

TEST_CASE("Deferred repair holds no guard until world-ready transition", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24003, true);

    const auto begin = guarded.Begin(request);
    REQUIRE(begin.Status == PartyQuestRuntimeGuardStatus::Deferred);
    REQUIRE(begin.BeginStatus == PartyQuestRuntimeDurableBeginStatus::Deferred);
    REQUIRE_FALSE(saveGuard.IsActive());
    REQUIRE(durability.GuardActiveDuringPersist.size() == 1);
    REQUIRE_FALSE(durability.GuardActiveDuringPersist[0]);

    const auto ready = guarded.MarkWorldReady(request.TransactionId);
    REQUIRE(ready.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(ready.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
    REQUIRE(durability.GuardActiveDuringPersist.size() == 2);
    REQUIRE(durability.GuardActiveDuringPersist[1]);
    REQUIRE(durability.GuardTransactionDuringPersist[1] == request.TransactionId);
}

TEST_CASE("World-ready persistence failure releases lease and leaves transaction deferred", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24004, true);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Deferred);

    durability.Allow = false;
    const auto ready = guarded.MarkWorldReady(request.TransactionId);
    REQUIRE(ready.Status == PartyQuestRuntimeGuardStatus::PersistenceFailure);
    REQUIRE_FALSE(saveGuard.IsActive());
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::DeferredWorld);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->SaveGuardActive);
}

TEST_CASE("Durable commit is published while guard is held and releases it afterward", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24005);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    AdvanceToReadyToCommit(guarded, session, request);

    const size_t beforeCommit = durability.GuardActiveDuringPersist.size();
    const auto committed = guarded.Commit(request.TransactionId);
    REQUIRE(committed.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(committed.TransitionStatus == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(durability.GuardActiveDuringPersist.size() == beforeCommit + 1);
    REQUIRE(durability.GuardActiveDuringPersist.back());
    REQUIRE(durability.GuardTransactionDuringPersist.back() == request.TransactionId);
    REQUIRE_FALSE(saveGuard.IsActive());
    REQUIRE(session.GetCoordinator().IsCommitted(request.TransactionId));
}

TEST_CASE("Possible runtime mutation keeps save guard when abort requires checkpoint restore", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24006);
    REQUIRE(guarded.Begin(request).Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(guarded.ArmRuntimeMutation(request.TransactionId).Status ==
        PartyQuestRuntimeGuardStatus::Ready);

    const auto aborted = guarded.AbortBeforeMutation(request.TransactionId);
    REQUIRE(aborted.Status == PartyQuestRuntimeGuardStatus::CheckpointRestoreRequired);
    REQUIRE(aborted.TransitionStatus ==
        PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired);
    REQUIRE(saveGuard.GetTransactionId() == request.TransactionId);
    REQUIRE_FALSE(guarded.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
}

TEST_CASE("Loaded crash recovery reconstructs save guard and unresolved recovery retains it", "[quest.party-state.runtime-guard]")
{
    GuardSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kGuardCampaign,
        kGuardPlayer);
    REQUIRE(paths.has_value());

    PartyQuestRuntimeApplyEntry recovery;
    recovery.TransactionId = 24007;
    recovery.TargetWorldRevision = 1907;
    recovery.QuestId = GameId(71, 0x3000);
    recovery.CanonicalDigest = 0xCAFEBABE12345678ull;
    recovery.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    recovery.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    recovery.SaveGuardActive = true;
    recovery.CheckpointCreated = true;
    recovery.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kGuardCampaign;
    state.PlayerProfileId = kGuardPlayer;
    state.Active = recovery;

    PartyQuestSaveGuard saveGuard;
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    REQUIRE(session.RestoreRecoveryState(state) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);

    const auto reconciled = guarded.ReconcileLoadedState();
    REQUIRE(reconciled.Status == PartyQuestRuntimeGuardStatus::Ready);
    REQUIRE(reconciled.GuardHeld);
    REQUIRE(saveGuard.GetTransactionId() == recovery.TransactionId);

    const auto unresolved = guarded.ResolveCrashRecovery(*paths);
    REQUIRE(unresolved.Status == PartyQuestRuntimeRecoveryStatus::CheckpointMissing);
    REQUIRE(saveGuard.GetTransactionId() == recovery.TransactionId);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
}

TEST_CASE("Busy physical save guard blocks runtime begin without publishing durable state", "[quest.party-state.runtime-guard]")
{
    PartyQuestSaveGuard saveGuard;
    REQUIRE(saveGuard.Acquire(99999) == PartyQuestSaveGuardAcquireStatus::Acquired);
    GuardDurability durability{&saveGuard};
    auto session = BuildGuardSession(durability);
    PartyQuestRuntimeGuardedSession guarded(session, saveGuard);
    const auto request = BuildGuardRequest(24008);

    const auto result = guarded.Begin(request);
    REQUIRE(result.Status == PartyQuestRuntimeGuardStatus::GuardBusy);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE(durability.GuardActiveDuringPersist.empty());
    REQUIRE(saveGuard.GetTransactionId() == 99999);
}
