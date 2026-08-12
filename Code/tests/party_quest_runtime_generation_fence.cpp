#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <future>
#include <optional>

using namespace std::chrono_literals;

TEST_CASE("Party quest process generation fence is shared and monotonic")
{
    auto& first = PartyQuestRuntimeGenerationFence::GetProcessFence();
    auto& second = PartyQuestRuntimeGenerationFence::GetProcessFence();
    REQUIRE(&first == &second);

    const uint64_t before = first.GetGeneration();
    {
        auto invalidation = first.BeginInvalidation();
        REQUIRE(invalidation.IsValid());
        CHECK(invalidation.GetGeneration() != 0);
        CHECK(invalidation.GetGeneration() != before);
    }

    CHECK(first.GetGeneration() != before);
}

TEST_CASE("Party quest invalidation lease prevents dispatch into a half-published runtime")
{
    PartyQuestRuntimeGenerationFence fence;
    std::optional<PartyQuestRuntimeGenerationFence::InvalidationLease> invalidation{
        fence.BeginInvalidation()};
    REQUIRE(invalidation->IsValid());
    const uint64_t rebuiltGeneration = invalidation->GetGeneration();

    std::promise<void> attempted;
    auto attemptedFuture = attempted.get_future();
    auto acquireFuture = std::async(
        std::launch::async,
        [&]()
        {
            attempted.set_value();
            auto execution = fence.TryAcquire(rebuiltGeneration);
            return execution.has_value() && execution->IsValid();
        });

    attemptedFuture.wait();
    CHECK(acquireFuture.wait_for(20ms) == std::future_status::timeout);

    invalidation.reset();
    REQUIRE(acquireFuture.wait_for(2s) == std::future_status::ready);
    CHECK(acquireFuture.get());
}

TEST_CASE("Party quest runtime owner lifecycle invalidates process dispatch generation")
{
    auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t before = fence.GetGeneration();

    PartyQuestRuntimeSessionOwner owner;
    const auto result = owner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Disconnect);

    CHECK(result.Status == PartyQuestRuntimeLifecycleFenceStatus::Allowed);
    CHECK(result.CanProceed());
    CHECK(fence.GetGeneration() != before);
}
