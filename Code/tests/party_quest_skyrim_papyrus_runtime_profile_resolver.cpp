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
} // namespace

TEST_CASE("Skyrim runtime identity capability is constructor confined", "[quest.party-state.quiescence][runtime-profile][runtime-identity]")
{
    PartyQuestSkyrimRuntimeIdentityAuthorization missing;
    REQUIRE_FALSE(missing.IsVerified());
    REQUIRE_FALSE(
        PartyQuestSkyrimPapyrusRuntimeProfileResolver::Resolve(missing).IsVerified());

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
}

TEST_CASE("Production Skyrim runtime profile registry fails closed before VM sampling", "[quest.party-state.quiescence][runtime-profile][fail-closed]")
{
    const auto runtimeIdentity =
        PartyQuestPapyrusRuntimeObserverTestAccess::AuthorizeRuntimeIdentity(
            9, 9, 9001, 42);
    REQUIRE(runtimeIdentity.IsVerified());

    // No production ABI/layout profile is approved at this milestone, even for
    // a VersionDb-authorized identity capability.
    const auto productionProfile =
        PartyQuestSkyrimPapyrusRuntimeProfileResolver::Resolve(runtimeIdentity);
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
