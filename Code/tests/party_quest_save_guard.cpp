#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <thread>

TEST_CASE("Critical repair save guard blocks user saves but permits controlled checkpoints", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));
    REQUIRE(guard.CanEnterEngineSave());

    REQUIRE(guard.Acquire(1001) == PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE(guard.IsActive());
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));
    REQUIRE_FALSE(guard.CanEnterEngineSave());

    REQUIRE(guard.Release(1001));
    REQUIRE_FALSE(guard.IsActive());
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(guard.CanEnterEngineSave());
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

TEST_CASE("Controlled save scope requires the exact active transaction and supports safe nesting", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(3001) == PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE_FALSE(guard.CanEnterEngineSave());

    PartyQuestControlledSaveScope wrongTransaction(guard, 3002);
    REQUIRE_FALSE(wrongTransaction.IsArmed());
    REQUIRE_FALSE(guard.CanEnterEngineSave());

    {
        PartyQuestControlledSaveScope outer(guard, 3001);
        REQUIRE(outer.IsArmed());
        REQUIRE(guard.CanEnterEngineSave());

        {
            PartyQuestControlledSaveScope nested(guard, 3001);
            REQUIRE(nested.IsArmed());
            REQUIRE(guard.CanEnterEngineSave());
        }

        REQUIRE(guard.CanEnterEngineSave());
    }

    REQUIRE_FALSE(guard.CanEnterEngineSave());
    REQUIRE(guard.Release(3001));
    REQUIRE(guard.CanEnterEngineSave());
}

TEST_CASE("Controlled save authorization is thread-local", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(4001) == PartyQuestSaveGuardAcquireStatus::Acquired);

    std::atomic<bool> otherThreadAllowed{true};
    {
        PartyQuestControlledSaveScope scope(guard, 4001);
        REQUIRE(scope.IsArmed());
        REQUIRE(guard.CanEnterEngineSave());

        std::thread worker([&]()
        {
            otherThreadAllowed.store(
                guard.CanEnterEngineSave(),
                std::memory_order_release);
        });
        worker.join();

        REQUIRE_FALSE(otherThreadAllowed.load(std::memory_order_acquire));
        REQUIRE(guard.CanEnterEngineSave());
    }

    REQUIRE_FALSE(guard.CanEnterEngineSave());
    REQUIRE(guard.Release(4001));
}

TEST_CASE("A stale controlled scope cannot authorize a later transaction", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(5001) == PartyQuestSaveGuardAcquireStatus::Acquired);

    {
        PartyQuestControlledSaveScope staleScope(guard, 5001);
        REQUIRE(staleScope.IsArmed());
        REQUIRE(guard.CanEnterEngineSave());

        REQUIRE(guard.Release(5001));
        REQUIRE(guard.Acquire(5002) == PartyQuestSaveGuardAcquireStatus::Acquired);
        REQUIRE_FALSE(guard.CanEnterEngineSave());
    }

    REQUIRE_FALSE(guard.CanEnterEngineSave());
    REQUIRE(guard.Release(5002));
}
