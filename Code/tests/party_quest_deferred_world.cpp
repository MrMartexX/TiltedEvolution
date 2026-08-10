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
PartyQuestRuntimeSafetyProfile BuildDeferredAuthorization(
    GameId aQuestId,
    uint32_t aProfileVersion = 1)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = aProfileVersion;
    requirement.ResolvedRecordFingerprint = 0x11 + aProfileVersion;
    requirement.WinningOverrideFingerprint = 0x22 + aProfileVersion;
    requirement.ScriptFingerprint = 0x33 + aProfileVersion;
    requirement.NativeAdapterFingerprint = 0x44 + aProfileVersion;
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
    uint32_t aTargetBaseId,
    uint32_t aCompatibilityProfileVersion = 1)
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
        BuildDeferredAuthorization(aQuestId, aCompatibilityProfileVersion));
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
    REQUIRE(queue.MarkReady(request));

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

TEST_CASE("Deferred world readiness rejects stale sidecar and compatibility identity", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const auto request = BuildDeferredRequest(3201, GameId(42, 0x1800), 1, 72, 0x6200);
    REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);

    auto changedSidecars = request;
    ++changedSidecars.SidecarManifestFingerprint;
    REQUIRE(queue.Enqueue(changedSidecars) ==
        PartyQuestDeferredWorldEnqueueStatus::TransactionConflict);
    REQUIRE_FALSE(queue.MarkReady(changedSidecars));
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);

    auto changedCompatibility = BuildDeferredRequest(
        request.TransactionId,
        request.CanonicalSnapshot.QuestId,
        request.CanonicalSnapshot.Revision,
        request.TargetWorldRevision,
        0x6200,
        2);
    REQUIRE(queue.Enqueue(changedCompatibility) ==
        PartyQuestDeferredWorldEnqueueStatus::TransactionConflict);
    REQUIRE_FALSE(queue.MarkReady(changedCompatibility));
    REQUIRE_FALSE(queue.FindByTransaction(request.TransactionId)->Ready);

    REQUIRE(queue.MarkReady(request));
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
    REQUIRE_FALSE(queue.MarkReady(request));
}

TEST_CASE("Deferred world ready requests are emitted deterministically by canonical world revision", "[quest.party-state.deferred-world]")
{
    PartyQuestDeferredWorldQueue queue;
    const auto later = BuildDeferredRequest(6002, GameId(45, 0x2000), 1, 102, 0xA000);
    const auto earlier = BuildDeferredRequest(6001, GameId(45, 0x1000), 1, 101, 0xB000);

    REQUIRE(queue.Enqueue(later) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.Enqueue(earlier) == PartyQuestDeferredWorldEnqueueStatus::Queued);
    REQUIRE(queue.MarkReady(later));
    REQUIRE(queue.MarkReady(earlier));

    const auto ready = queue.TakeReady();
    REQUIRE(ready.size() == 2);
    REQUIRE(ready[0].TargetWorldRevision == 101);
    REQUIRE(ready[1].TargetWorldRevision == 102);
}

TEST_CASE("Deferred world queue fails closed at local pending and transaction-history bounds", "[quest.party-state.deferred-world][resource-bounds]")
{
    SECTION("pending quest bound")
    {
        PartyQuestDeferredWorldQueue queue;
        for (size_t index = 0; index < PartyQuestDeferredWorldQueue::MaxPendingEntries; ++index)
        {
            const auto request = BuildDeferredRequest(
                7000 + index,
                GameId(50, static_cast<uint32_t>(0x1000 + index)),
                1,
                200 + index,
                static_cast<uint32_t>(0x2000 + index * 2));
            REQUIRE(queue.Enqueue(request) == PartyQuestDeferredWorldEnqueueStatus::Queued);
        }

        const auto overflow = BuildDeferredRequest(
            8000,
            GameId(51, 0x1000),
            1,
            900,
            0x5000);
        REQUIRE(queue.Enqueue(overflow) ==
            PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded);
        REQUIRE(queue.GetPendingCount() == PartyQuestDeferredWorldQueue::MaxPendingEntries);
    }

    SECTION("remembered transaction bound")
    {
        PartyQuestDeferredWorldQueue queue;
        const GameId questId(52, 0x1000);
        for (size_t index = 0;
             index < PartyQuestDeferredWorldQueue::MaxRememberedTransactions;
             ++index)
        {
            const auto request = BuildDeferredRequest(
                10000 + index,
                questId,
                1 + index,
                1000 + index,
                static_cast<uint32_t>(0x6000 + index));
            const auto expected = index == 0
                ? PartyQuestDeferredWorldEnqueueStatus::Queued
                : PartyQuestDeferredWorldEnqueueStatus::ReplacedOlderQuestRevision;
            REQUIRE(queue.Enqueue(request) == expected);
        }

        const auto overflow = BuildDeferredRequest(
            20000,
            questId,
            PartyQuestDeferredWorldQueue::MaxRememberedTransactions + 1,
            6000,
            0x9000);
        REQUIRE(queue.Enqueue(overflow) ==
            PartyQuestDeferredWorldEnqueueStatus::ResourceLimitExceeded);
        REQUIRE(queue.GetRememberedTransactionCount() ==
            PartyQuestDeferredWorldQueue::MaxRememberedTransactions);
        REQUIRE(queue.GetPendingCount() == 1);
    }
}
