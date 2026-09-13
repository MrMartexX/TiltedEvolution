#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <catch2/catch.hpp>

#include <type_traits>

namespace
{
PartyQuestAsyncSaveRequestIdentity Identity(uint64_t aGeneration = 10)
{
    PartyQuestAsyncSaveRequestIdentity value;
    value.CampaignId = {1, 2};
    value.PlayerProfileId = {3, 4};
    value.RuntimeGeneration = aGeneration;
    value.TransactionId = 20;
    value.TargetWorldRevision = 30;
    value.CaptureEpochId = 40;
    value.AttemptNonce = 50;
    value.SaveName = "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000032";
    return value;
}
} // namespace

static_assert(!std::is_copy_constructible_v<PartyQuestAsyncSaveCompletion>);
static_assert(!std::is_copy_assignable_v<PartyQuestAsyncSaveCompletion>);

TEST_CASE("Async save completion requires both exact closed artifacts", "[quest.party-state][async-save-contract]")
{
    PartyQuestAsyncSaveContract contract;
    const auto identity = Identity();
    REQUIRE(contract.Begin(identity, 100, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);

    auto first = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkseCosave, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 110);
    REQUIRE(first.Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE_FALSE(first.SkyrimEssClosed);
    REQUIRE(first.SkseCosaveClosed);
    REQUIRE_FALSE(first.Completion.has_value());

    auto complete = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 120);
    REQUIRE(complete.Status == PartyQuestAsyncSaveContractStatus::Complete);
    REQUIRE(complete.SkyrimEssClosed);
    REQUIRE(complete.SkseCosaveClosed);
    REQUIRE_FALSE(complete.CleanupRequired);
    REQUIRE(complete.Completion.has_value());
    REQUIRE(complete.Completion->Matches(identity));

    auto moved = std::move(*complete.Completion);
    REQUIRE(moved.IsValid());
    REQUIRE_FALSE(complete.Completion->IsValid());
}

TEST_CASE("Async save contract serializes overlap and rejects stale completion", "[quest.party-state][async-save-contract][identity]")
{
    PartyQuestAsyncSaveContract contract;
    const auto current = Identity();
    auto stale = current;
    ++stale.RuntimeGeneration;
    REQUIRE(contract.Begin(current, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(contract.Begin(stale, 2, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
    REQUIRE(
        contract.Observe(stale, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 3).Status == PartyQuestAsyncSaveContractStatus::Stale);
    REQUIRE(contract.Poll(4).Status == PartyQuestAsyncSaveContractStatus::Pending);
}

TEST_CASE("Either artifact failure blocks publication and requires cleanup", "[quest.party-state][async-save-contract][failure]")
{
    const auto identity = Identity();
    SECTION("ess succeeds then cosave fails")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 10, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(
            contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 11).Status ==
            PartyQuestAsyncSaveContractStatus::Pending);
        const auto failed = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkseCosave, PartyQuestAsyncSaveArtifactOutcome::Failed, 12);
        REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
        REQUIRE(failed.CleanupRequired);
        REQUIRE_FALSE(failed.Completion.has_value());
    }
    SECTION("cosave succeeds then ess fails")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 10, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(
            contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkseCosave, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 11).Status ==
            PartyQuestAsyncSaveContractStatus::Pending);
        const auto failed = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::Failed, 12);
        REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
        REQUIRE(failed.CleanupRequired);
        REQUIRE_FALSE(failed.Completion.has_value());
    }
}

TEST_CASE("Duplicate and out-of-order notifications are deterministic", "[quest.party-state][async-save-contract][duplicate]")
{
    PartyQuestAsyncSaveContract contract;
    const auto identity = Identity();
    REQUIRE(contract.Begin(identity, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(
        contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkseCosave, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 2).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(
        contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkseCosave, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 3).Status ==
        PartyQuestAsyncSaveContractStatus::Duplicate);
    REQUIRE(
        contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 4).Status ==
        PartyQuestAsyncSaveContractStatus::Complete);
    REQUIRE(
        contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 5).Status ==
        PartyQuestAsyncSaveContractStatus::Duplicate);
}

