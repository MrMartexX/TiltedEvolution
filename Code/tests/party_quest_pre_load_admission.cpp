#include <Structs/Skyrim/PartyQuestPreLoadAdmission.h>

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace
{
PartyQuestPreLoadAdmission Allow(void*)
{
    return PartyQuestPreLoadAdmission::Allowed;
}

static_assert(!std::is_copy_constructible_v<
    PartyQuestPreLoadAdmissionRegistry::Attempt>);
static_assert(std::is_nothrow_move_constructible_v<
    PartyQuestPreLoadAdmissionRegistry::Attempt>);
static_assert(!std::is_move_assignable_v<
    PartyQuestPreLoadAdmissionRegistry::Attempt>);

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

struct NestedCallbackState
{
    PartyQuestPreLoadAdmissionRegistry* OuterRegistry{};
    PartyQuestPreLoadAdmissionRegistry* InnerRegistry{};
    PartyQuestPreLoadAdmissionRegistry::Registration* OuterRegistration{};
    std::optional<PartyQuestPreLoadAdmissionRegistry::Registration>*
        DroppedOuterRegistration{};
    PartyQuestPreLoadAdmissionUnregisterStatus UnregisterStatus{
        PartyQuestPreLoadAdmissionUnregisterStatus::SynchronizationFailed};
    bool InnerAllowed{};
    bool DropOuter{};
};

PartyQuestPreLoadAdmission NestedInnerAction(void* apContext)
{
    auto& state = *static_cast<NestedCallbackState*>(apContext);
    if (state.DropOuter)
        state.DroppedOuterRegistration->reset();
    else
        state.UnregisterStatus = state.OuterRegistry->Unregister(
            *state.OuterRegistration);
    return PartyQuestPreLoadAdmission::Allowed;
}

PartyQuestPreLoadAdmission EnterNestedRegistry(void* apContext)
{
    auto& state = *static_cast<NestedCallbackState*>(apContext);
    auto nested = state.InnerRegistry->BeginAttempt();
    state.InnerAllowed = nested.IsAllowed();
    if (nested.Token)
        (void)nested.Token->Abort();
    return PartyQuestPreLoadAdmission::Allowed;
}

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
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
}

