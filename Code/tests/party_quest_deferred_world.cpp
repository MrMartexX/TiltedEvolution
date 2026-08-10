#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestDeferredWorld.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestRuntimeSafetyProfile BuildDeferredAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 1;
    requirement.ResolvedRecordFingerprint = 0x11;
    requirement.WinningOverrideFingerprint = 0x22;
    requirement.ScriptFingerprint = 0x33;
    requirement.NativeAdapterFingerprint = 0x44;
    requirement.AdapterMutationComponents = PartyQuestVerificationComponent::QuestSnapshot;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestRuntimeApplyRequest BuildDeferredRequest(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aQuestRevision,
    uint64_t aWorldRevision,
    uint32_t aTargetBaseId)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = static_cast<uint16_t>(20 + aQuestRevision);
    snapshot.Revision = aQuestRevision;
    snapshot.ReferenceAliases = {
        {1, GameId(0, aTargetBaseId), false},
        {2, GameId(0, aTargetBaseId), false}
    };
    snapshot.LocationAliases = {{3, GameId(0, aTargetBaseId + 1)}};
    snapshot.Canonicalize();

    PartyQuestSyncFacts syncFacts;
    syncFacts.QuestType = 1;
    syncFacts.HasStages = true;
    syncFacts.IsDisplayedInHud = true;
    syncFacts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, syncFacts);
    REQUIRE(admission.IsAdmitted());

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aWorldRevision;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        admission,
        snapshot,
        BuildDeferredAuthorization(aQuestId));
    REQUIRE(request.Plan.Safety.IsRuntimeSafe());
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(HasPartyQuestApplyAction(request.Plan.Actions, PartyQuestApplyAction::WaitForWorldTargets));
    return request;
}
} // namespace

TEST_CASE("Deferred world queue retains only the newest canonical quest revision", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const GameId questId(40, 0x1000);

    const auto first = BuildDeferredRequest(1001, questId, 3, 50, 0x2000);
    const auto newer = BuildDeferredRequest(1002, questId, 4, 51, 0x3000);
    const auto stale = BuildDeferredRequest(1003, questId, 2, 49, 0x4000);

    REQUIRE(queue.Enqueue(first) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.Enqueue(newer) == PartyQuestDeferredWorldEnqueueStatus::ReplacedOlderQuestRevision);
    REQUIRE(queue.GetPendingCount() == 1);
    REQUIRE(queue.FindByTransaction(first.TransactionId) == nullptr);
    REQUIRE(queue.FindByTransaction(newer.TransactionId) != nullptr);
    REQUIRE(queue.FindByQuest(questId)->Request.CanonicalSnapshot.Revision == 4);

    REQUIRE(queue.Enqueue(stale) == PartyQuestDeferredWorldEnqueueStatus::Stale);
    REQUIRE(queue.GetPendingCount() == 1);
}

TEST_CASE("Deferred world queue exposes stable target identities but requires explicit runtime readiness", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildDeferredRequest(2001, GameId(41, 0x1000), 1, 60, 0x5000);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);

    const auto* entry = queue.FindByTransaction(request.TransactionId);
    REQUIRE(entry != nullptr);
    REQUIRE_FALSE(entry->Ready);
    REQUIRE(entry->ReferencedWorldTargets.size() == 2); // duplicate alias target is canonicalized away
    REQUIRE(entry->ReferencedWorldTargets[0] == GameId(0, 0x5000));
    REQUIRE(entry->ReferencedWorldTargets[1] == GameId(0, 0x5001));

    REQUIRE(queue.TakeReady().empty());
    REQUIRE(queue.MarkReady(request.TransactionId));

    auto ready = queue.TakeReady();
    REQUIRE(ready.size() == 1);
    REQUIRE(ready[0].TransactionId == request.TransactionId);
    REQUIRE(queue.GetPendingCount() == 0);
}

TEST_CASE("Deferred world queue is idempotent and detects transaction reuse conflicts", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildDeferredRequest(3001, GameId(42, 0x1000), 1, 70, 0x6000);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Duplicate);

    auto conflict = request;
    conflict.TargetWorldRevision = 71;
    REQUIRE(queue.Enqueue(conflict) == PartyQuestDeferredWorldEnqueueStatus::TransactionConflict);
    REQUIRE(queue.GetPendingCount() == 1);
}

TEST_CASE("Deferred world queue rejects forged or stale mutation authorization", "[quest.party-state.deferred-world][mutation-authorization]")
{
    SECTION("missing authorization")
    {
        PartyQuestDeferredWorldQueue queue;
        auto request = BuildDeferredRequest(3501, GameId(42, 0x2000), 1, 75, 0x6500);
        request.Plan.MutationAuthorization = {};

        REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::UnsafePlan);
        REQUIRE(queue.GetPendingCount() == 0);
    }

    SECTION("snapshot changed after authorization")
    {
        PartyQuestDeferredWorldQueue queue;
        auto request = BuildDeferredRequest(3502, GameId(42, 0x3000), 1, 76, 0x6600);
        request.CanonicalSnapshot.CurrentStage += 1;
        request.CanonicalSnapshot.Canonicalize();

        REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::UnsafePlan);
        REQUIRE(queue.GetPendingCount() == 0);
    }
}

TEST_CASE("Same canonical quest revision with different transaction content fails closed", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const GameId questId(43, 0x1000);
    REQUIRE(queue.Enqueue(BuildDeferredRequest(4001, questId, 5, 80, 0x7000)) ==
        PartyQuestDeferredWorldEnqueueStatus::Queued);

    REQUIRE(queue.Enqueue(BuildDeferredRequest(4002, questId, 5, 81, 0x8000)) ==
        PartyQuestDeferredWorldEnqueueStatus::TransactionConflict);
    REQUIRE(queue.GetPendingCount() == 1);
}

TEST_CASE("Newer canonical observation invalidates deferred stale work before cell load", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const GameId questId(44, 0x1000);
    const auto request = BuildDeferredRequest(5001, questId, 6, 90, 0x9000);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);

    REQUIRE_FALSE(queue.InvalidateIfOlder(questId, 6));
    REQUIRE(queue.InvalidateIfOlder(questId, 7));
    REQUIRE(queue.GetPendingCount() == 0);
    REQUIRE_FALSE(queue.MarkReady(request.TransactionId));
}

TEST_CASE("Deferred world ready requests are emitted deterministically by canonical world revision", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const auto later = BuildDeferredRequest(6002, GameId(45, 0x2000), 1, 102, 0xA000);
    const auto earlier = BuildDeferredRequest(6001, GameId(45, 0x1000), 1, 101, 0xB000);

    REQUIRE(queue.Enqueue(later) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.Enqueue(earlier) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.MarkReady(later.TransactionId));
    REQUIRE(queue.MarkReady(earlier.TransactionId));

    const auto ready = queue.TakeReady();
    REQUIRE(ready.size() == 2);
    REQUIRE(ready[0].TargetWorldRevision == 101);
    REQUIRE(ready[1].TargetWorldRevision == 102);
}
