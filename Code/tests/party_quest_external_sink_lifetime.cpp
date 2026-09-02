#include <catch2/catch.hpp>

#include <Structs/Skyrim/PartyQuestExternalSinkLifetime.h>

namespace
{
struct ExternalSinkProbe
{
};

struct ExternalDispatcherProbe
{
    ExternalSinkProbe* Registered{};
    unsigned UnregisterCalls{};
    bool SinkMatched{true};

    void UnRegisterSink(ExternalSinkProbe* apSink) noexcept
    {
        SinkMatched = SinkMatched && apSink == Registered;
        ++UnregisterCalls;
        Registered = nullptr;
    }
};
}

TEST_CASE("PartyQuest external sink registration is released exactly once")
{
    ExternalSinkProbe sink;
    ExternalDispatcherProbe dispatcher{&sink};
    ExternalDispatcherProbe* pRegistration = &dispatcher;

    PartyQuestReleaseExternalSink(pRegistration, &sink);
    REQUIRE(pRegistration == nullptr);
    REQUIRE(dispatcher.Registered == nullptr);
    REQUIRE(dispatcher.SinkMatched);
    REQUIRE(dispatcher.UnregisterCalls == 1);

    PartyQuestReleaseExternalSink(pRegistration, &sink);
    REQUIRE(dispatcher.UnregisterCalls == 1);
}
