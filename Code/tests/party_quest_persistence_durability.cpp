#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::string ReadText(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}
}

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

TEST_CASE("stable-storage primitives keep the platform publication boundary explicit", "[quest.party-state.durability]")
{
    REQUIRE(PartyQuestStableStorage::HasDocumentedFileFlushPrimitive());
    REQUIRE(PartyQuestStableStorage::HasDocumentedAtomicFilePublicationPrimitive());
#ifdef _WIN32
    REQUIRE_FALSE(PartyQuestStableStorage::HasDocumentedParentDirectoryFlushPrimitive());
    REQUIRE_FALSE(PartyQuestStableStorage::HasDocumentedDurableFileRemovalPrimitive());
#else
    REQUIRE(PartyQuestStableStorage::HasDocumentedParentDirectoryFlushPrimitive());
    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableFileRemovalPrimitive());
#endif

    REQUIRE(PartyQuestStableStorage::FlushFile({}) ==
        PartyQuestStableStorageStatus::InvalidPath);
    REQUIRE(PartyQuestStableStorage::FlushDirectory({}) ==
        PartyQuestStableStorageStatus::InvalidPath);
    REQUIRE(PartyQuestStableStorage::FlushParentDirectory({}) ==
        PartyQuestStableStorageStatus::InvalidPath);
    REQUIRE(PartyQuestStableStorage::PublishFileRename({}, {}) ==
        PartyQuestStableStorageStatus::InvalidPath);
    REQUIRE(PartyQuestStableStorage::RemoveFileDurably({}) ==
        PartyQuestStableStorageStatus::InvalidPath);

    // Adding OS primitives is evidence only; it must not silently upgrade the
    // global mutation authority boundary.
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE("stable-storage file flush works without claiming cross-platform directory durability", "[quest.party-state.durability]")
{
    const auto nonce = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("tilted_party_quest_stable_storage_" + std::to_string(nonce));
    const auto path = root / "barrier.bin";

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    REQUIRE_FALSE(ec);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write("party-quest", 11);
        file.flush();
        REQUIRE(file.good());
        file.close();
        REQUIRE(file.good());
    }

    REQUIRE(PartyQuestStableStorage::FlushFile(path) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(PartyQuestStableStorage::FlushFile(root) !=
        PartyQuestStableStorageStatus::Success);
#ifdef _WIN32
    REQUIRE(PartyQuestStableStorage::FlushParentDirectory(path) ==
        PartyQuestStableStorageStatus::Unsupported);
#else
    REQUIRE(PartyQuestStableStorage::FlushParentDirectory(path) ==
        PartyQuestStableStorageStatus::Success);

    const auto link = root / "barrier-link.bin";
    std::filesystem::create_symlink(path, link, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestStableStorage::FlushFile(link) !=
        PartyQuestStableStorageStatus::Success);
#endif

    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(ec);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE("stable rename publication is same-directory and rejects accidental overwrite", "[quest.party-state.durability][publication]")
{
    const auto nonce = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("tilted_party_quest_publish_" + std::to_string(nonce));
    const auto source = root / "state.tmp";
    const auto destination = root / "state.bin";
    const auto otherRoot = root / "other";
    const auto crossDirectoryDestination = otherRoot / "state.bin";

    std::error_code ec;
    std::filesystem::create_directories(otherRoot, ec);
    REQUIRE_FALSE(ec);

    {
        std::ofstream file(source, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write("durable-state", 13);
        file.flush();
        REQUIRE(file.good());
    }

    REQUIRE(PartyQuestStableStorage::PublishFileRename(source, source) ==
        PartyQuestStableStorageStatus::CrossDirectoryRename);
    REQUIRE(std::filesystem::exists(source));

    REQUIRE(PartyQuestStableStorage::PublishFileRename(
        source,
        crossDirectoryDestination) ==
        PartyQuestStableStorageStatus::CrossDirectoryRename);
    REQUIRE(std::filesystem::exists(source));
    REQUIRE_FALSE(std::filesystem::exists(crossDirectoryDestination));

    {
        std::ofstream existing(destination, std::ios::binary | std::ios::trunc);
        REQUIRE(existing.is_open());
        existing.write("old", 3);
    }

    REQUIRE(PartyQuestStableStorage::PublishFileRename(source, destination) ==
        PartyQuestStableStorageStatus::RenameFailed);
    REQUIRE(std::filesystem::exists(source));
    REQUIRE(ReadText(destination) == "old");

    REQUIRE(PartyQuestStableStorage::PublishFileRename(source, destination, true) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(source));
    REQUIRE(std::filesystem::exists(destination));
    REQUIRE(ReadText(destination) == "durable-state");
    REQUIRE(PartyQuestStableStorage::FlushFile(destination) ==
        PartyQuestStableStorageStatus::Success);

    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(ec);

    // One proven publication primitive is not an end-to-end persistence proof.
    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE("durable file removal is explicit and never inferred on Windows", "[quest.party-state.durability][publication]")
{
    const auto nonce = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("tilted_party_quest_remove_" + std::to_string(nonce));
    const auto path = root / "obsolete.bin";

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    REQUIRE_FALSE(ec);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());
        file.write("obsolete", 8);
    }

#ifdef _WIN32
    REQUIRE(PartyQuestStableStorage::RemoveFileDurably(path) ==
        PartyQuestStableStorageStatus::Unsupported);
    REQUIRE(std::filesystem::exists(path));
#else
    REQUIRE(PartyQuestStableStorage::RemoveFileDurably(path) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(path));
#endif

    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(ec);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
