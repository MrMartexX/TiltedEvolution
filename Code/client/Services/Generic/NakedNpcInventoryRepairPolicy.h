#pragma once

struct NakedNpcInventoryRepairObservation
{
    bool IsWearingBodyPiece{false};
    bool DefaultOutfitContainsBodyPiece{false};
};

struct NakedNpcInventoryRepairPolicy
{
    /**
     * Appearance is not authoritative inventory evidence. Until a canonical inventory
     * ledger exists, a naked-NPC observation must not authorize inventory recreation
     * or a broad inventory reload.
     */
    [[nodiscard]] static constexpr bool CanCreateOrReloadInventory(
        const NakedNpcInventoryRepairObservation& acObservation) noexcept
    {
        (void)acObservation;
        return false;
    }
};
