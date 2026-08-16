#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>

#include <catch2/catch.hpp>

TEST_CASE("PoC durability policy explicitly stops below power-loss durability", "[quest.party-state.durability]")
{
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::Meets(
        PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee,
        PartyQuestPersistenceDurabilityPolicy::MinimumPoCRuntimeMutationGuarantee));
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::Meets(
        PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee,
        PartyQuestPersistenceGuarantee::PowerLossDurable));
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::Meets(
        PartyQuestPersistenceGuarantee::Volatile,
        PartyQuestPersistenceDurabilityPolicy::MinimumPoCRuntimeMutationGuarantee));

    // PoC crash-resilient ordering is intentionally insufficient authority for
    // any native Skyrim side effect. The native executor uses this exact gate.
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
