#include <Structs/Skyrim/PartyQuestExceptionBoundary.h>

#include <catch2/catch.hpp>

#include <stdexcept>

TEST_CASE("Party quest exception boundary contains callback failures", "[quest.party-state.exception-boundary]")
{
    bool entered = false;
    const bool completed = PartyQuestExceptionBoundary::Invoke(
        [&]()
        {
            entered = true;
            throw std::runtime_error("injected callback failure");
        });

    CHECK(entered);
    CHECK_FALSE(completed);
}

TEST_CASE("Party quest exception boundary returns the fail-closed fallback", "[quest.party-state.exception-boundary]")
{
    CHECK(
        PartyQuestExceptionBoundary::InvokeOr<int>(
            17,
            []() -> int
            {
                throw std::runtime_error("injected value failure");
            }) == 17);

    CHECK(
        PartyQuestExceptionBoundary::InvokeOr<int>(
            17,
            []() -> int
            {
                return 23;
            }) == 23);
}
