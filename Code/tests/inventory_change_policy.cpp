#include <Services/InventoryChangePublication.h>
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

RequestInventoryChanges Request(const InventoryChangeContext& acContext, const Inventory::Entry& acEntry)
{
    RequestInventoryChanges request;
    request.ServerId = 0x12345678;
    request.OwnershipEpoch = acContext.RequestedEpoch;
    request.Item = acEntry;
    request.Drop = acContext.DropRequested;
    request.UpdateClients = acContext.UpdateClients;
    return request;
}

Inventory::Entry Gold(int32_t aCount)
{
    auto entry = Entry(aCount);
    entry.BaseId = GameId(0, 0xF);
    return entry;
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
    const auto before = inventory.Entries;
    TiltedPhoques::Vector<NotifyInventoryChanges> outgoing;
    const auto result = InventoryChangePublication::ApplyAndPublish(
        inventory, Request(context, Entry(-1)), context,
        [&](const NotifyInventoryChanges& acNotify) { outgoing.push_back(acNotify); });
    REQUIRE_FALSE(result.Applied);
    REQUIRE_FALSE(result.Relay);
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 3);
    REQUIRE(inventory.Entries == before);
    REQUIRE(outgoing.empty());
}

TEST_CASE("Inventory intake applies one delta and keeps legitimate relay policies", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(3)));
    auto context = Owner();
    bool expectRelay = true;
    bool expectDrop = true;
    SECTION("owner with relay and drop")
    {
    }
    SECTION("owner without relay")
    {
        context.UpdateClients = false;
        expectRelay = false;
    }
    SECTION("disabled drops do not enter the outgoing envelope")
    {
        context.DropsEnabled = false;
        expectDrop = false;
    }
    SECTION("an ordinary inventory change does not become a drop")
    {
        context.DropRequested = false;
        expectDrop = false;
    }
    SECTION("nearby non-owner NPC relays even when caller suppresses updates")
    {
        context.SenderIsOwner = false;
        context.TargetIsNpc = context.InRange = true;
        context.UpdateClients = false;
        expectDrop = false;
    }
    SECTION("unowned object uses zero epoch")
    {
        context.HasOwnerComponent = false;
        context.RequestedEpoch = 0;
    }
    const auto request = Request(context, Entry(-1));
    auto itemId = request.Item.BaseId;
    TiltedPhoques::Vector<NotifyInventoryChanges> outgoing;
    int32_t countAtPublication = -1;
    const auto result = InventoryChangePublication::ApplyAndPublish(inventory, request, context,
        [&](const NotifyInventoryChanges& acNotify) {
            countAtPublication = inventory.GetEntryCountById(itemId);
            outgoing.push_back(acNotify);
        });
    REQUIRE(result.Applied);
    REQUIRE(result.Relay == expectRelay);
    REQUIRE(result.Drop == expectDrop);
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 2);
    REQUIRE(outgoing.size() == (expectRelay ? 1 : 0));
    if (expectRelay)
    {
        REQUIRE(countAtPublication == 2);
        REQUIRE(outgoing[0].ServerId == request.ServerId);
        REQUIRE(outgoing[0].OwnershipEpoch == request.OwnershipEpoch);
        REQUIRE(outgoing[0].Item == request.Item);
        REQUIRE(outgoing[0].Drop == expectDrop);
    }
}

TEST_CASE("Inventory intake arithmetic failure cannot relay or partly publish", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(std::numeric_limits<int32_t>::max())));
    const auto context = Owner();
    const auto before = inventory.Entries;
    TiltedPhoques::Vector<NotifyInventoryChanges> outgoing;
    const auto r = InventoryChangePublication::ApplyAndPublish(inventory, Request(context, Entry(1)), context,
        [&](const NotifyInventoryChanges& acNotify) { outgoing.push_back(acNotify); });
    REQUIRE_FALSE(r.Applied); REQUIRE_FALSE(r.Relay); REQUIRE_FALSE(r.Drop);
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == std::numeric_limits<int32_t>::max());
    REQUIRE(inventory.Entries == before);
    REQUIRE(outgoing.empty());
}

TEST_CASE("Respawn gold loss publishes one delta with current epoch before the personal result", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Gold(10)));
    const auto delta = Gold(-3);
    auto goldId = delta.BaseId;
    TiltedPhoques::Vector<NotifyInventoryChanges> inventoryMessages;
    TiltedPhoques::Vector<NotifyPlayerRespawn> personalMessages;
    TiltedPhoques::Vector<uint32_t> order;
    int32_t countAtPublication = -1;
    const bool applied = InventoryChangePublication::ApplyRespawnGoldChange(
        inventory, 0x12345678, 0xFEDCBA98, delta, 3,
        [&](const NotifyInventoryChanges& acNotify) {
            countAtPublication = inventory.GetEntryCountById(goldId);
            inventoryMessages.push_back(acNotify);
            order.push_back(1);
        },
        [&](const NotifyPlayerRespawn& acNotify) {
            personalMessages.push_back(acNotify);
            order.push_back(2);
        });
    REQUIRE(applied);
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 7);
    REQUIRE(countAtPublication == 7);
    REQUIRE(inventoryMessages.size() == 1);
    REQUIRE(inventoryMessages[0].ServerId == 0x12345678);
    REQUIRE(inventoryMessages[0].OwnershipEpoch == 0xFEDCBA98);
    REQUIRE(inventoryMessages[0].Item == delta);
    REQUIRE_FALSE(inventoryMessages[0].Drop);
    REQUIRE(personalMessages.size() == 1);
    REQUIRE(personalMessages[0].GoldLost == 3);
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
}

TEST_CASE("Failed respawn gold change preserves inventory and reports zero loss without a delta", "[inventory.intake]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(Entry(4)));
    auto delta = Gold(-3);
    SECTION("gold is missing") {}
    SECTION("zero delta is rejected") { delta.Count = 0; }
    const auto before = inventory.Entries;
    TiltedPhoques::Vector<NotifyInventoryChanges> inventoryMessages;
    TiltedPhoques::Vector<NotifyPlayerRespawn> personalMessages;
    const bool applied = InventoryChangePublication::ApplyRespawnGoldChange(
        inventory, 0x12345678, 0xFEDCBA98, delta, 3,
        [&](const NotifyInventoryChanges& acNotify) { inventoryMessages.push_back(acNotify); },
        [&](const NotifyPlayerRespawn& acNotify) { personalMessages.push_back(acNotify); });
    REQUIRE_FALSE(applied);
    REQUIRE(inventory.Entries == before);
    REQUIRE(inventoryMessages.empty());
    REQUIRE(personalMessages.size() == 1);
    REQUIRE(personalMessages[0].GoldLost == 0);
}
