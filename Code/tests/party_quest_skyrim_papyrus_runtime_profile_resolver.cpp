#include <Structs/Skyrim/PartyQuestSkyrimPapyrusRuntimeProfileResolver.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>

#include <catch2/catch.hpp>

#include <type_traits>

namespace
{
class RuntimeProfileCountingObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        ++m_observeCount;
        return {};
    }

    [[nodiscard]] uint32_t GetObserveCount() const noexcept
    {
        return m_observeCount;
    }

private:
    uint32_t m_observeCount{};
};

static_assert(!std::is_constructible_v<
    PartyQuestSkyrimRuntimeIdentityAuthorization,
    PartyQuestSkyrimRuntimeVersion,
    bool,
    bool>);
static_assert(!std::is_constructible_v<
    PartyQuestPapyrusRuntimeGenerationAuthorization,
    uint64_t,
    uint32_t,
    bool,
    bool>);
static_assert(!std::is_constructible_v<
    PartyQuestPapyrusRuntimeSnapshotAuthorization,
    uint64_t,
    uint32_t,
    bool,
    bool,
    bool>);
} // namespace

TEST_CASE("Skyrim runtime identity capability is constructor confined", "[quest.party-state.quiescence][runtime-profile][runtime-identity]")
{
    const auto generation =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource();
    const auto snapshot =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot();
    REQUIRE(generation.IsVerified());
    REQUIRE(snapshot.IsVerified());

    PartyQuestSkyrimRuntimeIdentityAuthorization missing;
    REQUIRE_FALSE(missing.IsVerified());
    REQUIRE_FALSE(
        PartyQuestSkyrimPapyrusRuntimeProfileResolver::Resolve(
            missing, generation, snapshot).IsVerified());

    const auto wrongExecutable =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42, false, true);
    REQUIRE_FALSE(wrongExecutable.IsVerified());

    const auto unsupportedByVersionDb =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42, true, false);
    REQUIRE_FALSE(unsupportedByVersionDb.IsVerified());

    const auto verifiedIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    REQUIRE(verifiedIdentity.IsVerified());
    REQUIRE(verifiedIdentity.GetRuntimeVersion().Major == 9);
    REQUIRE(verifiedIdentity.GetRuntimeVersion().Minor == 9);
    REQUIRE(verifiedIdentity.GetRuntimeVersion().Patch == 9001);
    REQUIRE(verifiedIdentity.GetRuntimeVersion().Build == 42);
}

TEST_CASE("Papyrus generation source authority requires complete monotonic work-arrival coverage", "[quest.party-state.quiescence][runtime-profile][generation-source]")
{
    PartyQuestPapyrusRuntimeGenerationAuthorization missing;
    REQUIRE_FALSE(missing.IsVerified());

    const auto missingFingerprint =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    REQUIRE_FALSE(missingFingerprint.IsVerified());

    const auto partialCoverage =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0x1122334455667788ull,
            static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks),
            true,
            true);
    REQUIRE_FALSE(partialCoverage.IsVerified());

    const auto nonMonotonic =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            false,
            true);
    REQUIRE_FALSE(nonMonotonic.IsVerified());

    const auto missesWorkArrival =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            false);
    REQUIRE_FALSE(missesWorkArrival.IsVerified());

    const auto verified =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    REQUIRE(verified.IsVerified());
    REQUIRE(verified.GetSourceFingerprint() == 0x1122334455667788ull);
    REQUIRE(HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(
        verified.GetCoveredWorkDomains()));
}

TEST_CASE("Papyrus snapshot authority requires complete coherent read-only fail-closed coverage", "[quest.party-state.quiescence][runtime-profile][snapshot]")
{
    PartyQuestPapyrusRuntimeSnapshotAuthorization missing;
    REQUIRE_FALSE(missing.IsVerified());

    const auto missingFingerprint =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true,
            true);
    REQUIRE_FALSE(missingFingerprint.IsVerified());

    const auto partialCoverage =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks),
            true,
            true,
            true);
    REQUIRE_FALSE(partialCoverage.IsVerified());

    const auto writableSampling =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            false,
            true,
            true);
    REQUIRE_FALSE(writableSampling.IsVerified());

    const auto incoherentAcrossDomains =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            false,
            true);
    REQUIRE_FALSE(incoherentAcrossDomains.IsVerified());

    const auto bestEffortOnFailure =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true,
            false);
    REQUIRE_FALSE(bestEffortOnFailure.IsVerified());

    const auto verified =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true,
            true);
    REQUIRE(verified.IsVerified());
    REQUIRE(verified.GetSnapshotFingerprint() == 0x8877665544332211ull);
    REQUIRE(HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(
        verified.GetCoveredWorkDomains()));
}

