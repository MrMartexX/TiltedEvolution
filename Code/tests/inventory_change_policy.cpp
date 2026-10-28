#include <Structs/InventoryChangePolicy.h>
#include <catch2/catch.hpp>
#include <limits>

namespace
{
Inventory::Entry Entry(int32_t aCount)
{
    Inventory::Entry result;
    result.BaseId = GameId(1, 42);
    result.Count = aCount;
    return result;
}
InventoryChangeContext Owner()
{
    InventoryChangeContext c;
    c.EntityExists = c.HasOwnerComponent = c.HasOwner = c.SenderIsOwner = true;
    c.CurrentEpoch = c.RequestedEpoch = 7;
    c.UpdateClients = true;
    c.DropsEnabled = c.DropRequested = true;
    return c;
}
}

TEST_CASE("Inventory intake rejects authority failures before changing counts", "[inventory.intake]")
{
    auto context = Owner();
    SECTION("missing entity") { context.EntityExists = false; }
    SECTION("stale epoch") { context.RequestedEpoch = 6; }
    SECTION("unowned actor") { context.HasOwner = false; }
    SECTION("zero owner incarnation") { context.CurrentEpoch = context.RequestedEpoch = 0; }
    SECTION("non-owner player") { context.SenderIsOwner = false; context.InRange = true; }
    SECTION("non-owner distant NPC") { context.SenderIsOwner = false; context.TargetIsNpc = true; }
    SECTION("unexpected epoch on object") { context.HasOwnerComponent = false; }
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(3)));
    const auto result = ApplyAuthorizedInventoryChange(inventory, Entry(-1), context);
    REQUIRE_FALSE(result.Applied);
    REQUIRE_FALSE(result.Relay);
    REQUIRE(inventory.Entries[0].Count == 3);
}

TEST_CASE("Inventory intake applies one delta and keeps legitimate relay policies", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(3)));
    auto context = Owner();
    SECTION("owner with relay and drop")
    {
        const auto r = ApplyAuthorizedInventoryChange(inventory, Entry(-1), context);
        REQUIRE(r.Applied); REQUIRE(r.Relay); REQUIRE(r.Drop);
    }
    SECTION("owner without relay")
    {
        context.UpdateClients = false;
        const auto r = ApplyAuthorizedInventoryChange(inventory, Entry(-1), context);
        REQUIRE(r.Applied); REQUIRE_FALSE(r.Relay);
    }
    SECTION("nearby non-owner NPC relays even when caller suppresses updates")
    {
        context.SenderIsOwner = false;
        context.TargetIsNpc = context.InRange = true;
        context.UpdateClients = false;
        const auto r = ApplyAuthorizedInventoryChange(inventory, Entry(-1), context);
        REQUIRE(r.Applied); REQUIRE(r.Relay); REQUIRE_FALSE(r.Drop);
    }
    SECTION("unowned object uses zero epoch")
    {
        context.HasOwnerComponent = false;
        context.RequestedEpoch = 0;
        const auto r = ApplyAuthorizedInventoryChange(inventory, Entry(-1), context);
        REQUIRE(r.Applied); REQUIRE(r.Relay);
    }
    REQUIRE(inventory.Entries[0].Count == 2);
}

TEST_CASE("Inventory intake arithmetic failure cannot relay or partly publish", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(std::numeric_limits<int32_t>::max())));
    const auto r = ApplyAuthorizedInventoryChange(inventory, Entry(1), Owner());
    REQUIRE_FALSE(r.Applied); REQUIRE_FALSE(r.Relay); REQUIRE_FALSE(r.Drop);
    REQUIRE(inventory.Entries[0].Count == std::numeric_limits<int32_t>::max());
}
