#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionBootstrap.h>

#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <type_traits>

class PartyQuestPlayerProfileLineageTestAccess final
{
public:
    static PartyQuestPlayerProfileLineageAuthorization Issue(
        PartyQuestPlayerProfileId aProfileId,
        uint64_t aRuntimeGeneration,
        bool aExactCharacterLineage = true,
        bool aPersistedWithCharacterLineage = true,
        bool aFilenameIndependent = true) noexcept
    {
        return PartyQuestPlayerProfileLineageAuthorization(
            aProfileId,
            aRuntimeGeneration,
            aExactCharacterLineage,
            aPersistedWithCharacterLineage,
            aFilenameIndependent);
    }

    static PartyQuestPlayerProfileLineageAuthorization ResolveBridgeSnapshots(
        const PartyQuestLineageProviderDescriptor& acProvider,
        const PartyQuestLineageRuntimeVersion& acExpectedRuntime,
        const PartyQuestLineageBridgeSnapshot& acFirst,
        const PartyQuestLineageBridgeSnapshot& acSecond,
        uint64_t aRuntimeGeneration) noexcept
    {
        return PartyQuestSkyrimPlayerProfileLineageResolver::
            ResolveStableSnapshots(
                acProvider,
                acExpectedRuntime,
                acFirst,
                acSecond,
                aRuntimeGeneration);
    }
};

class PartyQuestRuntimeSessionBootstrapTestAccess final
{
public:
    [[nodiscard]] static PartyQuestRuntimeSessionBootstrapResult
    BindIgnoringLifecycleCoverage(
        const std::filesystem::path& acCoopReplicaRoot,
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileLineageAuthorization& acPlayerProfile) noexcept
    {
        return PartyQuestRuntimeSessionBootstrap::BindProcessOwnerInternal(
            acCoopReplicaRoot,
            acCampaignId,
            acPlayerProfile,
            false);
    }
};

class PartyQuestRuntimeLifecycleIntegrationTestAccess final
{
public:
    static void Mark(PartyQuestRuntimeLifecycleEvent aEvent) noexcept
    {
        PartyQuestRuntimeLifecycleIntegrationPolicy::
            MarkVerifiedPreTransitionHook(aEvent);
    }

    static void Reset() noexcept
    {
        PartyQuestRuntimeLifecycleIntegrationPolicy::ResetForTests();
    }
};

namespace
{
const PartyQuestCampaignId kBootstrapCampaign{
    0xA101A102A103A104ull,
    0xA105A106A107A108ull};
const PartyQuestPlayerProfileId kBootstrapProfile{
    0xB101B102B103B104ull,
    0xB105B106B107B108ull};

struct BootstrapSandbox
{
    std::filesystem::path Root;
    std::filesystem::path CoopRoot;

    BootstrapSandbox()
    {
        const auto nonce =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_bootstrap_" + std::to_string(nonce));
        CoopRoot = Root / "CoopCampaigns";
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
        REQUIRE(CoopRoot.is_absolute());
    }

    ~BootstrapSandbox()
    {
        PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestPlayerProfileLineageAuthorization IssueForCurrentGeneration(
    bool aExactCharacterLineage = true,
    bool aPersistedWithCharacterLineage = true,
    bool aFilenameIndependent = true)
{
    const uint64_t generation =
        PartyQuestRuntimeGenerationFence::GetProcessFence().GetGeneration();
    return PartyQuestPlayerProfileLineageTestAccess::Issue(
        kBootstrapProfile,
        generation,
        aExactCharacterLineage,
        aPersistedWithCharacterLineage,
        aFilenameIndependent);
}
} // namespace

TEST_CASE(
    "Runtime bootstrap requires unforgeable character-lineage profile authority",
    "[quest.party-state.runtime-bootstrap][identity]")
{
    static_assert(std::is_default_constructible_v<
        PartyQuestPlayerProfileLineageAuthorization>);

    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    REQUIRE_FALSE(owner.IsBound());

    const PartyQuestPlayerProfileLineageAuthorization unverified;
    REQUIRE_FALSE(unverified.IsVerified());
    const auto rejected = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
        sandbox.CoopRoot,
        kBootstrapCampaign,
        unverified);
    REQUIRE(rejected.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE_FALSE(owner.IsBound());

    const auto filenameDerivedClaim = IssueForCurrentGeneration(
        true,
        true,
        false);
    REQUIRE_FALSE(filenameDerivedClaim.IsVerified());
    const auto filenameRejected =
        PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
            sandbox.CoopRoot,
            kBootstrapCampaign,
            filenameDerivedClaim);
    REQUIRE(filenameRejected.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::UnverifiedPlayerProfile);
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE(
    "Skyrim lineage bridge requires a stable persisted ABI snapshot",
    "[quest.party-state.runtime-bootstrap][identity][lineage-bridge]")
{
    const PartyQuestLineageRuntimeVersion runtime =
        PartyQuestLineageTargetRuntimeRegistry::Skyrim161170;
    PartyQuestLineageProviderDescriptor provider{
        kPartyQuestLineageProviderAbiVersion,
        static_cast<uint32_t>(sizeof(PartyQuestLineageProviderDescriptor)),
        static_cast<uint32_t>(PartyQuestLineageProviderKind::SkseCosave),
        0u,
        kPartyQuestRequiredLineageProviderCapabilities,
        runtime.Major,
        runtime.Minor,
        runtime.Patch,
        runtime.Build,
        kPartyQuestSkseCosaveProviderFingerprintV2,
        0u,
        0u};
    PartyQuestLineageBridgeSnapshot persisted{
        1u,
        static_cast<uint32_t>(sizeof(PartyQuestLineageBridgeSnapshot)),
        42u,
        static_cast<uint32_t>(PartyQuestLineageBridgeEvidenceState::Persisted),
        0u,
        kBootstrapProfile.High,
        kBootstrapProfile.Low};

    const auto verified =
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            provider,
            runtime,
            persisted,
            persisted,
            7u);
    REQUIRE(verified.IsVerified());
    REQUIRE(verified.GetProfileId() == kBootstrapProfile);
    REQUIRE(verified.GetRuntimeGeneration() == 7u);