TEST_CASE("Skyrim Papyrus runtime profile resolver requires an exact executable profile", "[quest.party-state.quiescence][runtime-profile][exact-match]")
{
    const auto runtimeIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    REQUIRE(runtimeIdentity.IsVerified());

    const auto mismatchedVersion =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9002, 42,
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    REQUIRE_FALSE(mismatchedVersion.IsVerified());

    const auto missingFingerprint =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9001, 42,
            0,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    REQUIRE_FALSE(missingFingerprint.IsVerified());

    const auto partialEnvelope =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9001, 42,
            0x1122334455667788ull,
            static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks),
            true,
            true);
    REQUIRE_FALSE(partialEnvelope.IsVerified());

    const auto incoherentSnapshot =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9001, 42,
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            false,
            true);
    REQUIRE_FALSE(incoherentSnapshot.IsVerified());

    const auto untrustedGeneration =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9001, 42,
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            false);
    REQUIRE_FALSE(untrustedGeneration.IsVerified());

    const auto exactCompleteProfile =
        PartyQuestPapyrusRuntimeObserverTestAccess::ResolveRuntimeProfileForTesting(
            runtimeIdentity,
            9, 9, 9001, 42,
            0x1122334455667788ull,
            kPartyQuestPapyrusRuntimeRequiredWorkDomains,
            true,
            true);
    REQUIRE(exactCompleteProfile.IsVerified());
    REQUIRE(exactCompleteProfile.GetRuntimeProfileFingerprint() ==
        0x1122334455667788ull);
    REQUIRE(exactCompleteProfile.GetGenerationSourceFingerprint() ==
        PartyQuestPapyrusRuntimeObserverTestAccess::
            kVerifiedTestGenerationSourceFingerprint);
}

TEST_CASE("Exact runtime profile rejects an invalid generation capability", "[quest.party-state.quiescence][runtime-profile][generation-source]")
{
    const auto runtimeIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    REQUIRE(runtimeIdentity.IsVerified());

    const auto partialGeneration =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource(
            0x8877665544332211ull,
            static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks),
            true,
            true);
    REQUIRE_FALSE(partialGeneration.IsVerified());

    const auto rejectedProfile =
        PartyQuestPapyrusRuntimeObserverTestAccess::
            ResolveRuntimeProfileWithGenerationForTesting(
                runtimeIdentity,
                partialGeneration,
                9, 9, 9001, 42,
                0x1122334455667788ull,
                kPartyQuestPapyrusRuntimeRequiredWorkDomains,
                true);
    REQUIRE_FALSE(rejectedProfile.IsVerified());
}

TEST_CASE("Exact runtime profile rejects an invalid snapshot capability", "[quest.party-state.quiescence][runtime-profile][snapshot]")
{
    const auto runtimeIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    const auto generation =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource();
    const auto partialSnapshot =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot(
            0x8877665544332211ull,
            static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::RunningStacks),
            true,
            true,
            true);
    REQUIRE(runtimeIdentity.IsVerified());
    REQUIRE(generation.IsVerified());
    REQUIRE_FALSE(partialSnapshot.IsVerified());

    const auto rejectedProfile =
        PartyQuestPapyrusRuntimeObserverTestAccess::
            ResolveRuntimeProfileWithEvidenceForTesting(
                runtimeIdentity,
                generation,
                partialSnapshot,
                9, 9, 9001, 42,
                0x1122334455667788ull,
                kPartyQuestPapyrusRuntimeRequiredWorkDomains);
    REQUIRE_FALSE(rejectedProfile.IsVerified());
}

TEST_CASE("Production Skyrim runtime profile registry fails closed before VM sampling", "[quest.party-state.quiescence][runtime-profile][fail-closed]")
{
    const auto runtimeIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    const auto generation =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeGenerationSource();
    const auto snapshot =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeSnapshot();
    REQUIRE(runtimeIdentity.IsVerified());
    REQUIRE(generation.IsVerified());
    REQUIRE(snapshot.IsVerified());

    // No production ABI/layout profile, generation source or coherent snapshot
    // contract is approved at this milestone. Even test-authorized prerequisites
    // cannot create a production profile while the registry remains empty.
    const auto productionProfile =
        PartyQuestSkyrimPapyrusRuntimeProfileResolver::Resolve(
            runtimeIdentity, generation, snapshot);
    REQUIRE_FALSE(productionProfile.IsVerified());

    RuntimeProfileCountingObserver observer;
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::
            AuthorizeWithRuntimeProfileAuthorization(observer, productionProfile);
    REQUIRE_FALSE(observerAuthorization.IsVerified());

    PartyQuestPapyrusRuntimeMonitor monitor(observer);
    REQUIRE_FALSE(monitor.Begin(9100, 0, 1000, observerAuthorization));
    REQUIRE(observer.GetObserveCount() == 0);
}
