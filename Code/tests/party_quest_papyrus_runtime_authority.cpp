#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <party_quest_papyrus_runtime_observer_test_access.h>

#include <catch2/catch.hpp>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
class AuthorityTestObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    explicit AuthorityTestObserver(
        std::vector<PartyQuestPapyrusRuntimeObservation> aSamples)
        : m_samples(std::move(aSamples))
    {
    }

    [[nodiscard]] PartyQuestPapyrusRuntimeObservation Observe(
        uint64_t) noexcept override
    {
        if (m_next >= m_samples.size())
            return {};
        return m_samples[m_next++];
    }

private:
    std::vector<PartyQuestPapyrusRuntimeObservation> m_samples;
    size_t m_next{};
};

static_assert(!std::is_copy_constructible_v<AuthorityTestObserver>);
static_assert(!std::is_move_constructible_v<AuthorityTestObserver>);
static_assert(HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(
    kPartyQuestPapyrusRuntimeRequiredWorkDomains));

PartyQuestPapyrusRuntimeObservation Idle(uint64_t aGeneration)
{
    return {
        PartyQuestPapyrusRuntimeObservationStatus::Idle,
        0,
        aGeneration,
        kPartyQuestPapyrusRuntimeRequiredWorkDomains};
}

PartyQuestPapyrusRuntimeObservation PartialIdle(uint64_t aGeneration)
{
    return {
        PartyQuestPapyrusRuntimeObservationStatus::Idle,
        0,
        aGeneration,
        static_cast<uint32_t>(PartyQuestPapyrusRuntimeWorkDomain::VmTaskQueue)};
}
} // namespace

TEST_CASE("Papyrus runtime observer authorization is exact-instance scoped", "[quest.party-state.quiescence][runtime-monitor][observer-authority]")
{
    AuthorityTestObserver first({Idle(10), Idle(10)});
    AuthorityTestObserver second({Idle(10), Idle(10)});

    PartyQuestPapyrusRuntimeObserverAuthorization missing;
    PartyQuestPapyrusRuntimeMonitor firstMonitor(first);
    REQUIRE_FALSE(firstMonitor.Begin(7000, 0, 1000, missing));
    REQUIRE(firstMonitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);

    const auto firstAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(first);
    REQUIRE(firstAuthorization.IsVerified());
    REQUIRE(firstAuthorization.Matches(first));
    REQUIRE_FALSE(firstAuthorization.Matches(second));
    REQUIRE(first.GetInstanceNonce() != second.GetInstanceNonce());

    PartyQuestPapyrusRuntimeMonitor secondMonitor(second);
    REQUIRE_FALSE(secondMonitor.Begin(7001, 0, 1000, firstAuthorization));
    REQUIRE(secondMonitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
    REQUIRE(secondMonitor.GetTransactionId() == 0);
}

TEST_CASE("Diagnostic observer sessions cannot produce authoritative quiescence", "[quest.party-state.quiescence][runtime-monitor][observer-authority]")
{
    AuthorityTestObserver observer({
        {PartyQuestPapyrusRuntimeObservationStatus::Idle, 0, 20},
        {PartyQuestPapyrusRuntimeObservationStatus::Idle, 0, 20}});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(7002, 0, 1000));
    REQUIRE_FALSE(monitor.IsAuthoritativeSession());
    REQUIRE(monitor.Poll(7002, 10) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(7002, 20) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);

    auto authorization = monitor.Authorize();
    REQUIRE(authorization.has_value());
    REQUIRE_FALSE(monitor.ConsumeAuthoritative(std::move(*authorization)));
    REQUIRE(authorization->IsVerified());
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);

    REQUIRE(monitor.Consume(std::move(*authorization)));
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
}

TEST_CASE("Authorized observer session consumes stable quiescence authoritatively", "[quest.party-state.quiescence][runtime-monitor][observer-authority]")
{
    AuthorityTestObserver observer({Idle(30), Idle(30)});
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(7003, 100, 1000, observerAuthorization));
    REQUIRE(monitor.IsAuthoritativeSession());
    REQUIRE(monitor.Poll(7003, 110) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(7003, 120) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);

    auto authorization = monitor.Authorize();
    REQUIRE(authorization.has_value());
    REQUIRE(monitor.ConsumeAuthoritative(std::move(*authorization)));
    REQUIRE_FALSE(authorization->IsVerified());
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
    REQUIRE_FALSE(monitor.IsAuthoritativeSession());
}

TEST_CASE("Authoritative idle requires the complete Papyrus work envelope", "[quest.party-state.quiescence][runtime-monitor][observer-authority][work-envelope]")
{
    AuthorityTestObserver observer({PartialIdle(40), Idle(40)});
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(7006, 0, 1000, observerAuthorization));
    REQUIRE(monitor.IsAuthoritativeSession());
    REQUIRE(monitor.Poll(7006, 10) ==
        PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
    REQUIRE_FALSE(monitor.Authorize().has_value());
    REQUIRE(monitor.Poll(7006, 20) ==
        PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
}

TEST_CASE("Papyrus event generation regression is terminal fail closed", "[quest.party-state.quiescence][runtime-monitor][observer-authority]")
{
    AuthorityTestObserver observer({Idle(50), Idle(49), Idle(49)});
    const auto observerAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(observer);
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(7004, 0, 1000, observerAuthorization));
    REQUIRE(monitor.Poll(7004, 10) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(7004, 20) ==
        PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
    REQUIRE_FALSE(monitor.Authorize().has_value());
    REQUIRE(monitor.Poll(7004, 30) ==
        PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
    REQUIRE(monitor.IsAuthoritativeSession());
}

TEST_CASE("Observer address reuse cannot revive stale runtime authority", "[quest.party-state.quiescence][runtime-monitor][observer-authority][lifetime]")
{
    alignas(AuthorityTestObserver) std::byte storage[sizeof(AuthorityTestObserver)];
    auto* first = new (storage) AuthorityTestObserver({Idle(60), Idle(60)});
    const uint64_t firstNonce = first->GetInstanceNonce();
    REQUIRE(firstNonce != 0);

    const auto staleAuthorization =
        PartyQuestPapyrusRuntimeObserverTestAccess::Authorize(*first);
    REQUIRE(staleAuthorization.Matches(*first));

    {
        PartyQuestPapyrusRuntimeMonitor monitor(*first);
        REQUIRE(monitor.Begin(7005, 0, 1000, staleAuthorization));
        REQUIRE(monitor.IsAuthoritativeSession());

        first->~AuthorityTestObserver();
        auto* second = new (storage) AuthorityTestObserver({Idle(60), Idle(60)});
        REQUIRE(second == first);
        REQUIRE(second->GetInstanceNonce() != 0);
        REQUIRE(second->GetInstanceNonce() != firstNonce);
        REQUIRE_FALSE(staleAuthorization.Matches(*second));
        REQUIRE_FALSE(monitor.IsAuthoritativeSession());

        REQUIRE(monitor.Poll(7005, 10) ==
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
        REQUIRE_FALSE(monitor.Authorize().has_value());

        second->~AuthorityTestObserver();
    }
}
