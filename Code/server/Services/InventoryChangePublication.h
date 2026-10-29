#pragma once

#include <Structs/InventoryChangePolicy.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyPlayerRespawn.h>

namespace InventoryChangePublication
{
// Keep the checked mutation and its wire envelope in the same testable boundary.
// The service still owns authority lookup and recipient selection.
template <class TPublish>
InventoryChangeResult ApplyAndPublish(
    Inventory& aInventory, const RequestInventoryChanges& acRequest,
    const InventoryChangeContext& acContext, TPublish&& aPublish) noexcept
{
    const auto result = ApplyAuthorizedInventoryChange(aInventory, acRequest.Item, acContext);
    if (result.Relay)
    {
        NotifyInventoryChanges notify;
        notify.ServerId = acRequest.ServerId;
        notify.OwnershipEpoch = acRequest.OwnershipEpoch;
        notify.Item = acRequest.Item;
        notify.Drop = result.Drop;
        aPublish(notify);
    }
    return result;
}

// Preserve the caller's gold-loss calculation. A failed delta sends only the
// personal zero-loss result, never an inventory update claiming a mutation.
template <class TPublishInventory, class TPublishRespawn>
bool ApplyRespawnGoldChange(
    Inventory& aInventory, uint32_t aServerId, uint32_t aOwnershipEpoch,
    const Inventory::Entry& acDelta, int32_t aGoldLost,
    TPublishInventory&& aPublishInventory, TPublishRespawn&& aPublishRespawn) noexcept
{
    const bool applied = aInventory.AddOrRemoveEntry(acDelta);
    if (applied)
    {
        NotifyInventoryChanges notify;
        notify.ServerId = aServerId;
        notify.OwnershipEpoch = aOwnershipEpoch;
        notify.Item = acDelta;
        notify.Drop = false;
        aPublishInventory(notify);
    }

    NotifyPlayerRespawn notify;
    notify.GoldLost = applied ? aGoldLost : 0;
    aPublishRespawn(notify);
    return applied;
}
}
