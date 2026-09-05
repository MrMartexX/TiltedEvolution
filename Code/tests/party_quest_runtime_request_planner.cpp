#include <Structs/Skyrim/PartyQuestRepair.h>
#include <Structs/Skyrim/PartyQuestRuntimeRequestPlanner.h>

#include <catch2/catch.hpp>

class PartyQuestRuntimeRequestPlannerTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeRequestPlanResult BuildDiagnostic(
        uint64_t aTransactionId,
        uint64_t aTargetWorldRevision,
        QuestSnapshot aCanonicalSnapshot,
        const PartyQuestSyncFacts& acLocalSyncFacts,
        const PartyQuestRuntimeCompatibilityRequirement& acCompatibilityRequirement,
        const PartyQuestRuntimeCompatibilityFacts& acLocalCompatibilityFacts,
        const PartyQuestCheckpointSidecarManifest& acSidecarManifest)
    {
        return PartyQuestRuntimeRequestPlanner::BuildDiagnostic(
            aTransactionId,
            aTargetWorldRevision,
            std::move(aCanonicalSnapshot),
            acLocalSyncFacts,
            acCompatibilityRequirement,
            acLocalCompatibilityFacts,
            acSidecarManifest);
    }
};

namespace
{
constexpr uint64_t kTransactionId = 41001;
constexpr uint64_t kWorldRevision = 51001;
const GameId kQuestId(101, 0xB100);
const PartyQuestCampaignId kCampaignId{
    0xB001B002B003B004ull,
    0xB005B006B007B008ull};

QuestSnapshot BuildCanonicalSnapshot()
{
    QuestSnapshot snapshot;
    snapshot.QuestId = kQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 7;
    snapshot.InitiatorPlayerId = 19;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestRuntimeCanonicalCandidate BuildCanonicalCandidate()
{
    PartyQuestRuntimeCanonicalCandidate candidate;
    candidate.CampaignId = kCampaignId;
    candidate.TransactionId = kTransactionId;
    candidate.WorldRevision = kWorldRevision;
    candidate.CanonicalSnapshot = BuildCanonicalSnapshot();
    return candidate;
}

PartyQuestReplica BuildPublishedReplica(
    const PartyQuestRuntimeCanonicalCandidate& acCandidate)
{
    PartyQuestReplica replica;
    replica.ObserveLocalSnapshot(acCandidate.CanonicalSnapshot);
    replica.SetObservedWorldRevision(acCandidate.WorldRevision);
    return replica;
}

PartyQuestSyncFacts BuildAdmittedLocalFacts()
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    return facts;
}

PartyQuestSyncFacts BuildBlockedLocalFacts()
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 0;
    facts.HasStages = true;
    facts.IsDisplayedInHud = false;
    facts.HasDisplayName = false;
    return facts;
}

PartyQuestRuntimeCompatibilityRequirement BuildRequirement()
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = kQuestId;
    requirement.ProfileVersion = 41;
    requirement.ResolvedRecordFingerprint = 0xB201B202B203B204ull;
    requirement.WinningOverrideFingerprint = 0xB211B212B213B214ull;
    requirement.ScriptFingerprint = 0xB221B222B223B224ull;
    requirement.NativeAdapterFingerprint = 0xB231B232B233B234ull;
    requirement.AdapterMutationComponents =
        PartyQuestVerificationComponent::QuestSnapshot;
    return requirement;
}

PartyQuestRuntimeCompatibilityFacts BuildFacts(
    const PartyQuestRuntimeCompatibilityRequirement& acRequirement)
{
    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = acRequirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = acRequirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = acRequirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = acRequirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = acRequirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = acRequirement.AdapterMutationComponents;
    return facts;
}
} // namespace

