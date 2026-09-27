#include <Structs/Skyrim/PartyQuestAsyncSaveFinalizationGate.h>

#include <catch2/catch.hpp>

#include <type_traits>
#include <utility>

namespace
{
PartyQuestAsyncSaveRequestIdentity Identity(uint64_t aAttemptNonce = 50)
{
    PartyQuestAsyncSaveRequestIdentity identity;
    identity.CampaignId = {1, 2};
    identity.PlayerProfileId = {3, 4};
    identity.RuntimeGeneration = 10;
    identity.TransactionId = 20;
    identity.TargetWorldRevision = 30;
    identity.CaptureEpochId = 40;
    identity.AttemptNonce = aAttemptNonce;
    identity.SaveName = aAttemptNonce == 50 ?
        "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000032" :
        "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000033";
    return identity;
}

void Begin(PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    const PartyQuestAsyncSaveRequestIdentity& acIdentity)
{
    REQUIRE(aContract.Begin(acIdentity, 1, false, false).Status ==
            PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(aGate.Begin(acIdentity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);
}

PartyQuestAsyncSaveFinalizationResult Complete(
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    const PartyQuestAsyncSaveRequestIdentity& acIdentity)
{
    REQUIRE(aContract.Observe(acIdentity, PartyQuestAsyncSaveArtifact::SkyrimEss,
        PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 2).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(aContract.Observe(acIdentity, PartyQuestAsyncSaveArtifact::SkseCosave,
        PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 3).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(aContract.ObservePublication(acIdentity,
        PartyQuestAsyncSaveArtifact::SkyrimEss,
        PartyQuestAsyncSavePublicationOutcome::PublishedSuccess, 4).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);
    auto complete = aContract.ObservePublication(acIdentity,
        PartyQuestAsyncSaveArtifact::SkseCosave,
        PartyQuestAsyncSavePublicationOutcome::PublishedSuccess, 5);
    REQUIRE(complete.Status == PartyQuestAsyncSaveContractStatus::Complete);
    return aGate.ObserveContractResult(acIdentity, std::move(complete));
}

PartyQuestAsyncSaveFinalizationResult Fail(
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    const PartyQuestAsyncSaveRequestIdentity& acIdentity)
{
    auto failed = aContract.Observe(acIdentity,
        PartyQuestAsyncSaveArtifact::SkyrimEss,
        PartyQuestAsyncSaveArtifactOutcome::Failed, 2);
    REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
    return aGate.ObserveContractResult(acIdentity, std::move(failed));
}
}

static_assert(!std::is_copy_constructible_v<PartyQuestAsyncSaveFinalizationResult>);
static_assert(!std::is_copy_assignable_v<PartyQuestAsyncSaveFinalizationResult>);

TEST_CASE("Logical completion is quarantined without request retirement",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    const auto result = Complete(contract, gate, identity);
    REQUIRE(result.Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);
    REQUIRE_FALSE(result.RetirementApplied);
    REQUIRE_FALSE(result.Completion.has_value());
}

TEST_CASE("Coordinated begin rolls back a half-admitted contract",
    "[quest.party-state][async-save-finalization][admission]")
{
    SECTION("matching admission")
    {
        PartyQuestAsyncSaveContract contract;
        PartyQuestAsyncSaveFinalizationGate gate;
        const auto identity = Identity();
        const auto result = gate.BeginCoordinated(
            contract, identity, 1, false, false);
        REQUIRE(result.Status == PartyQuestAsyncSaveFinalizationStatus::Pending);
        REQUIRE(contract.Begin(Identity(51), 2, false, false).Status ==
                PartyQuestAsyncSaveContractStatus::Busy);
    }

    SECTION("busy gate cannot strand a fresh contract")
    {
        PartyQuestAsyncSaveFinalizationGate gate;
        PartyQuestAsyncSaveContract firstContract;
        PartyQuestAsyncSaveContract secondContract;
        const auto first = Identity();
        const auto second = Identity(51);
        REQUIRE(gate.BeginCoordinated(
            firstContract, first, 1, false, false).Status ==
                PartyQuestAsyncSaveFinalizationStatus::Pending);

        REQUIRE(gate.BeginCoordinated(
            secondContract, second, 2, false, false).Status ==
                PartyQuestAsyncSaveFinalizationStatus::Busy);
        REQUIRE(secondContract.Begin(second, 3, false, false).Status ==
                PartyQuestAsyncSaveContractStatus::Pending);
    }

    SECTION("contract rejection does not touch gate")
    {
        PartyQuestAsyncSaveContract contract;
        PartyQuestAsyncSaveFinalizationGate gate;
        const auto first = Identity();
        REQUIRE(gate.BeginCoordinated(
            contract, first, 1, true, false).Status ==
                PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
        REQUIRE(gate.Begin(Identity(51)).Status ==
                PartyQuestAsyncSaveFinalizationStatus::Pending);
    }
}

TEST_CASE("Successful retirement releases matching completion once",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    REQUIRE(Complete(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    auto finalized = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Succeeded);
    REQUIRE(finalized.Status == PartyQuestAsyncSaveFinalizationStatus::Finalized);
    REQUIRE(finalized.RetirementApplied);
    REQUIRE(finalized.Completion.has_value());
    REQUIRE(finalized.Completion->Matches(identity));

    const auto duplicate = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Succeeded);
    REQUIRE(duplicate.Status == PartyQuestAsyncSaveFinalizationStatus::Duplicate);
    REQUIRE_FALSE(duplicate.Completion.has_value());
}

TEST_CASE("Failed retirement quarantines a logical completion",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    REQUIRE(Complete(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    const auto retired = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Failed);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess);
    REQUIRE(retired.RetirementApplied);
    REQUIRE_FALSE(retired.Completion.has_value());
}

TEST_CASE("Logical failure retires only on matching physical failure",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    REQUIRE(Fail(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    const auto retired = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Failed);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess);
    REQUIRE(retired.RetirementApplied);
    REQUIRE_FALSE(retired.Completion.has_value());
}

TEST_CASE("Early retirement drains pending request without success",
    "[quest.party-state][async-save-finalization]")
{
    for (const auto outcome : {PartyQuestAsyncSaveFinalOutcome::Failed,
             PartyQuestAsyncSaveFinalOutcome::Succeeded})
    {
        PartyQuestAsyncSaveContract contract;
        PartyQuestAsyncSaveFinalizationGate gate;
        const auto identity = Identity();
        Begin(contract, gate, identity);

        const auto result = gate.ObserveRetirement(contract, identity, outcome);
        REQUIRE(result.Status ==
                PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired);
        REQUIRE(result.RetirementApplied);
        REQUIRE_FALSE(result.Completion.has_value());
        REQUIRE(contract.Begin(Identity(51), 2, false, false).Status ==
                PartyQuestAsyncSaveContractStatus::Pending);
    }
}

TEST_CASE("Full identity rejects stale and ABA retirement",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    const auto retry = Identity(51);
    Begin(contract, gate, identity);
    REQUIRE(Complete(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    const auto stale = gate.ObserveRetirement(
        contract, retry, PartyQuestAsyncSaveFinalOutcome::Succeeded);
    REQUIRE(stale.Status == PartyQuestAsyncSaveFinalizationStatus::Stale);
    REQUIRE_FALSE(stale.RetirementApplied);

    REQUIRE(gate.ObserveRetirement(contract, identity,
        PartyQuestAsyncSaveFinalOutcome::Succeeded).Status ==
        PartyQuestAsyncSaveFinalizationStatus::Finalized);
    REQUIRE(contract.Begin(retry, 6, false, false).Status ==
            PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(gate.Begin(retry).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Pending);
    REQUIRE(gate.ObserveRetirement(contract, identity,
        PartyQuestAsyncSaveFinalOutcome::Succeeded).Status ==
        PartyQuestAsyncSaveFinalizationStatus::Stale);
}

TEST_CASE("Contradictory successful retirement after logical failure fails closed",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    REQUIRE(Fail(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    const auto result = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Succeeded);
    REQUIRE(result.Status ==
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolationRetired);
    REQUIRE(result.RetirementApplied);
    REQUIRE_FALSE(result.Completion.has_value());
}

TEST_CASE("Malformed outcome and identity do not mutate active request",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    const auto malformed = static_cast<PartyQuestAsyncSaveFinalOutcome>(255);
    const auto rejected = gate.ObserveRetirement(contract, identity, malformed);
    REQUIRE(rejected.Status == PartyQuestAsyncSaveFinalizationStatus::InvalidInput);
    REQUIRE_FALSE(rejected.RetirementApplied);
    REQUIRE(contract.Begin(Identity(51), 2, false, false).Status ==
            PartyQuestAsyncSaveContractStatus::Busy);
}

TEST_CASE("Cancellation and timeout require failed physical retirement",
    "[quest.party-state][async-save-finalization]")
{
    SECTION("cancelled request")
    {
        PartyQuestAsyncSaveContract contract;
        PartyQuestAsyncSaveFinalizationGate gate;
        const auto identity = Identity();
        Begin(contract, gate, identity);

        auto cancelled = contract.Cancel(identity);
        REQUIRE(cancelled.Status == PartyQuestAsyncSaveContractStatus::Cancelled);
        REQUIRE(gate.ObserveContractResult(identity, std::move(cancelled)).Status ==
                PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);
        const auto retired = gate.ObserveRetirement(
            contract, identity, PartyQuestAsyncSaveFinalOutcome::Failed);
        REQUIRE(retired.Status ==
                PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess);
        REQUIRE(retired.RetirementApplied);
        REQUIRE_FALSE(retired.Completion.has_value());
    }

    SECTION("timed out request")
    {
        PartyQuestAsyncSaveContract contract;
        PartyQuestAsyncSaveFinalizationGate gate;
        const auto identity = Identity();
        Begin(contract, gate, identity);

        auto timedOut = contract.Poll(PartyQuestAsyncSaveContract::kTimeoutMs + 2);
        REQUIRE(timedOut.Status == PartyQuestAsyncSaveContractStatus::TimedOut);
        REQUIRE(gate.ObserveContractResult(identity, std::move(timedOut)).Status ==
                PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);
        REQUIRE(gate.ObserveRetirement(
            contract, identity, PartyQuestAsyncSaveFinalOutcome::Failed).Status ==
                PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess);
    }
}

TEST_CASE("Overlapping gate begin and post-completion failure fail closed",
    "[quest.party-state][async-save-finalization]")
{
    PartyQuestAsyncSaveContract contract;
    PartyQuestAsyncSaveFinalizationGate gate;
    const auto identity = Identity();
    Begin(contract, gate, identity);
    REQUIRE(gate.Begin(Identity(51)).Status ==
            PartyQuestAsyncSaveFinalizationStatus::Busy);
    REQUIRE(Complete(contract, gate, identity).Status ==
            PartyQuestAsyncSaveFinalizationStatus::AwaitingRetirement);

    auto contradictory = contract.Observe(identity,
        PartyQuestAsyncSaveArtifact::SkyrimEss,
        PartyQuestAsyncSaveArtifactOutcome::Failed, 6);
    REQUIRE(contradictory.Status == PartyQuestAsyncSaveContractStatus::Failed);
    REQUIRE(gate.ObserveContractResult(identity, std::move(contradictory)).Status ==
            PartyQuestAsyncSaveFinalizationStatus::ProtocolViolation);
    const auto retired = gate.ObserveRetirement(
        contract, identity, PartyQuestAsyncSaveFinalOutcome::Failed);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveFinalizationStatus::RetiredWithoutSuccess);
    REQUIRE_FALSE(retired.Completion.has_value());
}
