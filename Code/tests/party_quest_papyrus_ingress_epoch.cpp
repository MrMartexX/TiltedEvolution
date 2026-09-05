#include <Structs/Skyrim/PartyQuestPapyrusIngressEpoch.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <thread>
#include <utility>

TEST_CASE("Papyrus ingress epoch invalidates a sample across one complete arrival", "[quest.party-state.quiescence][papyrus-ingress]")
{
    PartyQuestPapyrusIngressEpoch epoch;
    const auto before = epoch.Capture();
    REQUIRE(before.Healthy);
    REQUIRE(before.Generation == 1);
    REQUIRE(before.ActiveIngress == 0);

    {
        auto ingress = epoch.BeginIngress();
        REQUIRE(ingress.IsActive());
        const auto active = epoch.Capture();
        REQUIRE(active.Healthy);
        REQUIRE(active.Generation == 2);
        REQUIRE(active.ActiveIngress == 1);
        REQUIRE_FALSE(PartyQuestPapyrusIngressEpoch::IsStable(before, active));
    }

    const auto after = epoch.Capture();
    REQUIRE(after.Healthy);
    REQUIRE(after.Generation == 3);
    REQUIRE(after.ActiveIngress == 0);
    REQUIRE(epoch.GetIngressCount() == 1);
    REQUIRE_FALSE(PartyQuestPapyrusIngressEpoch::IsStable(before, after));
    REQUIRE(PartyQuestPapyrusIngressEpoch::IsStable(after, epoch.Capture()));
}

TEST_CASE("Papyrus ingress scope has one owner across moves", "[quest.party-state.quiescence][papyrus-ingress]")
{
    PartyQuestPapyrusIngressEpoch epoch;
    {
        auto first = epoch.BeginIngress();
        auto second = std::move(first);
        REQUIRE_FALSE(first.IsActive());
        REQUIRE(second.IsActive());

        PartyQuestPapyrusIngressEpoch::Scope third;
        third = std::move(second);
        REQUIRE_FALSE(second.IsActive());
        REQUIRE(third.IsActive());
        REQUIRE(epoch.Capture().ActiveIngress == 1);
    }

    const auto final = epoch.Capture();
    REQUIRE(final.Healthy);
    REQUIRE(final.ActiveIngress == 0);
    REQUIRE(final.Generation == 3);
    REQUIRE(epoch.GetIngressCount() == 1);
}

TEST_CASE("Concurrent Papyrus arrivals cannot look like a stable idle sample", "[quest.party-state.quiescence][papyrus-ingress][concurrency]")
{
    PartyQuestPapyrusIngressEpoch epoch;
    std::atomic_bool entered{false};
    std::atomic_bool release{false};

    const auto before = epoch.Capture();
    std::thread producer(
        [&]()
        {
            auto ingress = epoch.BeginIngress();
            entered.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        });

    while (!entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    const auto during = epoch.Capture();
    REQUIRE(during.ActiveIngress == 1);
    REQUIRE_FALSE(PartyQuestPapyrusIngressEpoch::IsStable(before, during));

    release.store(true, std::memory_order_release);
    producer.join();

    const auto after = epoch.Capture();
    REQUIRE(after.Healthy);
    REQUIRE(after.ActiveIngress == 0);
    REQUIRE(epoch.GetIngressCount() == 1);
    REQUIRE_FALSE(PartyQuestPapyrusIngressEpoch::IsStable(before, after));
}