TEST_CASE(
    "Production runtime request planner requires one-shot canonical inbox provenance",
    "[quest.party-state.runtime-request-planner][runtime-authority][provenance]")
{
    const auto candidate = BuildCanonicalCandidate();
    const auto publishedReplica = BuildPublishedReplica(candidate);
    const auto localFacts = BuildAdmittedLocalFacts();
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);
    const PartyQuestCheckpointSidecarManifest sidecars;

    PartyQuestRuntimeCanonicalInbox inbox;
    REQUIRE(inbox.BindCampaign(kCampaignId));
    REQUIRE(inbox.Observe(candidate, publishedReplica) ==
        PartyQuestRuntimeCanonicalObserveStatus::Accepted);

    auto authorization = inbox.TryAuthorizeLatest(kQuestId, publishedReplica);
    REQUIRE(authorization.has_value());
    REQUIRE(authorization->IsVerified());

    const auto result = PartyQuestRuntimeRequestPlanner::Build(
        candidate,
        std::move(*authorization),
        localFacts,
        requirement,
        compatibilityFacts,
        sidecars);

    REQUIRE(result.IsPlanned());
    REQUIRE_FALSE(authorization->IsVerified());
    REQUIRE(result.Request->TransactionId == kTransactionId);
    REQUIRE(result.Request->TargetWorldRevision == kWorldRevision);
    REQUIRE(result.Request->CanonicalSnapshot == candidate.CanonicalSnapshot);

    const auto replay = PartyQuestRuntimeRequestPlanner::Build(
        candidate,
        std::move(*authorization),
        localFacts,
        requirement,
        compatibilityFacts,
        sidecars);
    REQUIRE_FALSE(replay.IsPlanned());
    REQUIRE(replay.Status ==
        PartyQuestRuntimeRequestPlanStatus::CanonicalProvenanceRejected);
}

TEST_CASE(
    "Runtime request provenance cannot be forged or retargeted",
    "[quest.party-state.runtime-request-planner][runtime-authority][provenance]")
{
    const auto candidate = BuildCanonicalCandidate();
    const auto publishedReplica = BuildPublishedReplica(candidate);
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);

    SECTION("default capability")
    {
        PartyQuestRuntimeCanonicalAuthorization authorization;
        const auto result = PartyQuestRuntimeRequestPlanner::Build(
            candidate,
            std::move(authorization),
            BuildAdmittedLocalFacts(),
            requirement,
            compatibilityFacts,
            PartyQuestCheckpointSidecarManifest{});
        REQUIRE(result.Status ==
            PartyQuestRuntimeRequestPlanStatus::CanonicalProvenanceRejected);
        REQUIRE_FALSE(result.Request.has_value());
    }

    SECTION("authorized transaction cannot be retargeted")
    {
        PartyQuestRuntimeCanonicalInbox inbox;
        REQUIRE(inbox.BindCampaign(kCampaignId));
        REQUIRE(inbox.Observe(candidate, publishedReplica) ==
            PartyQuestRuntimeCanonicalObserveStatus::Accepted);
        auto authorization = inbox.TryAuthorizeLatest(kQuestId, publishedReplica);
        REQUIRE(authorization.has_value());

        auto tampered = candidate;
        ++tampered.WorldRevision;
        const auto result = PartyQuestRuntimeRequestPlanner::Build(
            tampered,
            std::move(*authorization),
            BuildAdmittedLocalFacts(),
            requirement,
            compatibilityFacts,
            PartyQuestCheckpointSidecarManifest{});
        REQUIRE(result.Status ==
            PartyQuestRuntimeRequestPlanStatus::CanonicalProvenanceRejected);
        REQUIRE_FALSE(result.Request.has_value());
        REQUIRE(authorization->IsVerified());
    }

    SECTION("authority is not issued after the published head advances")
    {
        PartyQuestRuntimeCanonicalInbox inbox;
        REQUIRE(inbox.BindCampaign(kCampaignId));
        REQUIRE(inbox.Observe(candidate, publishedReplica) ==
            PartyQuestRuntimeCanonicalObserveStatus::Accepted);

        auto advanced = candidate;
        advanced.TransactionId += 1;
        advanced.WorldRevision += 1;
        advanced.CanonicalSnapshot.Revision += 1;
        advanced.CanonicalSnapshot.CurrentStage = 50;
        advanced.CanonicalSnapshot.CompletedStages.push_back(50);
        advanced.CanonicalSnapshot.Canonicalize();
        const auto advancedReplica = BuildPublishedReplica(advanced);

        REQUIRE_FALSE(inbox.TryAuthorizeLatest(kQuestId, advancedReplica).has_value());
    }
}