    auto changed = persisted;
    ++changed.Sequence;
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            provider,
            runtime,
            persisted,
            changed,
            7u).IsVerified());

    changed = persisted;
    changed.State = static_cast<uint32_t>(
        PartyQuestLineageBridgeEvidenceState::CandidateUnpersisted);
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            provider,
            runtime,
            changed,
            changed,
            7u).IsVerified());

    changed = persisted;
    changed.Reserved = 1u;
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            provider,
            runtime,
            changed,
            changed,
            7u).IsVerified());
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            provider,
            runtime,
            persisted,
            persisted,
            0u).IsVerified());

    auto wrongProvider = provider;
    wrongProvider.ProviderFingerprint ^= 1u;
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            wrongProvider,
            runtime,
            persisted,
            persisted,
            7u).IsVerified());

    wrongProvider = provider;
    wrongProvider.RuntimePatch = 99u;
    REQUIRE_FALSE(
        PartyQuestPlayerProfileLineageTestAccess::ResolveBridgeSnapshots(
            wrongProvider,
            runtime,
            persisted,
            persisted,
            7u).IsVerified());
}

TEST_CASE(
    "lineage target registry names exact runtimes without approving unknown providers",
    "[quest.party-state.runtime-bootstrap][identity][lineage-provider][versions]")
{
    REQUIRE(PartyQuestLineageTargetRuntimeRegistry::IsTarget(
        PartyQuestLineageTargetRuntimeRegistry::Skyrim1597));
    REQUIRE(PartyQuestLineageTargetRuntimeRegistry::IsTarget(
        PartyQuestLineageTargetRuntimeRegistry::Skyrim161170));
    REQUIRE(PartyQuestLineageTargetRuntimeRegistry::IsTarget(
        PartyQuestLineageTargetRuntimeRegistry::Skyrim1799));
    REQUIRE(PartyQuestLineageTargetRuntimeRegistry::IsTarget(
        PartyQuestLineageTargetRuntimeRegistry::Skyrim17104));
    REQUIRE_FALSE(PartyQuestLineageTargetRuntimeRegistry::IsTarget(
        {1u, 6u, 640u, 0u}));

    for (const auto runtime : {
             PartyQuestLineageTargetRuntimeRegistry::Skyrim1597,
             PartyQuestLineageTargetRuntimeRegistry::Skyrim161170,
             PartyQuestLineageTargetRuntimeRegistry::Skyrim1799,
             PartyQuestLineageTargetRuntimeRegistry::Skyrim17104})
    {
        REQUIRE(PartyQuestLineageTargetRuntimeRegistry::AllowsProvider(
            runtime,
            PartyQuestLineageProviderKind::SkseCosave));
        REQUIRE_FALSE(PartyQuestLineageTargetRuntimeRegistry::AllowsProvider(
            runtime,
            PartyQuestLineageProviderKind::EmbeddedClient));
        REQUIRE_FALSE(PartyQuestLineageTargetRuntimeRegistry::AllowsProvider(
            runtime,
            PartyQuestLineageProviderKind::Unknown));
    }
}

TEST_CASE(
    "Production runtime bootstrap requires all installed character identity hooks",
    "[quest.party-state.runtime-bootstrap][identity][lifecycle]")
{
    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();

    PartyQuestRuntimeLifecycleIntegrationTestAccess::Reset();
    REQUIRE_FALSE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::LoadGame));
    REQUIRE_FALSE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::NewGame));
    REQUIRE_FALSE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::MainMenu));
    REQUIRE_FALSE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasCompleteCharacterIdentityCoverage());

    const auto authorization = IssueForCurrentGeneration();
    REQUIRE(authorization.IsVerified());
    const auto blocked = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
        sandbox.CoopRoot,
        kBootstrapCampaign,
        authorization);
    REQUIRE(blocked.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::LifecycleCoverageIncomplete);
    REQUIRE_FALSE(blocked.IsBound());
    REQUIRE_FALSE(owner.IsBound());

    PartyQuestRuntimeLifecycleIntegrationTestAccess::Mark(
        PartyQuestRuntimeLifecycleEvent::LoadGame);
    PartyQuestRuntimeLifecycleIntegrationTestAccess::Mark(
        PartyQuestRuntimeLifecycleEvent::NewGame);
    REQUIRE_FALSE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasCompleteCharacterIdentityCoverage());
    PartyQuestRuntimeLifecycleIntegrationTestAccess::Mark(
        PartyQuestRuntimeLifecycleEvent::MainMenu);
    REQUIRE(PartyQuestRuntimeLifecycleIntegrationPolicy::
        HasCompleteCharacterIdentityCoverage());
    PartyQuestRuntimeLifecycleIntegrationTestAccess::Reset();
}

