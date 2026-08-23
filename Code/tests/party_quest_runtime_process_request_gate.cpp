#include <Structs/Skyrim/PartyQuestRuntimeProcessRequestGate.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_process_owner_test_support.h>
#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kCampaign{
    0xAA11000000000001ull,
    0xAA22000000000002ull};
const PartyQuestCampaignId kOtherCampaign{
    0xBB11000000000001ull,
    0xBB22000000000002ull};
const PartyQuestPlayerProfileId kPlayer{
    0xCC11000000000001ull,
    0xCC22000000000002ull};
const GameId kQuest(51, 0x5100);

QuestSnapshot BuildSnapshot(uint64_t aRevision = 7, uint16_t aStage = 40)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = kQuest;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.Revision = aRevision;
    snapshot.InitiatorPlayerId = 9;
    snapshot.CompletedStages = {10, 20, aStage};
    snapshot.Objectives = {{aStage, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestRuntimeCanonicalCandidate BuildCandidate(
    uint64_t aTransaction = 51001,
    uint64_t aWorldRevision = 61001,
    uint64_t aQuestRevision = 7,
    uint16_t aStage = 40)
{
    PartyQuestRuntimeCanonicalCandidate candidate;
    candidate.CampaignId = kCampaign;
    candidate.TransactionId = aTransaction;
    candidate.WorldRevision = aWorldRevision;
    candidate.CanonicalSnapshot = BuildSnapshot(aQuestRevision, aStage);
    return candidate;
}

PartyQuestReplica BuildReplica(const PartyQuestRuntimeCanonicalCandidate& acCandidate)
{
    PartyQuestReplica replica;
    replica.ObserveLocalSnapshot(acCandidate.CanonicalSnapshot);
    replica.SetObservedWorldRevision(acCandidate.WorldRevision);
    return replica;
}

PartyQuestRuntimeCompatibilityRequirement BuildRequirement()
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = kQuest;
    requirement.ProfileVersion = 17;
    requirement.ResolvedRecordFingerprint = 0x5101510151015101ull;
    requirement.WinningOverrideFingerprint = 0x5202520252025202ull;
    requirement.ScriptFingerprint = 0x5303530353035303ull;
    requirement.NativeAdapterFingerprint = 0x5404540454045404ull;
    requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;
    return requirement;
}

PartyQuestRuntimeProcessPlanningEvidence BuildEvidence(
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

PartyQuestRuntimeCompatibilityManifest BuildManifest(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeCompatibilityManifest manifest;
    REQUIRE(manifest.AddRequirement(acRequirement));
    return manifest;
}
} // namespace

TEST_CASE(
    "process request gate plans only the exact current canonical head under the bound process owner",
    "[quest.party-state.runtime-request][process-owner][provenance]")
{
    PartyQuestRuntimeProcessOwnerTestScope scope(
        kCampaign,
        kPlayer,
        "tp_runtime_process_request_gate");

    const auto candidate = BuildCandidate();
    auto replica = BuildReplica(candidate);
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaign));
    REQUIRE(inbox.Observe(candidate, replica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto requirement = BuildRequirement();
    const auto manifest = BuildManifest(requirement);
    size_t samples = 0;
    const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
        inbox,
        replica,
        kQuest,
        manifest,
        [&](const PartyQuestRuntimeCanonicalCandidate& acObserved)
            -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
        {
            ++samples;
            REQUIRE(acObserved == candidate);
            return BuildEvidence(requirement);
        });

    REQUIRE(result.IsPlanned());
    REQUIRE(result.RuntimeGeneration ==
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration());
    REQUIRE(samples == 1);
    REQUIRE(result.Request->TransactionId == candidate.TransactionId);
    REQUIRE(result.Request->TargetWorldRevision == candidate.WorldRevision);
    REQUIRE(result.Request->CanonicalSnapshot == candidate.CanonicalSnapshot);
    REQUIRE(result.Request->Plan.DryRunOnly);
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    REQUIRE(scope.RuntimeSession().GetCoordinator().GetActive() == nullptr);
}

TEST_CASE(
    "process request gate refuses missing process ownership and campaign mismatch before sampling evidence",
    "[quest.party-state.runtime-request][process-owner][fail-closed]")
{
    const auto candidate = BuildCandidate();
    auto replica = BuildReplica(candidate);
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaign));
    REQUIRE(inbox.Observe(candidate, replica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);
    const auto requirement = BuildRequirement();
    const auto manifest = BuildManifest(requirement);

    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    size_t samples = 0;
    auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
        inbox,
        replica,
        kQuest,
        manifest,
        [&](const PartyQuestRuntimeCanonicalCandidate&)
            -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
        {
            ++samples;
            return BuildEvidence(requirement);
        });
    REQUIRE(result.Status ==
        PartyQuestRuntimeProcessRequestStatus::ProcessOwnerUnavailable);
    REQUIRE(samples == 0);
    REQUIRE_FALSE(result.Request.has_value());

    PartyQuestRuntimeProcessOwnerTestScope wrongScope(
        kOtherCampaign,
        kPlayer,
        "tp_runtime_process_request_wrong_campaign");
    result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
        inbox,
        replica,
        kQuest,
        manifest,
        [&](const PartyQuestRuntimeCanonicalCandidate&)
            -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
        {
            ++samples;
            return BuildEvidence(requirement);
        });
    REQUIRE(result.Status == PartyQuestRuntimeProcessRequestStatus::CampaignMismatch);
    REQUIRE(samples == 0);
    REQUIRE_FALSE(result.Request.has_value());
}

