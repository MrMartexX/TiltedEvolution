#include <Structs/Skyrim/PartyQuestRuntimeProcessRequestGate.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_process_owner_test_support.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kQuiescenceCampaign{
    0xDA11000000000001ull,
    0xDA22000000000002ull};
const PartyQuestPlayerProfileId kQuiescencePlayer{
    0xDB11000000000001ull,
    0xDB22000000000002ull};
const GameId kQuiescenceQuest(53, 0x5300);

PartyQuestRuntimeCanonicalCandidate BuildQuiescenceCandidate()
{
    QuestSnapshot snapshot;
    snapshot.QuestId = kQuiescenceQuest;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 7;
    snapshot.InitiatorPlayerId = 9;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();

    PartyQuestRuntimeCanonicalCandidate candidate;
    candidate.CampaignId = kQuiescenceCampaign;
    candidate.TransactionId = 53001;
    candidate.WorldRevision = 63001;
    candidate.CanonicalSnapshot = snapshot;
    return candidate;
}

PartyQuestReplica BuildQuiescenceReplica(
    const PartyQuestRuntimeCanonicalCandidate& acCandidate)
{
    PartyQuestReplica replica;
    replica.ObserveLocalSnapshot(acCandidate.CanonicalSnapshot);
    replica.SetObservedWorldRevision(acCandidate.WorldRevision);
    return replica;
}

PartyQuestRuntimeCompatibilityRequirement BuildQuiescenceRequirement()
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = kQuiescenceQuest;
    requirement.ProfileVersion = 17;
    requirement.ResolvedRecordFingerprint = 0x6101610161016101ull;
    requirement.WinningOverrideFingerprint = 0x6202620262026202ull;
    requirement.ScriptFingerprint = 0x6303630363036303ull;
    requirement.NativeAdapterFingerprint = 0x6404640464046404ull;
    requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;
    return requirement;
}

PartyQuestRuntimeProcessPlanningEvidence BuildQuiescenceEvidence(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeProcessPlanningEvidence evidence;
    evidence.SyncFacts.QuestType = 1;
    evidence.SyncFacts.HasStages = true;
    evidence.SyncFacts.IsDisplayedInHud = true;
    evidence.SyncFacts.HasDisplayName = true;
    evidence.CompatibilityFacts.ProfileVersion = acRequirement.ProfileVersion;
    evidence.CompatibilityFacts.ResolvedRecordFingerprint =
        acRequirement.ResolvedRecordFingerprint;
    evidence.CompatibilityFacts.WinningOverrideFingerprint =
        acRequirement.WinningOverrideFingerprint;
    evidence.CompatibilityFacts.ScriptFingerprint =
        acRequirement.ScriptFingerprint;
    evidence.CompatibilityFacts.NativeAdapterFingerprint =
        acRequirement.NativeAdapterFingerprint;
    evidence.CompatibilityFacts.AdapterMutationComponents =
        acRequirement.AdapterMutationComponents;
    return evidence;
}
} // namespace

TEST_CASE(
    "process request planning pins owner lifetime and rejects reentrant lifecycle teardown",
    "[quest.party-state.runtime-request][process-owner][quiescence]")
{
    PartyQuestRuntimeProcessOwnerTestScope scope(
        kQuiescenceCampaign,
        kQuiescencePlayer,
        "tp_runtime_process_request_quiescence");

    const auto candidate = BuildQuiescenceCandidate();
    auto replica = BuildQuiescenceReplica(candidate);
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kQuiescenceCampaign));
    REQUIRE(inbox.Observe(candidate, replica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto requirement = BuildQuiescenceRequirement();
    PartyQuestRuntimeCompatibilityManifest manifest;
    REQUIRE(manifest.AddRequirement(requirement));

    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generationBefore = fence.GetGeneration();
    REQUIRE(generationBefore != 0);

    PartyQuestRuntimeLifecycleFenceResult reentrantLifecycle;
    size_t observations{};
    const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
        inbox,
        replica,
        kQuiescenceQuest,
        manifest,
        [&](const PartyQuestRuntimeCanonicalCandidate& acObserved)
            -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
        {
            ++observations;
            REQUIRE(acObserved == candidate);
            REQUIRE(fence.IsExecutionLeaseHeldByCurrentThread());
            REQUIRE(fence.GetGeneration() == generationBefore);

            // A lifecycle transition reached synchronously from an observer must
            // fail closed rather than waiting on the execution lease owned by
            // this same thread. The owner must remain intact for this callback.
            reentrantLifecycle = owner.PrepareAndRelease(
                PartyQuestRuntimeLifecycleEvent::PartyLeave);
            REQUIRE(reentrantLifecycle.Status ==
                PartyQuestRuntimeLifecycleFenceStatus::InvalidState);
            REQUIRE_FALSE(reentrantLifecycle.CanProceed());
            REQUIRE(owner.IsBound());
            REQUIRE(owner.GetRuntimeSession() != nullptr);
            REQUIRE(owner.GetGuardedSession() != nullptr);
            REQUIRE(fence.GetGeneration() == generationBefore);

            return BuildQuiescenceEvidence(requirement);
        });

    REQUIRE(result.IsPlanned());
    REQUIRE(result.RuntimeGeneration == generationBefore);
    REQUIRE(observations == 1);
    REQUIRE(owner.IsBound());
    REQUIRE_FALSE(fence.IsExecutionLeaseHeldByCurrentThread());

    // Once the callback has fully returned and released its execution lease, the
    // same lifecycle transition is free to acquire exclusive invalidation and
    // release the process-owned session.
    const auto released = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::PartyLeave);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE(fence.GetGeneration() != generationBefore);
}
