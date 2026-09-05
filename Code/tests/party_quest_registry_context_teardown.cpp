#include <catch2/catch.hpp>
#include <entt/entt.hpp>

#include <Structs/Skyrim/PartyQuestRegistryContextTeardown.h>

namespace
{
struct RegistryContextTeardownProbeEvent
{
};

struct RegistryContextTeardownProbeService
{
    int& CallbackCount;
    bool& Destroyed;
    entt::scoped_connection Connection;

    RegistryContextTeardownProbeService(
        entt::dispatcher& aDispatcher,
        int& aCallbackCount,
        bool& aDestroyed)
        : CallbackCount(aCallbackCount)
        , Destroyed(aDestroyed)
        , Connection(
              aDispatcher.sink<RegistryContextTeardownProbeEvent>()
                  .connect<&RegistryContextTeardownProbeService::Handle>(this))
    {
    }

    ~RegistryContextTeardownProbeService()
    {
        Destroyed = true;
    }

    void Handle(const RegistryContextTeardownProbeEvent&)
    {
        ++CallbackCount;
    }
};

struct LocatedServiceProbe
{
    explicit LocatedServiceProbe(bool& aDestroyed)
        : Destroyed(aDestroyed)
    {
    }

    ~LocatedServiceProbe()
    {
        Destroyed = true;
    }

    bool& Destroyed;
};
}

TEST_CASE("PartyQuest registry context is drained before dispatcher member teardown")
{
    entt::registry registry;
    entt::dispatcher dispatcher;
    int callbackCount = 0;
    bool destroyed = false;

    registry.ctx().emplace<RegistryContextTeardownProbeService>(
        dispatcher, callbackCount, destroyed);

    dispatcher.trigger<RegistryContextTeardownProbeEvent>();
    REQUIRE(callbackCount == 1);
    REQUIRE_FALSE(destroyed);

    PartyQuestDestroyRegistryContextBeforeMembers(registry);

    REQUIRE(destroyed);

    // The dispatcher is intentionally still alive here. A context subscriber
    // surviving the explicit drain would receive this event and prove the
    // teardown ordering contract is broken.
    dispatcher.trigger<RegistryContextTeardownProbeEvent>();
    REQUIRE(callbackCount == 1);
}

TEST_CASE("PartyQuest located service is destroyed at the explicit boundary")
{
    bool destroyed = false;
    entt::locator<LocatedServiceProbe>::emplace(destroyed);
    REQUIRE(entt::locator<LocatedServiceProbe>::has_value());

    PartyQuestDestroyLocatedService<LocatedServiceProbe>();
    REQUIRE(destroyed);
    REQUIRE_FALSE(entt::locator<LocatedServiceProbe>::has_value());

    PartyQuestDestroyLocatedService<LocatedServiceProbe>();
    REQUIRE_FALSE(entt::locator<LocatedServiceProbe>::has_value());
}
