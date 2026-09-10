#pragma once

#include <entt/entity/registry.hpp>
#include <entt/locator/locator.hpp>

// World inherits entt::registry while its dispatcher/transport are derived-class
// members. C++ destroys those members before the registry base, so leaving
// context-owned services to registry destruction would let scoped connections
// outlive the dispatcher they reference. Drain the context explicitly from the
// World destructor body while every derived member is still alive.
template <class TRegistry>
void PartyQuestDestroyRegistryContextBeforeMembers(TRegistry& aRegistry) noexcept
{
    aRegistry.ctx() = {};
}

/** Destroy a located service at an explicit orderly lifecycle boundary. */
template <class TService>
void PartyQuestDestroyLocatedService() noexcept
{
    if (entt::locator<TService>::has_value())
        entt::locator<TService>::reset();
}
