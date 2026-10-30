#include <TiltedCore/Stl.hpp>
#include <glm/glm.hpp>
using namespace TiltedPhoques;

#include <Components.h>
#include <Systems/DeferredActorSetup.h>
#include <catch2/catch.hpp>
#include <entt/entt.hpp>
#include <limits>

TEST_CASE("Ready actor setup still needs a connected current grant", "[actions.session]")
{
    REQUIRE(CanConsumeDeferredActorSetup(true, 17, 4, 17, 4));
    // Loading the reference after disconnect does not restore authority.
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(false, 17, 4, 17, 4));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 4, 18, 4));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 4, 17, 5));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 5, 17, 4));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 0, 17, 0));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 4, 17, 0));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, 17, 0, 17, 4));
}

TEST_CASE("Deferred actor identities preserve valid zero entity IDs and full epoch width", "[actions.session]")
{
    constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
    REQUIRE(CanConsumeDeferredActorSetup(true, 0, 1, 0, 1));
    REQUIRE(CanConsumeDeferredActorSetup(true, max, max, max, max));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, max, max, max, 1));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(false, 0, 1, 0, 1));
}

TEST_CASE("Disconnect retires actual pending 3D and playback components before later readiness", "[actions.session]")
{
    entt::registry world;
    const auto entity = world.create();
    world.emplace<FormIdComponent>(entity, 0x1234u);
    world.emplace<RemoteComponent>(entity, 17u, 0x1234u, 4u);
    CharacterSpawnRequest request;
    request.ServerId = 17;
    request.OwnershipEpoch = 4;
    request.ActionsToReplay.Actions.resize(1);
    request.ActionsToReplay.Actions.front().EventName = "old-session";
    world.emplace<WaitingFor3D>(entity, request);
    auto& playback = world.emplace<RemoteAnimationComponent>(entity);
    playback.TimePoints.push_back(request.ActionsToReplay.Actions.front());
    playback.ReplayCount = 1;
    playback.ResetAnimationGraphForReplay = true;

    REQUIRE(world.all_of<WaitingFor3D, RemoteAnimationComponent>(entity));
    RetireDeferredActorSetupOnDisconnect(world);
    REQUIRE_FALSE(world.any_of<WaitingFor3D, RemoteAnimationComponent>(entity));
    REQUIRE(world.valid(entity));
    REQUIRE(world.get<FormIdComponent>(entity).Id == 0x1234);
    REQUIRE(world.get<RemoteComponent>(entity).Id == 17);
    REQUIRE(world.get<RemoteComponent>(entity).OwnershipEpoch == 4);

    // Use the production consumer's component membership. No reference-ready
    // callback can find the retired setup, even before owner teardown completes.
    uint32_t consumed = 0;
    for (const auto pending : world.view<FormIdComponent, RemoteComponent, WaitingFor3D>())
    {
        (void)pending;
        ++consumed;
    }
    REQUIRE(consumed == 0);
    RetireDeferredActorSetupOnDisconnect(world);
    REQUIRE_FALSE(world.any_of<WaitingFor3D, RemoteAnimationComponent>(entity));
}

TEST_CASE("A deferred setup without its remote owner is absent from the consumer view", "[actions.session]")
{
    entt::registry world;
    const auto entity = world.create();
    world.emplace<FormIdComponent>(entity, 0x1234u);
    world.emplace<WaitingFor3D>(entity, CharacterSpawnRequest{});
    REQUIRE(world.all_of<FormIdComponent, WaitingFor3D>(entity));
    const auto view = world.view<FormIdComponent, RemoteComponent, WaitingFor3D>();
    REQUIRE(view.begin() == view.end());
    RetireDeferredActorSetupOnDisconnect(world);
    REQUIRE_FALSE(world.all_of<WaitingFor3D>(entity));
}

TEST_CASE("Disconnect before any spawn does not create component storage", "[actions.session]")
{
    entt::registry world;
    const entt::registry& existing = world;
    REQUIRE(existing.storage().begin() == existing.storage().end());
    RetireDeferredActorSetupOnDisconnect(world);
    REQUIRE(existing.storage().begin() == existing.storage().end());
    RetireDeferredActorSetupOnDisconnect(world);
    REQUIRE(existing.storage().begin() == existing.storage().end());
    REQUIRE(world.alive() == 0);
}

TEST_CASE("Reconnect may reuse actor numbers but cannot resurrect the retired payload", "[actions.session]")
{
    entt::registry world;
    const auto entity = world.create();
    world.emplace<FormIdComponent>(entity, 0x1234u);
    world.emplace<RemoteComponent>(entity, 17u, 0x1234u, 4u);
    CharacterSpawnRequest old;
    old.ServerId = 17;
    old.OwnershipEpoch = 4;
    old.ActionsToReplay.Actions.resize(1);
    old.ActionsToReplay.Actions.front().EventName = "old-session";
    world.emplace<WaitingFor3D>(entity, old);
    world.emplace<RemoteAnimationComponent>(entity);
    RetireDeferredActorSetupOnDisconnect(world);

    CharacterSpawnRequest fresh = old;
    fresh.ActionsToReplay.Actions.front().EventName = "new-session";
    const auto& pending = world.emplace<WaitingFor3D>(entity, fresh).SpawnRequest;
    const auto& remote = world.get<RemoteComponent>(entity);
    REQUIRE(CanConsumeDeferredActorSetup(true, pending.ServerId, pending.OwnershipEpoch, remote.Id, remote.OwnershipEpoch));
    REQUIRE(pending.ActionsToReplay.Actions.size() == 1);
    REQUIRE(pending.ActionsToReplay.Actions.front().EventName == "new-session");
    REQUIRE_FALSE(world.all_of<RemoteAnimationComponent>(entity));
    REQUIRE(world.get<FormIdComponent>(entity).Id == 0x1234);
}

TEST_CASE("Failed actor grant validation does not modify the pending snapshot", "[actions.session]")
{
    CharacterSpawnRequest pending;
    pending.ServerId = 17;
    pending.OwnershipEpoch = 4;
    pending.ActionsToReplay.Actions.resize(1);
    pending.ActionsToReplay.Actions.front().EventName = "must-stay-unchanged";
    const auto before = pending;
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(false, pending.ServerId, pending.OwnershipEpoch, 17, 4));
    REQUIRE_FALSE(CanConsumeDeferredActorSetup(true, pending.ServerId, pending.OwnershipEpoch, 17, 5));
    REQUIRE(pending == before);
}
