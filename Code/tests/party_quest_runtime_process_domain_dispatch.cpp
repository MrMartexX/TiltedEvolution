#include <Structs/Skyrim/PartyQuestRuntimeMutationDispatch.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <catch2/catch.hpp>

namespace
{
const PartyQuestCampaignId kProcessDomainCampaign{
    0xD101D102D103D104ull,
    0xD105D106D107D108ull};
const PartyQuestPlayerProfileId kProcessDomainPlayer{
    0xD201D202D203D204ull,
    0xD205D206D207D208ull};

PartyQuestRuntimeMutationDispatchResult DispatchInvalidRequest(
    PartyQuestRuntimeGuardedSession& aGuarded,
    PartyQuestRuntimeGenerationFence& aFence,
    size_t& aObservations,
    size_t& aExecutions)
{
    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = 37001;

    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = GameId(97, 0xA100);

    return PartyQuestRuntimeMutationDispatchGate::Dispatch(
        aGuarded,
        request,
        requirement,
        aFence,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            ++aObservations;
            return PartyQuestRuntimeCompatibilityFacts{};
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++aExecutions;
            return true;
        });
}
} // namespace

TEST_CASE(
    "Explicit mutation dispatch cannot split the process runtime lifecycle domain",
    "[quest.party-state.runtime-dispatch][runtime-owner][process-domain]")
{
    auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto released = processOwner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(processOwner.IsBound());
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());

    SECTION("process generation fence plus private local guard is rejected")
    {
        PartyQuestRuntimeApplySession session(
            kProcessDomainCampaign,
            kProcessDomainPlayer);
        PartyQuestSaveGuard localGuard;
        PartyQuestRuntimeGuardedSession guarded(session, localGuard);

        size_t observations{};
        size_t executions{};
        const auto result = DispatchInvalidRequest(
            guarded,
            PartyQuestRuntimeGenerationFence::GetProcessFence(),
            observations,
            executions);

        REQUIRE(result.Status ==
            PartyQuestRuntimeMutationDispatchStatus::ProcessOwnerMismatch);
        REQUIRE_FALSE(result.MutationBarrierArmed);
        REQUIRE_FALSE(result.MutationInvoked);
        REQUIRE(observations == 0);
        REQUIRE(executions == 0);
    }

    SECTION("process SaveGuard plus unrelated generation fence is rejected")
    {
        PartyQuestRuntimeApplySession session(
            kProcessDomainCampaign,
            kProcessDomainPlayer);
        PartyQuestRuntimeGuardedSession guarded(session);
        PartyQuestRuntimeGenerationFence localFence;

        size_t observations{};
        size_t executions{};
        const auto result = DispatchInvalidRequest(
            guarded,
            localFence,
            observations,
            executions);

        REQUIRE(result.Status ==
            PartyQuestRuntimeMutationDispatchStatus::ProcessOwnerMismatch);
        REQUIRE_FALSE(result.MutationBarrierArmed);
        REQUIRE_FALSE(result.MutationInvoked);
        REQUIRE(observations == 0);
        REQUIRE(executions == 0);
        REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    }
}
