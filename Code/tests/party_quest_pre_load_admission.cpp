#include <Structs/Skyrim/PartyQuestPreLoadAdmission.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>

namespace
{
PartyQuestPreLoadAdmission Allow(void*)
{
    return PartyQuestPreLoadAdmission::Allowed;
}

PartyQuestPreLoadAdmission Block(void*)
{
    return PartyQuestPreLoadAdmission::Blocked;
}

PartyQuestPreLoadAdmission Throw(void*)
{
    throw std::runtime_error("test callback failure");
}

struct BlockingCallbackState
{
    std::mutex Mutex;
    std::condition_variable Changed;
    bool Entered{};
    bool Release{};
};

struct ReentrantUnregisterState
{
    PartyQuestPreLoadAdmissionRegistry* Registry{};
    PartyQuestPreLoadAdmissionRegistry::Registration* Registration{};
    PartyQuestPreLoadAdmissionUnregisterStatus Status{
        PartyQuestPreLoadAdmissionUnregisterStatus::SynchronizationFailed};
};

PartyQuestPreLoadAdmission UnregisterFromCallback(void* apContext)
{
    auto& state = *static_cast<ReentrantUnregisterState*>(apContext);
    state.Status = state.Registry->Unregister(*state.Registration);
    return PartyQuestPreLoadAdmission::Allowed;
}

PartyQuestPreLoadAdmission WaitUntilReleased(void* apContext)
{
    auto& state = *static_cast<BlockingCallbackState*>(apContext);
    std::unique_lock lock(state.Mutex);
    state.Entered = true;
    state.Changed.notify_all();
    state.Changed.wait(lock, [&state]() noexcept
    {
        return state.Release;
    });
    return PartyQuestPreLoadAdmission::Allowed;
}
}

TEST_CASE("Pre-Load admission is fail closed without a registered owner",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
}

TEST_CASE("Pre-Load admission publishes only one exact owner",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto first = registry.Register(nullptr, &Allow);
    REQUIRE(first.Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::Registered);
    REQUIRE(first.Token);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Allowed);

    auto duplicate = registry.Register(nullptr, &Block);
    REQUIRE(duplicate.Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::OwnerAlreadyRegistered);
    REQUIRE_FALSE(duplicate.Token);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Allowed);

    REQUIRE(registry.Unregister(*first.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
}

TEST_CASE("Pre-Load callback failures are contained and blocked",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    REQUIRE(registry.Register(nullptr, nullptr).Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::InvalidCallback);

    auto registered = registry.Register(nullptr, &Throw);
    REQUIRE(registered.Token);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Pre-Load unregister closes admission and drains callback leases",
          "[quest.party-state][pre-load-admission][concurrency]")
{
    using namespace std::chrono_literals;

    PartyQuestPreLoadAdmissionRegistry registry;
    BlockingCallbackState state;
    auto registered = registry.Register(&state, &WaitUntilReleased);
    REQUIRE(registered.Token);

    auto evaluation = std::async(std::launch::async, [&registry]()
    {
        return registry.Evaluate();
    });
    {
        std::unique_lock lock(state.Mutex);
        REQUIRE(state.Changed.wait_for(lock, 2s, [&state]() noexcept
        {
            return state.Entered;
        }));
    }

    auto unregister = std::async(
        std::launch::async,
        [&registry, &registered]()
        {
            return registry.Unregister(*registered.Token);
        });

    // Unregister cannot release the owner's context until the callback that
    // acquired the prior lease exits.
    REQUIRE(unregister.wait_for(50ms) == std::future_status::timeout);
    {
        std::lock_guard lock(state.Mutex);
        state.Release = true;
    }
    state.Changed.notify_all();

    REQUIRE(evaluation.get() == PartyQuestPreLoadAdmission::Blocked);
    REQUIRE(unregister.get() ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
}

TEST_CASE("Stale Pre-Load registration cannot remove its replacement",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto first = registry.Register(nullptr, &Allow);
    REQUIRE(first.Token);

    auto moved = std::move(*first.Token);
    REQUIRE_FALSE(first.Token->IsValid());
    REQUIRE(registry.Unregister(moved) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);

    auto replacement = registry.Register(nullptr, &Block);
    REQUIRE(replacement.Token);
    REQUIRE(registry.Unregister(moved) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::StaleRegistration);
    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
    REQUIRE(registry.Unregister(*replacement.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Reentrant Pre-Load unregister defers destruction and blocks admission",
          "[quest.party-state][pre-load-admission][reentrant]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    ReentrantUnregisterState state;
    state.Registry = &registry;
    auto registered = registry.Register(&state, &UnregisterFromCallback);
    REQUIRE(registered.Token);
    state.Registration = &*registered.Token;

    REQUIRE(registry.Evaluate() == PartyQuestPreLoadAdmission::Blocked);
    REQUIRE(state.Status ==
        PartyQuestPreLoadAdmissionUnregisterStatus::
            ReentrantCallbackDeferred);
    REQUIRE(registered.Token->IsValid());

    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE_FALSE(registered.Token->IsValid());
}
