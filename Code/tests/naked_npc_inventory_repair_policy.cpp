#include "../client/Services/Generic/NakedNpcInventoryRepairPolicy.h"

#include <array>

#include <catch2/catch.hpp>

TEST_CASE("Naked NPC appearance alone cannot authorize inventory recreation", "[inventory.naked-npc.safety]")
{
    const NakedNpcInventoryRepairObservation nakedWithDefaultOutfit{
        .IsWearingBodyPiece = false,
        .DefaultOutfitContainsBodyPiece = true,
    };

    REQUIRE_FALSE(NakedNpcInventoryRepairPolicy::CanCreateOrReloadInventory(nakedWithDefaultOutfit));
}

TEST_CASE("Repeated naked NPC observations never authorize item-creating repair", "[inventory.naked-npc.safety]")
{
    constexpr std::array<NakedNpcInventoryRepairObservation, 6> observations{{
        {true, true},  // Initial equipped state.
        {false, true}, // Clothing stolen.
        {false, true}, // Later update tick.
        {false, true}, // Inventory reopened.
        {false, true}, // Equipment/resync observation.
        {true, true},  // Legitimate re-equip must still not authorize recreation.
    }};

    for (const auto& observation : observations)
        CHECK_FALSE(NakedNpcInventoryRepairPolicy::CanCreateOrReloadInventory(observation));
}