TEST_CASE("Timeout cancellation and clock failure never publish completion", "[quest.party-state][async-save-contract][lifecycle]")
{
    const auto identity = Identity();
    SECTION("timeout")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 100, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(
            contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 101).Status ==
            PartyQuestAsyncSaveContractStatus::Pending);
        const auto result = contract.Poll(100 + PartyQuestAsyncSaveContract::kTimeoutMs + 1);
        REQUIRE(result.Status == PartyQuestAsyncSaveContractStatus::TimedOut);
        REQUIRE(result.CleanupRequired);
        REQUIRE_FALSE(result.Completion.has_value());
    }
    SECTION("generation invalidation")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 100, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(contract.Cancel(identity).Status == PartyQuestAsyncSaveContractStatus::Cancelled);
        REQUIRE_FALSE(contract.Cancel(identity).Completion.has_value());
    }
    SECTION("clock regression")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 100, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        REQUIRE(contract.Poll(99).Status == PartyQuestAsyncSaveContractStatus::InvalidClock);
    }
}

TEST_CASE("Pre-existing targets and invalid names fail before request ownership", "[quest.party-state][async-save-contract][confinement]")
{
    SECTION("main exists")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(Identity(), 1, true, false).Status == PartyQuestAsyncSaveContractStatus::ExistingFileConflict);
        REQUIRE(contract.Poll(2).Status == PartyQuestAsyncSaveContractStatus::Inactive);
    }
    SECTION("cosave exists")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(Identity(), 1, false, true).Status == PartyQuestAsyncSaveContractStatus::ExistingFileConflict);
    }
    SECTION("path-like name")
    {
        PartyQuestAsyncSaveContract contract;
        auto invalid = Identity();
        invalid.SaveName = "..\\ordinary-save";
        REQUIRE(contract.Begin(invalid, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::InvalidIdentity);
    }
    SECTION("well-formed name bound to another transaction")
    {
        PartyQuestAsyncSaveContract contract;
        auto invalid = Identity();
        invalid.SaveName = "STR_PreRepair_T0000000000000015_R000000000000001E_A0000000000000032";
        REQUIRE(contract.Begin(invalid, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::InvalidIdentity);
    }
}

TEST_CASE("Unknown completion ABI values fail closed without artifact publication", "[quest.party-state][async-save-contract][abi]")
{
    const auto identity = Identity();

    SECTION("unknown artifact cannot alias the SKSE co-save")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        const auto failed = contract.Observe(identity, static_cast<PartyQuestAsyncSaveArtifact>(0xFF), PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 2);
        REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
        REQUIRE_FALSE(failed.SkyrimEssClosed);
        REQUIRE_FALSE(failed.SkseCosaveClosed);
        REQUIRE(failed.CleanupRequired);
        REQUIRE_FALSE(failed.Completion.has_value());
    }

    SECTION("unknown outcome cannot alias success")
    {
        PartyQuestAsyncSaveContract contract;
        REQUIRE(contract.Begin(identity, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
        const auto failed = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, static_cast<PartyQuestAsyncSaveArtifactOutcome>(0xFF), 2);
        REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
        REQUIRE_FALSE(failed.SkyrimEssClosed);
        REQUIRE_FALSE(failed.SkseCosaveClosed);
        REQUIRE(failed.CleanupRequired);
        REQUIRE_FALSE(failed.Completion.has_value());
    }
}

TEST_CASE("Failure cannot be hidden by an earlier success observation", "[quest.party-state][async-save-contract][failure]")
{
    PartyQuestAsyncSaveContract contract;
    const auto identity = Identity();
    REQUIRE(contract.Begin(identity, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(
        contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess, 2).Status ==
        PartyQuestAsyncSaveContractStatus::Pending);

    const auto failed = contract.Observe(identity, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::Failed, 3);
    REQUIRE(failed.Status == PartyQuestAsyncSaveContractStatus::Failed);
    REQUIRE(failed.SkyrimEssClosed);
    REQUIRE_FALSE(failed.SkseCosaveClosed);
    REQUIRE(failed.CleanupRequired);
    REQUIRE_FALSE(failed.Completion.has_value());
}

TEST_CASE("Partial terminal attempt must retire before a new request", "[quest.party-state][async-save-contract][cleanup]")
{
    PartyQuestAsyncSaveContract contract;
    const auto first = Identity();
    auto second = first;
    ++second.AttemptNonce;
    second.SaveName = "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000033";
    REQUIRE(contract.Begin(first, 1, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
    REQUIRE(contract.Observe(first, PartyQuestAsyncSaveArtifact::SkyrimEss, PartyQuestAsyncSaveArtifactOutcome::Failed, 2).Status == PartyQuestAsyncSaveContractStatus::Failed);
    REQUIRE(contract.Begin(second, 3, false, false).Status == PartyQuestAsyncSaveContractStatus::Busy);
    contract.Retire();
    REQUIRE(contract.Begin(second, 4, false, false).Status == PartyQuestAsyncSaveContractStatus::Pending);
}