TEST_CASE(
    "Runtime bootstrap test seam binds exact profile layout under the current generation lease",
    "[quest.party-state.runtime-bootstrap][identity][generation]")
{
    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    REQUIRE_FALSE(owner.IsBound());

    const auto authorization = IssueForCurrentGeneration();
    REQUIRE(authorization.IsVerified());

    const auto bound =
        PartyQuestRuntimeSessionBootstrapTestAccess::BindIgnoringLifecycleCoverage(
            sandbox.CoopRoot,
            kBootstrapCampaign,
            authorization);
    REQUIRE(bound.Status == PartyQuestRuntimeSessionBootstrapStatus::Bound);
    REQUIRE(bound.IsBound());
    REQUIRE(bound.Owner.Status == PartyQuestRuntimeSessionOwnerBindStatus::Bound);
    REQUIRE(owner.IsBound());
    REQUIRE(owner.GetRuntimeSession() != nullptr);
    REQUIRE(owner.GetRuntimeSession()->GetCampaignId() == kBootstrapCampaign);
    REQUIRE(owner.GetRuntimeSession()->GetPlayerProfileId() == kBootstrapProfile);

    const auto expectedPaths = PartyQuestCoopSaveLayout::Build(
        sandbox.CoopRoot,
        kBootstrapCampaign,
        kBootstrapProfile);
    REQUIRE(expectedPaths.has_value());
    REQUIRE(owner.GetPaths() != nullptr);
    REQUIRE(*owner.GetPaths() == *expectedPaths);

    const auto duplicate =
        PartyQuestRuntimeSessionBootstrapTestAccess::BindIgnoringLifecycleCoverage(
            sandbox.CoopRoot,
            kBootstrapCampaign,
            authorization);
    REQUIRE(duplicate.Status == PartyQuestRuntimeSessionBootstrapStatus::Bound);
    REQUIRE(duplicate.Owner.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::AlreadyBound);
    REQUIRE(duplicate.IsBound());

    REQUIRE(owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown).CanProceed());
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE(
    "Runtime bootstrap rejects stale profile authority after generation invalidation",
    "[quest.party-state.runtime-bootstrap][generation][lifecycle]")
{
    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();

    const auto staleAuthorization = IssueForCurrentGeneration();
    const uint64_t authorizedGeneration =
        staleAuthorization.GetRuntimeGeneration();
    const uint64_t currentGeneration = fence.Invalidate();
    REQUIRE(currentGeneration != authorizedGeneration);

    const auto rejected = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
        sandbox.CoopRoot,
        kBootstrapCampaign,
        staleAuthorization);
    REQUIRE(rejected.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::RuntimeGenerationUnavailable);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE_FALSE(owner.IsBound());
}

TEST_CASE(
    "Runtime bootstrap cannot cross a pending lifecycle transition",
    "[quest.party-state.runtime-bootstrap][generation][lifecycle]")
{
    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());

    const auto ticket = fence.BeginLifecycleTransition();
    REQUIRE(ticket.IsValid());
    REQUIRE(fence.IsLifecycleTransitionPending());

    const auto authorization = PartyQuestPlayerProfileLineageTestAccess::Issue(
        kBootstrapProfile,
        ticket.Generation);
    REQUIRE(authorization.IsVerified());

    const auto rejected = PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
        sandbox.CoopRoot,
        kBootstrapCampaign,
        authorization);
    REQUIRE(rejected.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::RuntimeGenerationUnavailable);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE_FALSE(owner.IsBound());

    REQUIRE(fence.CompleteLifecycleTransition(ticket));
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());
}

TEST_CASE(
    "Runtime bootstrap rejects invalid campaign and non-absolute replica root before binding",
    "[quest.party-state.runtime-bootstrap][identity][confinement]")
{
    BootstrapSandbox sandbox;
    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto authorization = IssueForCurrentGeneration();

    const auto invalidCampaign =
        PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
            sandbox.CoopRoot,
            {},
            authorization);
    REQUIRE(invalidCampaign.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::InvalidCampaign);
    REQUIRE_FALSE(owner.IsBound());

    const auto relativeRoot =
        PartyQuestRuntimeSessionBootstrap::BindProcessOwner(
            std::filesystem::path("CoopCampaigns"),
            kBootstrapCampaign,
            authorization);
    REQUIRE(relativeRoot.Status ==
        PartyQuestRuntimeSessionBootstrapStatus::InvalidReplicaRoot);
    REQUIRE_FALSE(owner.IsBound());
}
