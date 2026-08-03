#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <catch2/catch.hpp>

TEST_CASE("Critical repair save guard blocks user saves but permits controlled checkpoints", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));

    REQUIRE(guard.Acquire(1001) == PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE(guard.IsActive());
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));

    REQUIRE(guard.Release(1001));
    REQUIRE_FALSE(guard.IsActive());
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
}

TEST_CASE("Critical repair save guard is transaction-scoped and idempotent", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(0) == PartyQuestSaveGuardAcquireStatus::InvalidTransaction);
    REQUIRE(guard.Acquire(2001) == PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE(guard.Acquire(2001) == PartyQuestSaveGuardAcquireStatus::Duplicate);
    REQUIRE(guard.Acquire(2002) == PartyQuestSaveGuardAcquireStatus::Busy);
    REQUIRE(guard.GetTransactionId() == 2001);

    REQUIRE_FALSE(guard.Release(0));
    REQUIRE_FALSE(guard.Release(2002));
    REQUIRE(guard.IsActive());
    REQUIRE(guard.Release(2001));
    REQUIRE_FALSE(guard.IsActive());
}
