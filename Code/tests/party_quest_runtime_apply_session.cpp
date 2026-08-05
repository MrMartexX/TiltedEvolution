#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplySession.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

#include <vector>

namespace
{
const PartyQuestCampaignId kSessionCampaign{0xA1A2A3A4A5A6A7A8ull, 0xB1B2B3B4B5B6B7B8ull};
const PartyQuestPlayerProfileId kSessionPlayer{0xC1C2C3C4C5C6C7C8ull, 0xD1D2D3D4D5D6D7D8ull};

PartyQuestRuntimeSafetyProfile BuildSessionAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 7;
    requirement.ResolvedRecordFingerprint = 0x1100110011001100ull;
    requirement.WinningOverrideFingerprint = 0x2200220022002200ull;
    requirement.ScriptFingerprint = 0x3300330033003300ull;
    requirement.NativeAdapterFingerprint = 0x4400440044004400ull;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestAdmissionDecision BuildSessionAdmission(GameId aQuestId)
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

PartyQuestRuntimeApplyRequest BuildSessionRequest(uint64_t aTransactionId, GameId aQuestId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 30;
    snapshot.Revision = 3;
    snapshot.InitiatorPlayerId = 8;
    snapshot.CompletedStages = {10, 20, 30};
    snapshot.Objectives = {{30, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = 25;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildSessionAdmission(aQuestId),
        snapshot,
        BuildSessionAuthorization(aQuestId));
    REQUIRE(request.Plan.Safety.IsRuntimeSafe());
    return request;
}

struct DurableCapture
{
    bool Allow{true};
    bool Throw{};
    std::vector<PartyQuestRuntimeRecoveryState> States;

    bool Persist(const PartyQuestRuntimeRecoveryState& acState)
    {
        if (Throw)
            throw 42;
        if (!Allow)
            return false;
        States.push_back(acState);
        return true;
    }
};

PartyQuestRuntimeApplySession BuildSession(DurableCapture& aCapture)
{
    return PartyQuestRuntimeApplySession(
        kSessionCampaign,
        kSessionPlayer,
        [&aCapture](const PartyQuestRuntimeRecoveryState& acState)
        {
            return aCapture.Persist(acState);
        });
}
} // namespace

TEST_CASE("Durable runtime session persists every critical transition before publishing it", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(1001, GameId(31, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(capture.States.size() == 1);
    REQUIRE(capture.States.back().CampaignId == kSessionCampaign);
    REQUIRE(capture.States.back().PlayerProfileId == kSessionPlayer);
    REQUIRE(capture.States.back().Active->State == PartyQuestRuntimeApplyState::AwaitingCheckpoint);

    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(capture.States.back().Active->State == PartyQuestRuntimeApplyState::ReadyToApply);

    REQUIRE(session.ArmRuntimeMutation(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(capture.States.back().Active->State == PartyQuestRuntimeApplyState::WaitingForPapyrus);
    REQUIRE(capture.States.back().Active->RuntimeMutationMayHaveOccurred);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);

    REQUIRE(session.MarkPapyrusQuiescent(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(capture.States.back().Active->State == PartyQuestRuntimeApplyState::Verifying);

    const auto firstSample = session.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot);
    REQUIRE(firstSample.Verification == PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE_FALSE(firstSample.PersistenceFailed);

    const auto secondSample = session.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot);
    REQUIRE(secondSample.Verification == PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE_FALSE(secondSample.PersistenceFailed);
    REQUIRE(capture.States.back().Active->State == PartyQuestRuntimeApplyState::ReadyToCommit);

    REQUIRE(session.Commit(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE(session.GetCoordinator().IsCommitted(request.TransactionId));
    REQUIRE(capture.States.back().Active == std::nullopt);
    REQUIRE(capture.States.back().Committed.size() == 1);
    REQUIRE(capture.States.back().Committed[0].TransactionId == request.TransactionId);
}

TEST_CASE("Failed persistence never publishes a newly started critical repair", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    capture.Allow = false;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(2001, GameId(32, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::PersistenceFailure);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(session.GetCoordinator().IsSaveGuardActive());
    REQUIRE(capture.States.empty());
}

TEST_CASE("Mutation arm persistence failure leaves runtime mutation explicitly unarmed", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(3001, GameId(33, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);

    capture.Allow = false;
    REQUIRE(session.ArmRuntimeMutation(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToApply);
    REQUIRE_FALSE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);
    REQUIRE(session.GetCoordinator().IsSaveGuardActive());
}

TEST_CASE("Commit persistence failure keeps verified repair guarded and uncommitted", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(4001, GameId(34, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.ArmRuntimeMutation(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.MarkPapyrusQuiescent(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE_FALSE(session.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot).PersistenceFailed);
    REQUIRE(session.SubmitResnapshot(request.TransactionId, request.CanonicalSnapshot).Verification ==
        PartyQuestRuntimeVerificationStatus::Stable);

    capture.Allow = false;
    REQUIRE(session.Commit(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::ReadyToCommit);
    REQUIRE(session.GetCoordinator().IsSaveGuardActive());
    REQUIRE_FALSE(session.GetCoordinator().IsCommitted(request.TransactionId));
}

TEST_CASE("Live abort cannot clear a possibly mutated repair before checkpoint restoration", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(5001, GameId(35, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.ArmRuntimeMutation(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);

    REQUIRE(session.AbortBeforeMutation(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->RuntimeMutationMayHaveOccurred);

    REQUIRE(session.CompleteLiveCheckpointRestore(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE(capture.States.back().Active == std::nullopt);
}

TEST_CASE("Pre-mutation abort is durable and needs no checkpoint rollback", "[quest.party-state.runtime-apply.session]")
{
    DurableCapture capture;
    auto session = BuildSession(capture);
    const auto request = BuildSessionRequest(6001, GameId(36, 0x1000));

    REQUIRE(session.Begin(request) == PartyQuestRuntimeDurableBeginStatus::Started);
    REQUIRE(session.MarkCheckpointCreated(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.AbortBeforeMutation(request.TransactionId) == PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE(capture.States.back().Active == std::nullopt);
}

TEST_CASE("Crash recovery barrier is not cleared in memory when persistence fails", "[quest.party-state.runtime-apply.session]")
{
    const auto request = BuildSessionRequest(7001, GameId(37, 0x1000));

    PartyQuestRuntimeApplyCoordinator crashed;
    REQUIRE(crashed.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(crashed.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(crashed.MarkApplyDispatched(request.TransactionId));
    const auto recoveryState = crashed.ExportRecoveryState(kSessionCampaign, kSessionPlayer);

    DurableCapture capture;
    auto session = BuildSession(capture);
    REQUIRE(session.RestoreRecoveryState(recoveryState) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());

    capture.Allow = false;
    REQUIRE(session.CompleteCrashCheckpointRestore(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::PersistenceFailure);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);

    capture.Allow = true;
    REQUIRE(session.CompleteCrashCheckpointRestore(request.TransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() == nullptr);
}

TEST_CASE("Invalid campaign player or missing durable handler cannot arm runtime repair", "[quest.party-state.runtime-apply.session]")
{
    const auto request = BuildSessionRequest(8001, GameId(38, 0x1000));

    PartyQuestRuntimeApplySession invalidCampaign(
        {},
        kSessionPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; });
    REQUIRE(invalidCampaign.Begin(request) == PartyQuestRuntimeDurableBeginStatus::InvalidRequest);

    PartyQuestRuntimeApplySession invalidPlayer(
        kSessionCampaign,
        {},
        [](const PartyQuestRuntimeRecoveryState&) { return true; });
    REQUIRE(invalidPlayer.Begin(request) == PartyQuestRuntimeDurableBeginStatus::InvalidRequest);

    PartyQuestRuntimeApplySession noPersistence(kSessionCampaign, kSessionPlayer);
    REQUIRE(noPersistence.Begin(request) == PartyQuestRuntimeDurableBeginStatus::PersistenceFailure);
    REQUIRE(noPersistence.GetCoordinator().GetActive() == nullptr);
}
