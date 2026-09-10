#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

TEST_CASE(
    "durable directory tree promotes an existing anchor and creates descendants",
    "[quest.party-state.durability][directory-tree]")
{
    const auto nonce = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("tp_party_quest_durable_tree_" + std::to_string(nonce));
    const auto existing = root / "existing";
    const auto first = existing / "created-a";
    const auto nested = first / "created-b";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::create_directories(existing, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::is_directory(existing));
    REQUIRE_FALSE(std::filesystem::exists(first));

    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableDirectoryTreePrimitive());
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nested) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(std::filesystem::is_directory(existing));
    REQUIRE(std::filesystem::is_directory(first));
    REQUIRE(std::filesystem::is_directory(nested));

    // A pre-existing tree must be promotable too; this is the startup case where
    // an earlier process created the namespace before the stronger contract was
    // available. Success must represent a fresh stable-storage barrier, not a
    // shortcut based only on the directory already being present.
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nested) ==
        PartyQuestStableStorageStatus::Success);

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(
        PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());

    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(ec);
}
