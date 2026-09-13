#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <catch2/catch.hpp>

TEST_CASE(
    "Reference readiness is generation-bound and unload-aware",
    "[quest.party-state.runtime-reference-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    const uint64_t generation = fence.GetGeneration();

    REQUIRE_FALSE(readiness.IsLoaded(0x1234, generation));
    REQUIRE(readiness.Observe(0x1234, true));
    REQUIRE(readiness.IsLoaded(0x1234, generation));
    REQUIRE(readiness.GetObservationGeneration() == generation);

    REQUIRE(readiness.Observe(0x1234, false));
    REQUIRE_FALSE(readiness.IsLoaded(0x1234, generation));

    REQUIRE_FALSE(readiness.Observe(0, true));
    REQUIRE_FALSE(readiness.IsLoaded(0, generation));
}

TEST_CASE(
    "Reference readiness validates a complete target set under one generation lease",
    "[quest.party-state.runtime-reference-readiness][lifecycle]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    const uint64_t generation = fence.GetGeneration();

    REQUIRE(readiness.Observe(0x1100, true));
    REQUIRE(readiness.Observe(0x1200, true));
    REQUIRE(readiness.Observe(0x1300, true));

    const std::vector<uint32_t> complete{0x1100, 0x1200, 0x1300};
    REQUIRE(readiness.AreLoaded(complete, generation));
    REQUIRE_FALSE(readiness.AreLoaded({0x1100, 0x1400}, generation));
    REQUIRE_FALSE(readiness.AreLoaded({0x1100, 0}, generation));

    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());
    REQUIRE_FALSE(readiness.AreLoaded(complete, generation));
    REQUIRE_FALSE(readiness.AreLoaded(complete, transition.Generation));

    REQUIRE(fence.CompleteLifecycleTransition(transition));
    REQUIRE_FALSE(readiness.AreLoaded(complete, transition.Generation));

    REQUIRE(readiness.Observe(0x1100, true));
    REQUIRE(readiness.Observe(0x1200, true));
    REQUIRE(readiness.Observe(0x1300, true));
    REQUIRE(readiness.AreLoaded(complete, transition.Generation));
}

TEST_CASE(
    "Reference readiness cannot survive runtime generation invalidation",
    "[quest.party-state.runtime-reference-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    const uint64_t firstGeneration = fence.GetGeneration();

    REQUIRE(readiness.Observe(0x2000, true));
    REQUIRE(readiness.IsLoaded(0x2000, firstGeneration));

    const uint64_t secondGeneration = fence.Invalidate();
    REQUIRE(secondGeneration != firstGeneration);
    REQUIRE_FALSE(readiness.IsLoaded(0x2000, firstGeneration));
    REQUIRE_FALSE(readiness.IsLoaded(0x2000, secondGeneration));

    REQUIRE(readiness.Observe(0x2000, true));
    REQUIRE(readiness.GetObservationGeneration() == secondGeneration);
    REQUIRE(readiness.IsLoaded(0x2000, secondGeneration));
}

TEST_CASE(
    "Reference readiness fails closed while engine lifecycle transition is pending",
    "[quest.party-state.runtime-reference-readiness]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    const uint64_t before = fence.GetGeneration();

    REQUIRE(readiness.Observe(0x3000, true));
    REQUIRE(readiness.IsLoaded(0x3000, before));

    const auto transition = fence.BeginLifecycleTransition();
    REQUIRE(transition.IsValid());
    REQUIRE(transition.Generation != before);
    REQUIRE(fence.IsLifecycleTransitionPending());

    REQUIRE_FALSE(readiness.Observe(0x3001, true));
    REQUIRE_FALSE(readiness.IsLoaded(0x3000, before));
    REQUIRE_FALSE(readiness.IsLoaded(0x3000, transition.Generation));

    REQUIRE(fence.CompleteLifecycleTransition(transition));
    REQUIRE_FALSE(readiness.IsLoaded(0x3000, transition.Generation));
    REQUIRE(readiness.Observe(0x3001, true));
    REQUIRE(readiness.IsLoaded(0x3001, transition.Generation));
}

TEST_CASE(
    "Reference readiness capacity overflow poisons positive evidence until next generation",
    "[quest.party-state.runtime-reference-readiness][resource-bounds]")
{
    PartyQuestRuntimeGenerationFence fence;
    PartyQuestRuntimeReferenceReadiness readiness(fence);
    const uint64_t generation = fence.GetGeneration();

    bool admittedAll = true;
    for (size_t index = 0;
         index < PartyQuestRuntimeReferenceReadiness::MaxTrackedReferences;
         ++index)
    {
        admittedAll = admittedAll && readiness.Observe(
            static_cast<uint32_t>(0x10000 + index),
            true);
    }
    REQUIRE(admittedAll);
    REQUIRE(readiness.IsLoaded(0x10000, generation));

    REQUIRE_FALSE(readiness.Observe(0xF0000000u, true));
    REQUIRE(readiness.IsOverflowed());
    REQUIRE_FALSE(readiness.IsLoaded(0x10000, generation));
    REQUIRE_FALSE(readiness.IsLoaded(0xF0000000u, generation));
    REQUIRE_FALSE(readiness.AreLoaded({0x10000}, generation));
    REQUIRE_FALSE(readiness.Observe(0x10000, false));

    const uint64_t nextGeneration = fence.Invalidate();
    REQUIRE_FALSE(readiness.IsLoaded(0x10000, nextGeneration));
    REQUIRE(readiness.Observe(0x4000, true));
    REQUIRE_FALSE(readiness.IsOverflowed());
    REQUIRE(readiness.IsLoaded(0x4000, nextGeneration));
    REQUIRE(readiness.AreLoaded({0x4000}, nextGeneration));
}