TEST_CASE("Pre-Load admission publishes only one exact owner",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto first = registry.Register(nullptr, &Allow);
    REQUIRE(first.Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::Registered);
    REQUIRE(first.Token);
    auto admitted = registry.BeginAttempt();
    REQUIRE(admitted.IsAllowed());
    REQUIRE(admitted.Token->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);

    auto duplicate = registry.Register(nullptr, &Block);
    REQUIRE(duplicate.Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::OwnerAlreadyRegistered);
    REQUIRE_FALSE(duplicate.Token);
    auto stillAdmitted = registry.BeginAttempt();
    REQUIRE(stillAdmitted.IsAllowed());
    REQUIRE(stillAdmitted.Token->Abort() ==
        PartyQuestPreLoadAttemptStatus::Aborted);

    REQUIRE(registry.Unregister(*first.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE(registry.CanDestroyContext());
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
}

TEST_CASE("Pre-Load callback failures are contained and blocked",
          "[quest.party-state][pre-load-admission]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    REQUIRE(registry.Register(nullptr, nullptr).Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::InvalidCallback);

    auto registered = registry.Register(nullptr, &Throw);
    REQUIRE(registered.Token);
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
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
        return registry.BeginAttempt().IsAllowed();
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

    REQUIRE_FALSE(evaluation.get());
    REQUIRE(unregister.get() ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
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
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
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

    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(state.Status ==
        PartyQuestPreLoadAdmissionUnregisterStatus::
            ReentrantCallbackDeferred);
    REQUIRE(registered.Token->IsValid());

    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE_FALSE(registered.Token->IsValid());
}

TEST_CASE("Nested callback unregister detects an outer callback frame",
          "[quest.party-state][pre-load-admission][reentrant][nested]")
{
    PartyQuestPreLoadAdmissionRegistry outer;
    PartyQuestPreLoadAdmissionRegistry inner;
    NestedCallbackState state;
    state.OuterRegistry = &outer;
    state.InnerRegistry = &inner;

    auto innerRegistration = inner.Register(&state, &NestedInnerAction);
    REQUIRE(innerRegistration.Token);
    auto outerRegistration = outer.Register(&state, &EnterNestedRegistry);
    REQUIRE(outerRegistration.Token);
    state.OuterRegistration = &*outerRegistration.Token;

    const auto outerAttempt = outer.BeginAttempt();
    REQUIRE_FALSE(outerAttempt.IsAllowed());
    REQUIRE_FALSE(outerAttempt.Token);
    REQUIRE(state.InnerAllowed);
    REQUIRE(state.UnregisterStatus ==
        PartyQuestPreLoadAdmissionUnregisterStatus::
            ReentrantCallbackDeferred);

    REQUIRE(outer.Unregister(*outerRegistration.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    REQUIRE(inner.Unregister(*innerRegistration.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Nested callback registration drop detects an outer callback frame",
          "[quest.party-state][pre-load-admission][reentrant][nested]")
{
    PartyQuestPreLoadAdmissionRegistry outer;
    PartyQuestPreLoadAdmissionRegistry inner;
    NestedCallbackState state;
    state.OuterRegistry = &outer;
    state.InnerRegistry = &inner;
    state.DropOuter = true;

    auto innerRegistration = inner.Register(&state, &NestedInnerAction);
    REQUIRE(innerRegistration.Token);
    std::optional<PartyQuestPreLoadAdmissionRegistry::Registration>
        outerRegistration;
    auto registeredOuter = outer.Register(&state, &EnterNestedRegistry);
    REQUIRE(registeredOuter.Token);
    outerRegistration.emplace(std::move(*registeredOuter.Token));
    state.DroppedOuterRegistration = &outerRegistration;

    const auto outerAttempt = outer.BeginAttempt();
    REQUIRE_FALSE(outerAttempt.IsAllowed());
    REQUIRE_FALSE(outerAttempt.Token);
    REQUIRE(state.InnerAllowed);
    REQUIRE_FALSE(outerRegistration);
    REQUIRE(outer.IsPoisoned());
    REQUIRE_FALSE(outer.CanDestroyContext());
    REQUIRE_FALSE(outer.BeginAttempt().IsAllowed());

    REQUIRE(inner.Unregister(*innerRegistration.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Pre-Load attempt is exact, exclusive, and explicitly abortable",
          "[quest.party-state][pre-load-admission][attempt]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);

    auto begun = registry.BeginAttempt();
    REQUIRE(begun.IsAllowed());
    REQUIRE(registry.HasActiveAttempt());
    REQUIRE(begun.Token->IsPrepared());
    REQUIRE_FALSE(begun.Token->IsCommitted());

    const auto duplicate = registry.BeginAttempt();
    REQUIRE_FALSE(duplicate.IsAllowed());
    REQUIRE_FALSE(duplicate.Token);

    auto moved = std::move(*begun.Token);
    REQUIRE_FALSE(begun.Token->IsPrepared());
    REQUIRE(moved.IsPrepared());
    REQUIRE(moved.Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE_FALSE(registry.HasActiveAttempt());
    REQUIRE_FALSE(moved.IsPrepared());
    REQUIRE(moved.Abort() == PartyQuestPreLoadAttemptStatus::InvalidState);

    auto replacement = registry.BeginAttempt();
    REQUIRE(replacement.IsAllowed());
    REQUIRE(moved.Abort() == PartyQuestPreLoadAttemptStatus::InvalidState);
    REQUIRE(replacement.Token->IsPrepared());
    REQUIRE(replacement.Token->Abort() ==
        PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Prepared Pre-Load attempt destruction performs exact abort",
          "[quest.party-state][pre-load-admission][attempt]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);
    {
        auto begun = registry.BeginAttempt();
        REQUIRE(begun.IsAllowed());
        REQUIRE(begun.Token->IsPrepared());
    }

    auto next = registry.BeginAttempt();
    REQUIRE(next.IsAllowed());
    REQUIRE(next.Token->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Pre-Load unregister waits for a prepared attempt",
          "[quest.party-state][pre-load-admission][attempt][concurrency]")
{
    using namespace std::chrono_literals;

    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);
    auto begun = registry.BeginAttempt();
    REQUIRE(begun.IsAllowed());

    auto unregister = std::async(std::launch::async, [&]()
    {
        return registry.Unregister(*registered.Token);
    });
    REQUIRE(unregister.wait_for(50ms) == std::future_status::timeout);
    REQUIRE(begun.Token->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE(unregister.get() ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Committed Pre-Load attempt owns admission until exact completion",
          "[quest.party-state][pre-load-admission][attempt][concurrency]")
{
    using namespace std::chrono_literals;

    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);
    auto begun = registry.BeginAttempt();
    REQUIRE(begun.IsAllowed());
    REQUIRE(begun.Token->CommitPublished() ==
        PartyQuestPreLoadAttemptStatus::Committed);
    REQUIRE(registry.HasActiveAttempt());
    REQUIRE(begun.Token->IsCommitted());
    REQUIRE(begun.Token->Abort() ==
        PartyQuestPreLoadAttemptStatus::InvalidState);
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());

    auto unregister = std::async(std::launch::async, [&]()
    {
        return registry.Unregister(*registered.Token);
    });
    REQUIRE(unregister.wait_for(50ms) == std::future_status::timeout);
    REQUIRE(begun.Token->Complete() ==
        PartyQuestPreLoadAttemptStatus::Completed);
    REQUIRE_FALSE(registry.HasActiveAttempt());
    REQUIRE(begun.Token->Complete() ==
        PartyQuestPreLoadAttemptStatus::InvalidState);
    REQUIRE(unregister.get() ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Destroyed committed Pre-Load attempt is explicitly abandoned",
          "[quest.party-state][pre-load-admission][attempt][abandoned]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);
    {
        auto begun = registry.BeginAttempt();
        REQUIRE(begun.IsAllowed());
        REQUIRE(begun.Token->CommitPublished() ==
            PartyQuestPreLoadAttemptStatus::Committed);
    }

    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(registry.HasActiveAttempt());
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(registry.Unregister(*registered.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::AbandonedAttempt);
    REQUIRE(registered.Token->IsValid());
    REQUIRE(registry.IsPoisoned());
    REQUIRE_FALSE(registry.CanDestroyContext());
    REQUIRE_FALSE(registered.Token->CanDestroyContext());
    REQUIRE(registry.Register(nullptr, &Allow).Status ==
        PartyQuestPreLoadAdmissionRegistrationStatus::RegistryClosed);
}

TEST_CASE("Blocked and throwing Pre-Load attempts retire their exact reservation",
          "[quest.party-state][pre-load-admission][attempt]")
{
    SECTION("blocked")
    {
        PartyQuestPreLoadAdmissionRegistry registry;
        auto registered = registry.Register(nullptr, &Block);
        REQUIRE(registered.Token);
        const auto begun = registry.BeginAttempt();
        REQUIRE_FALSE(begun.IsAllowed());
        REQUIRE_FALSE(begun.Token);
        REQUIRE(registry.Unregister(*registered.Token) ==
            PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    }

    SECTION("exception")
    {
        PartyQuestPreLoadAdmissionRegistry registry;
        auto registered = registry.Register(nullptr, &Throw);
        REQUIRE(registered.Token);
        const auto begun = registry.BeginAttempt();
        REQUIRE_FALSE(begun.IsAllowed());
        REQUIRE_FALSE(begun.Token);
        REQUIRE(registry.Unregister(*registered.Token) ==
            PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
    }
}

TEST_CASE("Pre-Load control block outlives its registry facade",
          "[quest.party-state][pre-load-admission][lifetime]")
{
    std::optional<PartyQuestPreLoadAdmissionRegistry::Registration> registration;
    std::optional<PartyQuestPreLoadAdmissionRegistry::Attempt> attempt;
    {
        auto registry = std::make_unique<PartyQuestPreLoadAdmissionRegistry>();
        auto registered = registry->Register(nullptr, &Allow);
        REQUIRE(registered.Token);
        registration.emplace(std::move(*registered.Token));
        auto begun = registry->BeginAttempt();
        REQUIRE(begun.IsAllowed());
        attempt.emplace(std::move(*begun.Token));
    }

    REQUIRE(registration->IsPoisoned());
    REQUIRE_FALSE(registration->CanDestroyContext());
    REQUIRE(attempt->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE_FALSE(registration->CanDestroyContext());
}

TEST_CASE("Dropped Pre-Load registration unpublishes its callback",
          "[quest.party-state][pre-load-admission][lifetime]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    {
        auto registered = registry.Register(nullptr, &Allow);
        REQUIRE(registered.Token);
    }

    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(registry.CanDestroyContext());
    REQUIRE_FALSE(registry.IsPoisoned());

    auto replacement = registry.Register(nullptr, &Block);
    REQUIRE(replacement.Token);
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(registry.Unregister(*replacement.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Dropped registration with a prepared attempt poisons ownership",
          "[quest.party-state][pre-load-admission][lifetime]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    std::optional<PartyQuestPreLoadAdmissionRegistry::Attempt> attempt;
    {
        auto registered = registry.Register(nullptr, &Allow);
        REQUIRE(registered.Token);
        auto begun = registry.BeginAttempt();
        REQUIRE(begun.IsAllowed());
        attempt.emplace(std::move(*begun.Token));
    }

    REQUIRE(registry.IsPoisoned());
    REQUIRE_FALSE(registry.CanDestroyContext());
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
    REQUIRE(attempt->CommitPublished() ==
        PartyQuestPreLoadAttemptStatus::Poisoned);
    REQUIRE(attempt->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
}

TEST_CASE("Dropped registration with an active callback drains and poisons",
          "[quest.party-state][pre-load-admission][lifetime][concurrency]")
{
    using namespace std::chrono_literals;

    PartyQuestPreLoadAdmissionRegistry registry;
    BlockingCallbackState state;
    auto registered = registry.Register(&state, &WaitUntilReleased);
    REQUIRE(registered.Token);
    auto registration = std::move(*registered.Token);

    auto evaluation = std::async(std::launch::async, [&]()
    {
        return registry.BeginAttempt().IsAllowed();
    });
    {
        std::unique_lock lock(state.Mutex);
        REQUIRE(state.Changed.wait_for(lock, 2s, [&]() noexcept
        {
            return state.Entered;
        }));
    }

    auto dropped = std::async(
        std::launch::async,
        [owned = std::move(registration)]() mutable
        {
            { auto exact = std::move(owned); }
            return true;
        });
    REQUIRE(dropped.wait_for(50ms) == std::future_status::timeout);
    {
        std::lock_guard lock(state.Mutex);
        state.Release = true;
    }
    state.Changed.notify_all();

    REQUIRE_FALSE(evaluation.get());
    REQUIRE(dropped.get());
    REQUIRE(registry.IsPoisoned());
    REQUIRE_FALSE(registry.CanDestroyContext());
    REQUIRE_FALSE(registry.BeginAttempt().IsAllowed());
}

TEST_CASE("Prepared Pre-Load attempt may commit while unregister waits",
          "[quest.party-state][pre-load-admission][attempt][concurrency]")
{
    using namespace std::chrono_literals;

    PartyQuestPreLoadAdmissionRegistry registry;
    auto registered = registry.Register(nullptr, &Allow);
    REQUIRE(registered.Token);
    auto begun = registry.BeginAttempt();
    REQUIRE(begun.IsAllowed());

    auto unregister = std::async(std::launch::async, [&]()
    {
        return registry.Unregister(*registered.Token);
    });
    REQUIRE(begun.Token->CommitPublished() ==
        PartyQuestPreLoadAttemptStatus::Committed);
    REQUIRE(unregister.wait_for(50ms) == std::future_status::timeout);
    REQUIRE(begun.Token->Complete() ==
        PartyQuestPreLoadAttemptStatus::Completed);
    REQUIRE(unregister.get() ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}

TEST_CASE("Completed old Pre-Load attempt cannot affect replacement owner",
          "[quest.party-state][pre-load-admission][attempt][aba]")
{
    PartyQuestPreLoadAdmissionRegistry registry;
    auto first = registry.Register(nullptr, &Allow);
    REQUIRE(first.Token);
    auto oldAttempt = registry.BeginAttempt();
    REQUIRE(oldAttempt.IsAllowed());
    REQUIRE(oldAttempt.Token->CommitPublished() ==
        PartyQuestPreLoadAttemptStatus::Committed);
    REQUIRE(oldAttempt.Token->Complete() ==
        PartyQuestPreLoadAttemptStatus::Completed);
    REQUIRE(registry.Unregister(*first.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);

    auto replacement = registry.Register(nullptr, &Allow);
    REQUIRE(replacement.Token);
    auto current = registry.BeginAttempt();
    REQUIRE(current.IsAllowed());
    REQUIRE(oldAttempt.Token->Abort() ==
        PartyQuestPreLoadAttemptStatus::InvalidState);
    REQUIRE(current.Token->IsPrepared());
    REQUIRE(current.Token->Abort() == PartyQuestPreLoadAttemptStatus::Aborted);
    REQUIRE(registry.Unregister(*replacement.Token) ==
        PartyQuestPreLoadAdmissionUnregisterStatus::Unregistered);
}
