#include <catch2/catch.hpp>
#include <entt/entt.hpp>

#include <Structs/Skyrim/PartyQuestModMappingPublication.h>

namespace
{
struct ModMappingTeardownProbeEvent
{
};

struct ModMappingTeardownProbeState
{
    entt::dispatcher& Dispatcher;

    ~ModMappingTeardownProbeState()
    {
        // Model a synchronous dispatcher opportunity while dependent state is
        // being destroyed. A subscription that survives this point can invoke a
        // callback on a partially destroyed owner.
        Dispatcher.trigger<ModMappingTeardownProbeEvent>();
    }
};

struct ModMappingTeardownProbeOwner
{
    int& CallbackCount;
    entt::scoped_connection Connection;
    ModMappingTeardownProbeState State;

    ModMappingTeardownProbeOwner(entt::dispatcher& aDispatcher, int& aCallbackCount)
        : CallbackCount(aCallbackCount)
        , Connection(
              aDispatcher.sink<ModMappingTeardownProbeEvent>()
                  .connect<&ModMappingTeardownProbeOwner::Handle>(this))
        , State{aDispatcher}
    {
    }

    ~ModMappingTeardownProbeOwner()
    {
        // Destructor bodies run before member destruction. Releasing here is the
        // contract ModSystem relies on because its connection is also declared
        // before the callback-dependent mapping state.
        Connection.release();
    }

    void Handle(const ModMappingTeardownProbeEvent&)
    {
        ++CallbackCount;
    }
};
}

TEST_CASE("PartyQuest mod mapping publication remains fail closed until commit")
{
    PartyQuestModMappingPublication publication;

    REQUIRE_FALSE(publication.IsReady());

    publication.BeginRebuild();
    REQUIRE_FALSE(publication.IsReady());

    // A failed/abandoned candidate rebuild performs no Commit().
    REQUIRE_FALSE(publication.IsReady());

    publication.Commit();
    REQUIRE(publication.IsReady());

    // The next rebuild revokes the previously published mapping immediately.
    publication.BeginRebuild();
    REQUIRE_FALSE(publication.IsReady());
}

TEST_CASE("PartyQuest callback subscription is revoked before dependent member teardown")
{
    entt::dispatcher dispatcher;
    int callbackCount = 0;

    {
        ModMappingTeardownProbeOwner owner(dispatcher, callbackCount);
        dispatcher.trigger<ModMappingTeardownProbeEvent>();
        REQUIRE(callbackCount == 1);
    }

    // State destruction above triggers the same event. Early release in the
    // owner destructor must prevent a callback after member teardown begins.
    REQUIRE(callbackCount == 1);
}
