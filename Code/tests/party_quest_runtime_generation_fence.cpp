#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <thread>

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

TEST_CASE("Async lifecycle transition advances generation and blocks dispatch until exact completion", "[quest.party-state.lifecycle][generation-fence]")
{
    PartyQuestRuntimeGenerationFence fence;
    const uint64_t before = fence.GetGeneration();

    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());
    REQUIRE(transition.Generation != before);
    REQUIRE(fence.GetGeneration() == transition.Generation);
    REQUIRE(fence.IsLifecycleTransitionPending());

    CHECK_FALSE(fence.TryAcquire(before).has_value());
    CHECK_FALSE(fence.TryAcquire(transition.Generation).has_value());
    CHECK_FALSE(fence.BeginLifecycleTransition().IsValid());

    auto wrong = transition;
    ++wrong.Ticket;
    if (wrong.Ticket == 0)
        ++wrong.Ticket;
    CHECK_FALSE(fence.CompleteLifecycleTransition(wrong));
    REQUIRE(fence.IsLifecycleTransitionPending());
    CHECK_FALSE(fence.TryAcquire(transition.Generation).has_value());

    REQUIRE(fence.CompleteLifecycleTransition(transition));
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());
    CHECK_FALSE(fence.CompleteLifecycleTransition(transition));

    auto execution = fence.TryAcquire(transition.Generation);
    REQUIRE(execution.has_value());
    CHECK(execution->IsValid());
    CHECK(execution->GetGeneration() == transition.Generation);
}

TEST_CASE("Async lifecycle transition may complete on another thread", "[quest.party-state.lifecycle][generation-fence]")
{
    PartyQuestRuntimeGenerationFence fence;
    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());

    std::atomic<bool> completed{false};
    std::thread completionThread([&]()
    {
        completed.store(
            fence.CompleteLifecycleTransition(transition),
            std::memory_order_release);
    });
    completionThread.join();

    REQUIRE(completed.load(std::memory_order_acquire));
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());
    auto execution = fence.TryAcquire(transition.Generation);
    REQUIRE(execution.has_value());
    CHECK(execution->IsValid());
}

TEST_CASE("Synchronous invalidation cannot erase a pending async lifecycle transition", "[quest.party-state.lifecycle][generation-fence]")
{
    PartyQuestRuntimeGenerationFence fence;
    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());

    const uint64_t afterAdditionalInvalidation = fence.Invalidate();
    REQUIRE(afterAdditionalInvalidation != transition.Generation);
    REQUIRE(fence.IsLifecycleTransitionPending());
    CHECK_FALSE(fence.TryAcquire(afterAdditionalInvalidation).has_value());

    REQUIRE(fence.CompleteLifecycleTransition(transition));
    REQUIRE_FALSE(fence.IsLifecycleTransitionPending());

    auto execution = fence.TryAcquire(afterAdditionalInvalidation);
    REQUIRE(execution.has_value());
    CHECK(execution->IsValid());
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