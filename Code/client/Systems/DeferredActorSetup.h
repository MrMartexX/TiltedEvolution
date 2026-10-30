#pragma once

#include <cstdint>

struct WaitingFor3D;
struct RemoteAnimationComponent;

// A ready reference is evidence only. The pending spawn snapshot must still
// belong to this connected actor's currently accepted ownership grant.
constexpr bool CanConsumeDeferredActorSetup(
    bool aIsOnline, uint32_t aExpectedServerId, uint32_t aExpectedEpoch,
    uint32_t aCurrentServerId, uint32_t aCurrentEpoch) noexcept
{
    return aIsOnline && aExpectedEpoch != 0 &&
        aExpectedServerId == aCurrentServerId && aExpectedEpoch == aCurrentEpoch;
}

// Revoke stored work before disconnect cleanup can call into the engine.
// The caller retains responsibility for the existing actor/owner teardown.
template <class TRegistry>
void RetireDeferredActorSetupOnDisconnect(TRegistry& aRegistry)
{
    // entt::registry::clear<T>() assures a pool even when T never existed.
    // Disconnect before the first spawn must not allocate new storage.
    const TRegistry& existing = aRegistry;
    if (!existing.template storage<WaitingFor3D>().empty())
        aRegistry.template storage<WaitingFor3D>().clear();
    if (!existing.template storage<RemoteAnimationComponent>().empty())
        aRegistry.template storage<RemoteAnimationComponent>().clear();
}
