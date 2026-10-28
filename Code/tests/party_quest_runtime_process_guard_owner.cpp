#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeGuardedSession.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_safety_test_access.h>
#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kProcessGuardOwnerCampaign{
    0xE101E102E103E104ull,
    0xE105E106E107E108ull};
const PartyQuestPlayerProfileId kProcessGuardOwnerPlayer{
    0xE201E202E203E204ull,
    0xE205E206E207E208ull};

PartyQuestRuntimeApplyRequest BuildImmediateProcessGuardRequest()
{
    QuestSnapshot snapshot;
    snapshot.QuestId = GameId(98, 0xA200);
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 20;
    snapshot.Revision = 4;
    snapshot.CompletedStages = {10, 20};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = 38001;
    request.TargetWorldRevision = 48001;
    request.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan.Safety.Status = PartyQuestRuntimeSafetyStatus::RuntimeSafe;
    request.Plan.Safety.Reason =
        PartyQuestRuntimeSafetyReason::VerifiedNativeAdapter;
    request.Plan.Actions = PartyQuestApplyAction::AdapterManaged |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    PartyQuestRuntimeSafetyTestAccess::AuthorizePlan(request.Plan, snapshot);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request).has_value());
    return request;
}
} // namespace

TEST_CASE(
    "Unowned immediate runtime session cannot acquire the process SaveGuard",
    "[quest.party-state.runtime-guard][runtime-owner][lifecycle]")
{
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    auto& processGuard = PartyQuestSaveGuard::GetProcessGuard();
    REQUIRE_FALSE(processOwner.IsBound());
    REQUIRE_FALSE(processGuard.IsActive());

    size_t persistenceCalls{};
    PartyQuestRuntimeApplySession privateSession(
        kProcessGuardOwnerCampaign,
        kProcessGuardOwnerPlayer,
        [&](const PartyQuestRuntimeRecoveryState&)
        {
            ++persistenceCalls;
            return true;
        },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    PartyQuestRuntimeGuardedSession privateGuarded(privateSession);
    const auto request = BuildImmediateProcessGuardRequest();

    const auto begin = privateGuarded.Begin(request);
    REQUIRE(begin.Status == PartyQuestRuntimeGuardStatus::InvalidState);
    REQUIRE_FALSE(begin.GuardHeld);
    REQUIRE(persistenceCalls == 0);
    REQUIRE(privateSession.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(processGuard.IsActive());
    REQUIRE_FALSE(processOwner.IsBound());
}