TEST_CASE(
    "process request gate requires reviewed requirement and fresh matching local evidence",
    "[quest.party-state.runtime-request][compatibility][fail-closed]")
{
    PartyQuestRuntimeProcessOwnerTestScope scope(
        kCampaign,
        kPlayer,
        "tp_runtime_process_request_evidence");
    const auto candidate = BuildCandidate();
    auto replica = BuildReplica(candidate);
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaign));
    REQUIRE(inbox.Observe(candidate, replica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto requirement = BuildRequirement();

    SECTION("requirement unavailable")
    {
        PartyQuestRuntimeCompatibilityManifest emptyManifest;
        size_t samples = 0;
        const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
            inbox,
            replica,
            kQuest,
            emptyManifest,
            [&](const PartyQuestRuntimeCanonicalCandidate&)
                -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
            {
                ++samples;
                return BuildEvidence(requirement);
            });
        REQUIRE(result.Status ==
            PartyQuestRuntimeProcessRequestStatus::RequirementUnavailable);
        REQUIRE(samples == 0);
        REQUIRE_FALSE(result.Request.has_value());
    }

    SECTION("fresh evidence unavailable")
    {
        const auto manifest = BuildManifest(requirement);
        const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
            inbox,
            replica,
            kQuest,
            manifest,
            [](const PartyQuestRuntimeCanonicalCandidate&)
                -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
            {
                return std::nullopt;
            });
        REQUIRE(result.Status == PartyQuestRuntimeProcessRequestStatus::EvidenceUnavailable);
        REQUIRE_FALSE(result.Request.has_value());
    }

    SECTION("compatibility mismatch")
    {
        const auto manifest = BuildManifest(requirement);
        const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
            inbox,
            replica,
            kQuest,
            manifest,
            [&](const PartyQuestRuntimeCanonicalCandidate&)
                -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
            {
                auto evidence = BuildEvidence(requirement);
                ++evidence.CompatibilityFacts.ScriptFingerprint;
                return evidence;
            });
        REQUIRE(result.Status == PartyQuestRuntimeProcessRequestStatus::PlannerRejected);
        REQUIRE(result.Planner.Status ==
            PartyQuestRuntimeRequestPlanStatus::CompatibilityRejected);
        REQUIRE_FALSE(result.Request.has_value());
    }
}

TEST_CASE(
    "process request gate drops a plan when canonical published head changes during fresh evidence sampling",
    "[quest.party-state.runtime-request][revalidation][race]")
{
    PartyQuestRuntimeProcessOwnerTestScope scope(
        kCampaign,
        kPlayer,
        "tp_runtime_process_request_revalidation");
    const auto candidate = BuildCandidate();
    auto replica = BuildReplica(candidate);
    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaign));
    REQUIRE(inbox.Observe(candidate, replica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    const auto requirement = BuildRequirement();
    const auto manifest = BuildManifest(requirement);
    const auto result = PartyQuestRuntimeProcessRequestGate::PlanLatest(
        inbox,
        replica,
        kQuest,
        manifest,
        [&](const PartyQuestRuntimeCanonicalCandidate&)
            -> std::optional<PartyQuestRuntimeProcessPlanningEvidence>
        {
            auto advanced = candidate;
            ++advanced.TransactionId;
            ++advanced.WorldRevision;
            ++advanced.CanonicalSnapshot.Revision;
            advanced.CanonicalSnapshot.CurrentStage = 50;
            advanced.CanonicalSnapshot.CompletedStages.push_back(50);
            advanced.CanonicalSnapshot.Canonicalize();
            replica = BuildReplica(advanced);
            REQUIRE(inbox.Observe(advanced, replica) ==
                PartyQuestRuntimeCanonicalObserveStatus::Superseded);
            return BuildEvidence(requirement);
        });

    REQUIRE(result.Status ==
        PartyQuestRuntimeProcessRequestStatus::CanonicalAuthorizationUnavailable);
    REQUIRE_FALSE(result.Request.has_value());
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    REQUIRE(scope.RuntimeSession().GetCoordinator().GetActive() == nullptr);
}
