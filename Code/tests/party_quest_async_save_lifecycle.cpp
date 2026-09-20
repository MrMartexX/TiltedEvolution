#include <Structs/Skyrim/PartyQuestAsyncSaveLifecycle.h>

#include <catch2/catch.hpp>

namespace
{
PartyQuestAsyncSaveRequestIdentity Identity(uint64_t aNonce = 50)
{
    PartyQuestAsyncSaveRequestIdentity identity;
    identity.CampaignId = {1, 2};
    identity.PlayerProfileId = {3, 4};
    identity.RuntimeGeneration = 10;
    identity.TransactionId = 20;
    identity.TargetWorldRevision = 30;
    identity.CaptureEpochId = 40;
    identity.AttemptNonce = aNonce;
    identity.SaveName = aNonce == 50 ?
        "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000032" :
        "STR_PreRepair_T0000000000000014_R000000000000001E_A0000000000000033";
    return identity;
}

void Admit(PartyQuestAsyncSaveLifecycle& aLifecycle,
    const PartyQuestAsyncSaveRequestIdentity& acIdentity)
{
    const auto result = aLifecycle.ObserveEngineAdmission(acIdentity,
        PartyQuestAsyncSaveEngineAdmissionOutcome::Admitted);
    REQUIRE(result.Status == PartyQuestAsyncSaveLifecycleStatus::Active);
    REQUIRE(result.DrainRequired);
    REQUIRE_FALSE(result.SafeToInvalidate);
}
}

TEST_CASE("Async save authorizes consumption only after current successful retirement",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    Admit(lifecycle, identity);

    const auto completed = lifecycle.ObserveCompletion(
        identity, PartyQuestAsyncSavePhysicalOutcome::Succeeded);
    REQUIRE(completed.Status ==
            PartyQuestAsyncSaveLifecycleStatus::CompletionAccepted);
    REQUIRE_FALSE(completed.ConsumptionAuthorized);
    REQUIRE(completed.DrainRequired);

    const auto retired = lifecycle.ObserveRetirement(identity);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveLifecycleStatus::RetiredAuthorized);
    REQUIRE(retired.ConsumptionAuthorized);
    REQUIRE(retired.SafeToInvalidate);
    REQUIRE_FALSE(retired.DrainRequired);

    const auto duplicate = lifecycle.ObserveRetirement(identity);
    REQUIRE(duplicate.Status == PartyQuestAsyncSaveLifecycleStatus::Duplicate);
    REQUIRE_FALSE(duplicate.ConsumptionAuthorized);
}

