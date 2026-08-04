#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <thread>

namespace
{
bool EngineSaveAllowed(const PartyQuestSaveGuard& acGuard)
{
    auto permit = acGuard.TryEnterEngineSave();
    return permit.IsAllowed();
}
} // namespace

TEST_CASE("Critical repair save guard blocks user saves but permits controlled checkpoints", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));
    REQUIRE(EngineSaveAllowed(guard));

    REQUIRE(guard.Acquire(1001) == PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE(guard.IsActive());
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Auto));
    REQUIRE_FALSE(guard.CanSave(PartyQuestSaveKind::Quick));
    REQUIRE(guard.CanSave(PartyQuestSaveKind::ControlledCheckpoint));
    REQUIRE_FALSE(EngineSaveAllowed(guard));

    REQUIRE(guard.Release(1001));
    REQUIRE_FALSE(guard.IsActive());
    REQUIRE(guard.CanSave(PartyQuestSaveKind::Manual));
    REQUIRE(EngineSaveAllowed(guard));
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
    REQUIRE_FALSE(EngineSaveAllowed(guard));

    PartyQuestControlledSaveScope wrongTransaction(guard, 3002);
    REQUIRE_FALSE(wrongTransaction.IsArmed());
    REQUIRE_FALSE(EngineSaveAllowed(guard));

    {
        PartyQuestControlledSaveScope outer(guard, 3001);
        REQUIRE(outer.IsArmed());
        REQUIRE(EngineSaveAllowed(guard));

        {
            PartyQuestControlledSaveScope nested(guard, 3001);
            REQUIRE(nested.IsArmed());
            REQUIRE(EngineSaveAllowed(guard));
        }

        REQUIRE(EngineSaveAllowed(guard));
    }

    REQUIRE_FALSE(EngineSaveAllowed(guard));
    REQUIRE(guard.Release(3001));
    REQUIRE(EngineSaveAllowed(guard));
}

TEST_CASE("Controlled save authorization is thread-local", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(4001) == PartyQuestSaveGuardAcquireStatus::Acquired);

    std::atomic<bool> otherThreadAllowed{true};
    {
        PartyQuestControlledSaveScope scope(guard, 4001);
        REQUIRE(scope.IsArmed());
        REQUIRE(EngineSaveAllowed(guard));

        std::thread worker([&]()
        {
            auto permit = guard.TryEnterEngineSave();
            otherThreadAllowed.store(
                permit.IsAllowed(),
                std::memory_order_release);
        });
        worker.join();

        REQUIRE_FALSE(otherThreadAllowed.load(std::memory_order_acquire));
        REQUIRE(EngineSaveAllowed(guard));
    }

    REQUIRE_FALSE(EngineSaveAllowed(guard));
    REQUIRE(guard.Release(4001));
}

TEST_CASE("A stale controlled scope cannot authorize a later transaction", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    REQUIRE(guard.Acquire(5001) == PartyQuestSaveGuardAcquireStatus::Acquired);

    {
        PartyQuestControlledSaveScope staleScope(guard, 5001);
        REQUIRE(staleScope.IsArmed());
        REQUIRE(EngineSaveAllowed(guard));

        REQUIRE(guard.Release(5001));
        REQUIRE(guard.Acquire(5002) == PartyQuestSaveGuardAcquireStatus::Acquired);
        REQUIRE_FALSE(EngineSaveAllowed(guard));
    }

    REQUIRE_FALSE(EngineSaveAllowed(guard));
    REQUIRE(guard.Release(5002));
}

TEST_CASE("Critical repair acquisition drains an already running engine save", "[quest.party-state.save-guard]")
{
    PartyQuestSaveGuard guard;
    std::atomic<bool> saveAttempted{false};
    std::atomic<bool> saveAllowed{false};
    std::atomic<bool> releaseSave{false};
    std::atomic<bool> acquireStarted{false};
    std::atomic<bool> acquireFinished{false};
    std::atomic<PartyQuestSaveGuardAcquireStatus> acquireStatus{
        PartyQuestSaveGuardAcquireStatus::Busy};

    std::thread saveThread([&]()
    {
        auto permit = guard.TryEnterEngineSave();
        saveAllowed.store(permit.IsAllowed(), std::memory_order_release);
        saveAttempted.store(true, std::memory_order_release);
        if (!permit.IsAllowed())
            return;

        while (!releaseSave.load(std::memory_order_acquire))
            std::this_thread::yield();
    });

    while (!saveAttempted.load(std::memory_order_acquire))
        std::this_thread::yield();

    if (!saveAllowed.load(std::memory_order_acquire))
    {
        releaseSave.store(true, std::memory_order_release);
        saveThread.join();
        REQUIRE(saveAllowed.load(std::memory_order_acquire));
        return;
    }

    std::thread acquireThread([&]()
    {
        acquireStarted.store(true, std::memory_order_release);
        acquireStatus.store(guard.Acquire(6001), std::memory_order_release);
        acquireFinished.store(true, std::memory_order_release);
    });

    while (!acquireStarted.load(std::memory_order_acquire))
        std::this_thread::yield();

    for (int i = 0; i < 256; ++i)
        std::this_thread::yield();
    const bool acquiredWhileSaveWasRunning =
        acquireFinished.load(std::memory_order_acquire);

    releaseSave.store(true, std::memory_order_release);
    saveThread.join();
    acquireThread.join();

    REQUIRE_FALSE(acquiredWhileSaveWasRunning);
    REQUIRE(acquireFinished.load(std::memory_order_acquire));
    REQUIRE(acquireStatus.load(std::memory_order_acquire) ==
        PartyQuestSaveGuardAcquireStatus::Acquired);
    REQUIRE(guard.GetTransactionId() == 6001);
    REQUIRE(guard.Release(6001));
}
