#include <Structs/Skyrim/PartyQuestRuntimeMutationDispatch.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <catch2/catch.hpp>

TEST_CASE("Production mutation dispatch rejects a private session before observation or arming",
    "[quest.party-state.runtime-dispatch][runtime-owner]")
{
    auto& processOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto released = processOwner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    REQUIRE(released.CanProceed());
    REQUIRE_FALSE(processOwner.IsBound());

    const PartyQuestCampaignId campaign{
        0xC101C102C103C104ull,
        0xC105C106C107C108ull};
    const PartyQuestPlayerProfileId player{
        0xC201C202C203C204ull,
        0xC205C206C207C208ull};

    PartyQuestRuntimeApplySession privateSession(campaign, player);
    PartyQuestSaveGuard privateGuard;
    PartyQuestRuntimeGuardedSession privateGuarded(privateSession, privateGuard);

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = 27001;

    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = GameId(96, 0x9100);

    size_t observations{};
    size_t executions{};
    const auto result = PartyQuestRuntimeMutationDispatchGate::Dispatch(
        privateGuarded,
        request,
        requirement,
        [&](const GameId&) -> std::optional<PartyQuestRuntimeCompatibilityFacts>
        {
            ++observations;
            return PartyQuestRuntimeCompatibilityFacts{};
        },
        [&](const PartyQuestRuntimeApplyRequest&)
        {
            ++executions;
            return true;
        });

    REQUIRE(result.Status ==
        PartyQuestRuntimeMutationDispatchStatus::ProcessOwnerMismatch);
    REQUIRE_FALSE(result.MutationBarrierArmed);
    REQUIRE_FALSE(result.MutationInvoked);
    REQUIRE_FALSE(result.WasDispatched());
    REQUIRE(observations == 0);
    REQUIRE(executions == 0);
    REQUIRE_FALSE(privateGuard.IsActive());
    REQUIRE(privateSession.GetCoordinator().GetActive() == nullptr);
}