TEST_CASE("Closing async save admission suppresses late success until exact retirement",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    Admit(lifecycle, identity);

    const auto closed = lifecycle.CloseAdmission();
    REQUIRE(closed.Status == PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    REQUIRE(closed.CancelRequired);
    REQUIRE(closed.DrainRequired);
    REQUIRE_FALSE(closed.SafeToInvalidate);

    const auto cancelled = lifecycle.ObserveCancelRequested(identity);
    REQUIRE_FALSE(cancelled.CancelRequired);
    REQUIRE(cancelled.DrainRequired);

    const auto lateSuccess = lifecycle.ObserveCompletion(
        identity, PartyQuestAsyncSavePhysicalOutcome::Succeeded);
    REQUIRE(lateSuccess.Status ==
            PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    REQUIRE_FALSE(lateSuccess.ConsumptionAuthorized);
    REQUIRE(lateSuccess.DrainRequired);

    const auto retired = lifecycle.ObserveRetirement(identity);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveLifecycleStatus::RetiredWithoutAuthority);
    REQUIRE_FALSE(retired.ConsumptionAuthorized);
    REQUIRE(retired.SafeToInvalidate);
}

TEST_CASE("Stale async save events fail closed without releasing active ownership",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    const auto stale = Identity(51);
    Admit(lifecycle, identity);
    REQUIRE(lifecycle.CloseAdmission().CancelRequired);

    const auto staleCompletion = lifecycle.ObserveCompletion(
        stale, PartyQuestAsyncSavePhysicalOutcome::Succeeded);
    REQUIRE(staleCompletion.Status == PartyQuestAsyncSaveLifecycleStatus::Stale);
    REQUIRE_FALSE(staleCompletion.ConsumptionAuthorized);
    REQUIRE(staleCompletion.DrainRequired);

    const auto staleRetirement = lifecycle.ObserveRetirement(stale);
    REQUIRE(staleRetirement.Status == PartyQuestAsyncSaveLifecycleStatus::Stale);
    REQUIRE(staleRetirement.DrainRequired);
    REQUIRE_FALSE(staleRetirement.SafeToInvalidate);
}

TEST_CASE("Async save lifecycle close cancel and retire are deterministic",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    Admit(lifecycle, identity);

    REQUIRE(lifecycle.CloseAdmission().Status ==
            PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    REQUIRE(lifecycle.CloseAdmission().Status ==
            PartyQuestAsyncSaveLifecycleStatus::Duplicate);
    REQUIRE(lifecycle.ObserveCancelRequested(identity).Status ==
            PartyQuestAsyncSaveLifecycleStatus::AdmissionClosed);
    REQUIRE(lifecycle.ObserveCancelRequested(identity).Status ==
            PartyQuestAsyncSaveLifecycleStatus::Duplicate);
    REQUIRE(lifecycle.ObserveRetirement(identity).Status ==
            PartyQuestAsyncSaveLifecycleStatus::RetiredWithoutAuthority);
    REQUIRE(lifecycle.ObserveRetirement(identity).Status ==
            PartyQuestAsyncSaveLifecycleStatus::Duplicate);
}

TEST_CASE("Rejected engine save admission has no async ownership to drain",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto rejected = lifecycle.ObserveEngineAdmission(Identity(),
        PartyQuestAsyncSaveEngineAdmissionOutcome::Rejected);
    REQUIRE(rejected.Status == PartyQuestAsyncSaveLifecycleStatus::Inactive);
    REQUIRE(rejected.SafeToInvalidate);
    REQUIRE_FALSE(rejected.CancelRequired);
    REQUIRE_FALSE(rejected.DrainRequired);

    const auto shutdown = lifecycle.CloseAdmission();
    REQUIRE(shutdown.SafeToInvalidate);
    REQUIRE_FALSE(shutdown.DrainRequired);
}

TEST_CASE("Shutdown with an admitted async save requires cancellation and drain",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    Admit(lifecycle, identity);

    const auto shutdown = lifecycle.CloseAdmission();
    REQUIRE(shutdown.CancelRequired);
    REQUIRE(shutdown.DrainRequired);
    REQUIRE_FALSE(shutdown.SafeToInvalidate);

    REQUIRE(lifecycle.ObserveCancelRequested(identity).DrainRequired);
    const auto retired = lifecycle.ObserveRetirement(identity);
    REQUIRE(retired.SafeToInvalidate);
    REQUIRE_FALSE(retired.ConsumptionAuthorized);
}

TEST_CASE("Retired async save lifecycle admits the next distinct request",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto first = Identity();
    const auto second = Identity(51);
    Admit(lifecycle, first);
    REQUIRE(lifecycle.ObserveCompletion(
        first, PartyQuestAsyncSavePhysicalOutcome::Succeeded).Status ==
            PartyQuestAsyncSaveLifecycleStatus::CompletionAccepted);
    REQUIRE(lifecycle.ObserveRetirement(first).ConsumptionAuthorized);

    const auto rejectedNext = lifecycle.ObserveEngineAdmission(second,
        PartyQuestAsyncSaveEngineAdmissionOutcome::Rejected);
    REQUIRE(rejectedNext.Status ==
            PartyQuestAsyncSaveLifecycleStatus::Inactive);
    REQUIRE(rejectedNext.SafeToInvalidate);

    REQUIRE(lifecycle.ObserveEngineAdmission(first,
        PartyQuestAsyncSaveEngineAdmissionOutcome::Admitted).Status ==
            PartyQuestAsyncSaveLifecycleStatus::Duplicate);
    Admit(lifecycle, second);

    const auto oldReplay = lifecycle.ObserveRetirement(first);
    REQUIRE(oldReplay.Status == PartyQuestAsyncSaveLifecycleStatus::Stale);
    REQUIRE(oldReplay.DrainRequired);
    REQUIRE_FALSE(oldReplay.SafeToInvalidate);
    REQUIRE_FALSE(oldReplay.ConsumptionAuthorized);
}

TEST_CASE("Failed async save completion retires without publication authority",
    "[quest.party-state][async-save-lifecycle]")
{
    PartyQuestAsyncSaveLifecycle lifecycle;
    const auto identity = Identity();
    Admit(lifecycle, identity);

    const auto failed = lifecycle.ObserveCompletion(
        identity, PartyQuestAsyncSavePhysicalOutcome::Failed);
    REQUIRE(failed.Status ==
            PartyQuestAsyncSaveLifecycleStatus::CompletionAccepted);
    REQUIRE_FALSE(failed.ConsumptionAuthorized);
    REQUIRE(failed.DrainRequired);

    const auto retired = lifecycle.ObserveRetirement(identity);
    REQUIRE(retired.Status ==
            PartyQuestAsyncSaveLifecycleStatus::RetiredWithoutAuthority);
    REQUIRE_FALSE(retired.ConsumptionAuthorized);
    REQUIRE(retired.SafeToInvalidate);
}
