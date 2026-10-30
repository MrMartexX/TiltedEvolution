#include "InventoryService.h"

#include <Components.h>
#include <Services/InventoryChangePublication.h>
#include <World.h>
#include <GameServer.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>

#include <Setting.h>
namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", false};
}

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_inventoryChangeConnection = aDispatcher.sink<PacketEvent<RequestInventoryChanges>>().connect<&InventoryService::OnInventoryChanges>(this);
    m_equipmentChangeConnection = aDispatcher.sink<PacketEvent<RequestEquipmentChanges>>().connect<&InventoryService::OnEquipmentChanges>(this);
    m_drawWeaponConnection = aDispatcher.sink<PacketEvent<DrawWeaponRequest>>().connect<&InventoryService::OnWeaponDrawnRequest>(this);
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    auto view = m_world.view<InventoryComponent>();
    const auto it = view.find(static_cast<entt::entity>(message.ServerId));
    if (it == view.end())
        return;

    const auto* pOwner = m_world.try_get<OwnerComponent>(*it);
    const auto* pCharacter = m_world.try_get<CharacterComponent>(*it);
    const auto* pCell = m_world.try_get<CellIdComponent>(*it);
    InventoryChangeContext context;
    context.EntityExists = true;
    context.HasOwnerComponent = pOwner != nullptr;
    context.HasOwner = pOwner && pOwner->GetOwner();
    context.SenderIsOwner = pOwner && pOwner->GetOwner() == acMessage.pPlayer;
    context.TargetIsNpc = pCharacter && !pCharacter->IsPlayer();
    context.InRange = pCharacter && pCell &&
        acMessage.pPlayer->GetCellComponent().IsInRange(*pCell, pCharacter->IsDragon());
    context.CurrentEpoch = pOwner ? pOwner->OwnershipEpoch : 0;
    context.RequestedEpoch = message.OwnershipEpoch;
    context.UpdateClients = message.UpdateClients;
    context.DropsEnabled = static_cast<bool>(bEnableItemDrops);
    context.DropRequested = message.Drop;

    auto& inventory = view.get<InventoryComponent>(*it).Content;
    const auto result = InventoryChangePublication::ApplyAndPublish(inventory, message, context,
        [&](const NotifyInventoryChanges& acNotify) {
            if (!GameServer::Get()->SendToPlayersInRange(acNotify, *it, acMessage.GetSender()))
                spdlog::error("InventoryService::OnInventoryChanges: SendToPlayersInRange failed");
        });
    if (!result.Applied)
    {
        spdlog::debug("Rejected inventory delta: player={:X} actor={:X} requestedEpoch={} currentEpoch={}",
            acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch, context.CurrentEpoch);
        return;
    }
}

void InventoryService::OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
        return;

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    if (pOwnerComponent)
    {
        if (pOwnerComponent->GetOwner() != acMessage.pPlayer || pOwnerComponent->OwnershipEpoch != message.OwnershipEpoch)
        {
            const uint32_t ownerId = pOwnerComponent->GetOwner() ? pOwnerComponent->GetOwner()->GetId() : 0;
            spdlog::debug(
                "Rejected equipment change from player {:X} for actor {:X}; current owner is {:X} and requested epoch {} does not match {}",
                acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, pOwnerComponent->OwnershipEpoch);
            return;
        }
    }
    else if (message.OwnershipEpoch != 0)
    {
        spdlog::warn(
            "Rejected equipment change from player {:X} because object {:X} unexpectedly carried ownership epoch {}",
            acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch);
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);

    NotifyEquipmentChanges notify;
    notify.ServerId = message.ServerId;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.ItemId = message.ItemId;
    notify.EquipSlotId = message.EquipSlotId;
    notify.Count = message.Count;
    notify.Unequip = message.Unequip;
    notify.IsSpell = message.IsSpell;
    notify.IsShout = message.IsShout;

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).GetOwner() == acMessage.pPlayer)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);
    }
}
