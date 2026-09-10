#include <Structs/Skyrim/PartyQuestPapyrusRuntimeMonitor.h>

#include <catch2/catch.hpp>

#include <utility>
#include <vector>

namespace
{
class ScriptedPapyrusObserver final : public PartyQuestPapyrusRuntimeObserver
{
public:
    explicit ScriptedPapyrusObserver(
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

    [[nodiscard]] size_t Calls() const noexcept { return m_next; }

private:
    std::vector<PartyQuestPapyrusRuntimeObservation> m_samples;
    size_t m_next{};
};

PartyQuestPapyrusRuntimeObservation Idle(uint64_t aGeneration)
{
    return {
        PartyQuestPapyrusRuntimeObservationStatus::Idle,
        0,
        aGeneration};
}

PartyQuestPapyrusRuntimeObservation Busy(
    uint32_t aPendingWork,
    uint64_t aGeneration)
{
    return {
        PartyQuestPapyrusRuntimeObservationStatus::Busy,
        aPendingWork,
        aGeneration};
}
} // namespace

TEST_CASE("Papyrus runtime work-domain masks reject unknown bits", "[quest.party-state.quiescence][runtime-monitor][work-envelope]")
{
    constexpr uint32_t unknownBit = 1u << 31;
    REQUIRE_FALSE(IsPartyQuestPapyrusRuntimeWorkDomainMaskValid(unknownBit));
    REQUIRE_FALSE(HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(0));
    REQUIRE(HasCompletePartyQuestPapyrusRuntimeWorkEnvelope(
        kPartyQuestPapyrusRuntimeRequiredWorkDomains));

    const PartyQuestPapyrusRuntimeObservation malformed{
        PartyQuestPapyrusRuntimeObservationStatus::Idle,
        0,
        1,
        unknownBit};
    REQUIRE_FALSE(malformed.IsSelfConsistent());
}

TEST_CASE("Runtime Papyrus monitor requires stable trusted idle observations", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({Idle(10), Idle(10)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6001, 100, 1000));
    REQUIRE(monitor.Poll(6001, 110) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE_FALSE(monitor.Authorize().has_value());

    REQUIRE(monitor.Poll(6001, 120) ==
        PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto authorization = monitor.Authorize();
    REQUIRE(authorization.has_value());
    REQUIRE(authorization->GetTransactionId() == 6001);
    REQUIRE(monitor.Consume(std::move(*authorization)));
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
    REQUIRE(monitor.GetTransactionId() == 0);
}

TEST_CASE("Busy runtime work and event generation changes reset monitor stability", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({
        Idle(20),
        Busy(3, 20),
        Idle(20),
        Idle(21),
        Idle(21)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6002, 0, 1000));
    REQUIRE(monitor.Poll(6002, 10) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6002, 20) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6002, 30) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6002, 40) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6002, 50) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    REQUIRE(monitor.Authorize().has_value());
}

TEST_CASE("Unknown runtime observation invalidates prior proof and waits until deadline", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({
        Idle(30),
        Idle(30),
        {PartyQuestPapyrusRuntimeObservationStatus::Unknown, 0, 0},
        Idle(30),
        Idle(30)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6003, 100, 1000));
    REQUIRE(monitor.Poll(6003, 110) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6003, 120) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    auto stale = monitor.Authorize();
    REQUIRE(stale.has_value());

    REQUIRE(monitor.Poll(6003, 130) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6003, 140) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6003, 150) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
    REQUIRE_FALSE(monitor.Consume(std::move(*stale)));

    auto current = monitor.Authorize();
    REQUIRE(current.has_value());
    REQUIRE(monitor.Consume(std::move(*current)));
}

TEST_CASE("Runtime Papyrus monitor times out fail closed", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({Idle(40), Idle(40)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6004, 1000, 50));
    REQUIRE(monitor.Poll(6004, 1020) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6004, 1050) == PartyQuestPapyrusRuntimeMonitorStatus::TimedOut);
    REQUIRE_FALSE(monitor.Authorize().has_value());
    REQUIRE(observer.Calls() == 1);

    REQUIRE(monitor.Poll(6004, 1060) == PartyQuestPapyrusRuntimeMonitorStatus::TimedOut);
    REQUIRE(monitor.Reset(6004));
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Inactive);
}

TEST_CASE("Unsupported runtime observation is terminal and cannot authorize", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({
        {PartyQuestPapyrusRuntimeObservationStatus::Unsupported, 0, 0},
        Idle(50)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6005, 0, 1000));
    REQUIRE(monitor.Poll(6005, 1) == PartyQuestPapyrusRuntimeMonitorStatus::Unsupported);
    REQUIRE_FALSE(monitor.Authorize().has_value());
    REQUIRE(monitor.Poll(6005, 2) == PartyQuestPapyrusRuntimeMonitorStatus::Unsupported);
    REQUIRE(observer.Calls() == 1);
}

TEST_CASE("Malformed runtime samples and clock regression fail closed", "[quest.party-state.quiescence][runtime-monitor]")
{
    SECTION("busy sample cannot claim zero pending work")
    {
        ScriptedPapyrusObserver observer({
            {PartyQuestPapyrusRuntimeObservationStatus::Busy, 0, 60}});
        PartyQuestPapyrusRuntimeMonitor monitor(observer);
        REQUIRE(monitor.Begin(6006, 10, 100));
        REQUIRE(monitor.Poll(6006, 11) ==
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
        REQUIRE_FALSE(monitor.Authorize().has_value());
    }

    SECTION("idle sample cannot claim pending work")
    {
        ScriptedPapyrusObserver observer({
            {PartyQuestPapyrusRuntimeObservationStatus::Idle, 1, 60}});
        PartyQuestPapyrusRuntimeMonitor monitor(observer);
        REQUIRE(monitor.Begin(6007, 10, 100));
        REQUIRE(monitor.Poll(6007, 11) ==
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidObservation);
        REQUIRE_FALSE(monitor.Authorize().has_value());
    }

    SECTION("monotonic clock regression is terminal")
    {
        ScriptedPapyrusObserver observer({Idle(70), Idle(70)});
        PartyQuestPapyrusRuntimeMonitor monitor(observer);
        REQUIRE(monitor.Begin(6008, 100, 1000));
        REQUIRE(monitor.Poll(6008, 110) ==
            PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
        REQUIRE(monitor.Poll(6008, 109) ==
            PartyQuestPapyrusRuntimeMonitorStatus::InvalidClock);
        REQUIRE_FALSE(monitor.Authorize().has_value());
        REQUIRE(observer.Calls() == 1);
    }
}

TEST_CASE("Wrong transaction cannot disturb an active runtime monitor", "[quest.party-state.quiescence][runtime-monitor]")
{
    ScriptedPapyrusObserver observer({Idle(80), Idle(80)});
    PartyQuestPapyrusRuntimeMonitor monitor(observer);

    REQUIRE(monitor.Begin(6009, 0, 1000));
    REQUIRE(monitor.Poll(6010, 10) ==
        PartyQuestPapyrusRuntimeMonitorStatus::InvalidTransaction);
    REQUIRE(monitor.GetTransactionId() == 6009);
    REQUIRE(monitor.GetStatus() == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(observer.Calls() == 0);

    REQUIRE(monitor.Poll(6009, 10) == PartyQuestPapyrusRuntimeMonitorStatus::Waiting);
    REQUIRE(monitor.Poll(6009, 20) == PartyQuestPapyrusRuntimeMonitorStatus::Quiescent);
}
