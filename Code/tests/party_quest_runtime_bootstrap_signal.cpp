#include <Structs/Skyrim/PartyQuestRuntimeBootstrapSignal.h>

#include <catch2/catch.hpp>

namespace
{
struct BootstrapEvidenceHarness final
{
    PartyQuestRuntimeBootstrapSignal Signal;
    bool CampaignVerified{};
    bool LineageVerified{};
    uint32_t Attempts{};
    uint32_t Binds{};

    void ProcessGameThread() noexcept
    {
        if (!Signal.Consume())
            return;

        ++Attempts;
        if (CampaignVerified && LineageVerified)
            ++Binds;
    }
};
} // namespace

TEST_CASE(
    "Runtime bootstrap retries only after a new authoritative signal",
    "[quest.party-state.runtime-bootstrap][production][signal]")
{
    BootstrapEvidenceHarness harness;

    harness.ProcessGameThread();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 0);
    REQUIRE(harness.Binds == 0);

    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);

    harness.ProcessGameThread();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);

    harness.Signal.Publish();
    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 2);
    REQUIRE_FALSE(harness.Signal.Consume());
}

TEST_CASE(
    "Runtime bootstrap supports campaign before lineage without polling",
    "[quest.party-state.runtime-bootstrap][production][signal][ordering]")
{
    BootstrapEvidenceHarness harness;

    harness.CampaignVerified = true;
    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);
    REQUIRE(harness.Binds == 0);

    for (int i = 0; i < 8; ++i)
        harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);

    harness.LineageVerified = true;
    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 2);
    REQUIRE(harness.Binds == 1);
}

TEST_CASE(
    "Runtime bootstrap supports lineage before campaign without polling",
    "[quest.party-state.runtime-bootstrap][production][signal][ordering]")
{
    BootstrapEvidenceHarness harness;

    harness.LineageVerified = true;
    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);
    REQUIRE(harness.Binds == 0);

    for (int i = 0; i < 8; ++i)
        harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 1);

    harness.CampaignVerified = true;
    harness.Signal.Publish();
    harness.ProcessGameThread();
    REQUIRE(harness.Attempts == 2);
    REQUIRE(harness.Binds == 1);
}

TEST_CASE(
    "Runtime bootstrap signal reset discards stale lifecycle work",
    "[quest.party-state.runtime-bootstrap][production][signal][lifecycle]")
{
    PartyQuestRuntimeBootstrapSignal signal;
    signal.Publish();
    signal.Reset();
    REQUIRE_FALSE(signal.Consume());

    signal.Publish();
    REQUIRE(signal.Consume());
    REQUIRE_FALSE(signal.Consume());
}
