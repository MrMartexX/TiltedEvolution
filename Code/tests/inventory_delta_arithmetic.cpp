#include <Structs/Inventory.h>

#include <catch2/catch.hpp>

#include <limits>

namespace
{
Inventory::Entry MakeEntry(int32_t aCount, float aExtraHealth = 0.0f)
{
    Inventory::Entry entry{};
    entry.BaseId = GameId(1, 0x1234);
    entry.Count = aCount;
    entry.ExtraHealth = aExtraHealth;
    return entry;
}
} // namespace

TEST_CASE("Inventory delta creates only positive absent entries", "[inventory.delta]")
{
    Inventory inventory;

    REQUIRE_FALSE(inventory.AddOrRemoveEntry(MakeEntry(0)));
    REQUIRE_FALSE(inventory.AddOrRemoveEntry(MakeEntry(-1)));
    REQUIRE(inventory.Entries.empty());

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(3)));
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 3);

    REQUIRE_FALSE(inventory.AddOrRemoveEntry(MakeEntry(0)));
    auto absent = MakeEntry(-1);
    absent.BaseId = GameId(1, 0x5678);
    REQUIRE_FALSE(inventory.AddOrRemoveEntry(absent));
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 3);
}

TEST_CASE("Inventory delta preserves legal removal semantics", "[inventory.delta]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(5)));

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(-2)));
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == 3);

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(-3)));
    REQUIRE(inventory.Entries.empty());

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(2)));
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(-3)));
    REQUIRE(inventory.Entries.empty());
}

TEST_CASE("Inventory delta rejects positive overflow without mutation", "[inventory.delta]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(std::numeric_limits<int32_t>::max())));

    REQUIRE_FALSE(inventory.AddOrRemoveEntry(MakeEntry(1)));
    REQUIRE(inventory.Entries.size() == 1);
    REQUIRE(inventory.Entries[0].Count == std::numeric_limits<int32_t>::max());
}

TEST_CASE("Inventory delta handles INT_MIN removal without signed overflow", "[inventory.delta]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(1)));

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(std::numeric_limits<int32_t>::min())));
    REQUIRE(inventory.Entries.empty());
}

TEST_CASE("Inventory delta merges only equal extra-data stacks", "[inventory.delta]")
{
    Inventory inventory;
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(2, 1.0f)));
    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(3, 0.5f)));
    REQUIRE(inventory.Entries.size() == 2);

    REQUIRE(inventory.AddOrRemoveEntry(MakeEntry(-1, 1.0f)));
    REQUIRE(inventory.Entries.size() == 2);
    REQUIRE(inventory.Entries[0].Count == 1);
    REQUIRE(inventory.Entries[1].Count == 3);

    REQUIRE_FALSE(inventory.AddOrRemoveEntry(MakeEntry(-1, 0.25f)));
    REQUIRE(inventory.Entries.size() == 2);
}

TEST_CASE("Inventory snapshots are idempotent while deltas are cumulative", "[inventory.delta]")
{
    Inventory snapshot;
    REQUIRE(snapshot.AddOrRemoveEntry(MakeEntry(4)));

    Inventory fromSnapshot = snapshot;
    fromSnapshot = snapshot;
    REQUIRE(fromSnapshot.Entries.size() == 1);
    REQUIRE(fromSnapshot.Entries[0].Count == 4);

    REQUIRE(fromSnapshot.AddOrRemoveEntry(MakeEntry(2)));
    REQUIRE(fromSnapshot.AddOrRemoveEntry(MakeEntry(2)));
    REQUIRE(fromSnapshot.Entries[0].Count == 8);
}
