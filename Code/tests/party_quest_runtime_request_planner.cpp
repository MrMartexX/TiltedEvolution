#include <Structs/Skyrim/PartyQuestRuntimeRequestPlanner.h>

#include <catch2/catch.hpp>

namespace
{
constexpr uint64_t kTransactionId = 41001;
constexpr uint64_t kWorldRevision = 51001;
const GameId kQuestId(101, 0xB100);

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
    "Runtime request planner requires independent local authority before producing a request",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto snapshot = BuildCanonicalSnapshot();
    const auto localFacts = BuildAdmittedLocalFacts();
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);
    const PartyQuestCheckpointSidecarManifest sidecars;

    const auto result = PartyQuestRuntimeRequestPlanner::Build(
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

    const auto result = PartyQuestRuntimeRequestPlanner::Build(
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

    const auto result = PartyQuestRuntimeRequestPlanner::Build(
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
    "Runtime request planner rejects invalid transaction provenance before authority evaluation",
    "[quest.party-state.runtime-request-planner][runtime-authority]")
{
    const auto requirement = BuildRequirement();
    const auto compatibilityFacts = BuildFacts(requirement);

    SECTION("zero transaction")
    {
        const auto result = PartyQuestRuntimeRequestPlanner::Build(
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
        const auto result = PartyQuestRuntimeRequestPlanner::Build(
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
        const auto result = PartyQuestRuntimeRequestPlanner::Build(
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