TEST_CASE(
    "Runtime request planner diagnostic seam requires independent local authority before producing a request",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto snapshot = BuildCanonicalSnapshot();
    const auto localFacts = BuildAdmittedLocalFacts();
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);
    const PartyQuestCheckpointSidecarManifest sidecars;

    const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
        kTransactionId,
        kWorldRevision,
        snapshot,
        localFacts,
        requirement,
        compatibilityFacts,
        sidecars);

    REQUIRE(result.IsPlanned());
    REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::Planned);
    REQUIRE(result.AdmissionStatus == PartyQuestAdmissionStatus::SharedProvisional);
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::Authorized);
    REQUIRE(result.SafetyStatus == PartyQuestRuntimeSafetyStatus::RuntimeSafe);
    REQUIRE(result.Request.has_value());

    const auto& request = *result.Request;
    REQUIRE(request.TransactionId == kTransactionId);
    REQUIRE(request.TargetWorldRevision == kWorldRevision);
    REQUIRE(request.SidecarManifestFingerprint == sidecars.ComputeFingerprint());
    REQUIRE(request.CanonicalSnapshot == snapshot);
    REQUIRE(request.Plan.DryRunOnly);
    REQUIRE(request.Plan.MutationAuthorization.IsVerified());
    REQUIRE(PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(request).has_value());
}

TEST_CASE(
    "Server canonical data cannot bypass local admission in runtime request planner",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);

    const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
        kTransactionId,
        kWorldRevision,
        BuildCanonicalSnapshot(),
        BuildBlockedLocalFacts(),
        requirement,
        compatibilityFacts,
        PartyQuestCheckpointSidecarManifest{});

    REQUIRE_FALSE(result.IsPlanned());
    REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::AdmissionRejected);
    REQUIRE_FALSE(result.Request.has_value());
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::UnknownQuest);
    REQUIRE(result.SafetyStatus == PartyQuestRuntimeSafetyStatus::Blocked);
}

TEST_CASE(
    "Runtime request planner rejects stale or mismatched local compatibility evidence",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto requirement = BuildRequirement();
    auto compatibilityFacts = BuildFacts(requirement);
    ++compatibilityFacts.ScriptFingerprint;

    const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
        kTransactionId,
        kWorldRevision,
        BuildCanonicalSnapshot(),
        BuildAdmittedLocalFacts(),
        requirement,
        compatibilityFacts,
        PartyQuestCheckpointSidecarManifest{});

    REQUIRE_FALSE(result.IsPlanned());
    REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::CompatibilityRejected);
    REQUIRE(result.AdmissionStatus == PartyQuestAdmissionStatus::SharedProvisional);
    REQUIRE(result.CompatibilityStatus == PartyQuestRuntimeCompatibilityStatus::ScriptMismatch);
    REQUIRE_FALSE(result.Request.has_value());
}

TEST_CASE(
    "Runtime request planner diagnostic seam rejects invalid transaction provenance before authority evaluation",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);

    SECTION("zero transaction")
    {
        const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
            0,
            kWorldRevision,
            BuildCanonicalSnapshot(),
            BuildAdmittedLocalFacts(),
            requirement,
            compatibilityFacts,
            PartyQuestCheckpointSidecarManifest{});
        REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::InvalidInput);
        REQUIRE_FALSE(result.Request.has_value());
    }

    SECTION("zero canonical revision")
    {
        auto snapshot = BuildCanonicalSnapshot();
        snapshot.Revision = 0;
        const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
            kTransactionId,
            kWorldRevision,
            snapshot,
            BuildAdmittedLocalFacts(),
            requirement,
            compatibilityFacts,
            PartyQuestCheckpointSidecarManifest{});
        REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::InvalidInput);
        REQUIRE_FALSE(result.Request.has_value());
    }

    SECTION("compatibility contract for another quest")
    {
        auto mismatchedRequirement = requirement;
        mismatchedRequirement.QuestId = GameId(101, 0xB101);
        const auto result = PartyQuestRuntimeRequestPlannerTestAccess::BuildDiagnostic(
            kTransactionId,
            kWorldRevision,
            BuildCanonicalSnapshot(),
            BuildAdmittedLocalFacts(),
            mismatchedRequirement,
            BuildFacts(mismatchedRequirement),
            PartyQuestCheckpointSidecarManifest{});
        REQUIRE(result.Status == PartyQuestRuntimeRequestPlanStatus::InvalidInput);
        REQUIRE_FALSE(result.Request.has_value());
    }
}
