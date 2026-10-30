#pragma once

#include <Structs/Inventory.h>

struct InventoryChangeContext
{
    bool EntityExists{};
    bool HasOwnerComponent{};
    bool HasOwner{};
    bool SenderIsOwner{};
    bool TargetIsNpc{};
    bool InRange{};
    uint32_t CurrentEpoch{};
    uint32_t RequestedEpoch{};
    bool UpdateClients{};
    bool DropsEnabled{};
    bool DropRequested{};
};

struct InventoryChangeResult
{
    bool Applied{};
    bool Relay{};
    bool Drop{};
};

inline InventoryChangeResult ApplyAuthorizedInventoryChange(
    Inventory& aInventory, const Inventory::Entry& acEntry, const InventoryChangeContext& acContext) noexcept
{
    if (!acContext.EntityExists)
        return {};
    bool remoteInteraction = false;
    if (acContext.HasOwnerComponent)
    {
        if (!acContext.HasOwner || acContext.CurrentEpoch == 0 || acContext.CurrentEpoch != acContext.RequestedEpoch)
            return {};
        if (!acContext.SenderIsOwner)
        {
            remoteInteraction = acContext.TargetIsNpc && acContext.InRange;
            if (!remoteInteraction)
                return {};
        }
    }
    else if (acContext.RequestedEpoch != 0)
        return {};

    if (!aInventory.AddOrRemoveEntry(acEntry))
        return {};
    return {true, acContext.UpdateClients || remoteInteraction,
        acContext.DropsEnabled && !remoteInteraction && acContext.DropRequested};
}
