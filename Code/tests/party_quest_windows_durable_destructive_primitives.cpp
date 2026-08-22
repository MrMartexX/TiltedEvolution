#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct DestructiveStorageSandbox
{
    std::filesystem::path Root;

    DestructiveStorageSandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_durable_destructive_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~DestructiveStorageSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

std::vector<uint8_t> ReadAll(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    REQUIRE(file.is_open());
    const auto end = file.tellg();
    REQUIRE(end >= 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
    return bytes;
}
} // namespace

TEST_CASE(
    "stable storage durably copies and removes exact confined files",
    "[quest.party-state.durability][copy][remove]")
{
    DestructiveStorageSandbox sandbox;
    const auto workspace = sandbox.Root / "workspace";
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(workspace) ==
        PartyQuestStableStorageStatus::Success);

    const std::vector<uint8_t> payload{
        0x53, 0x6B, 0x79, 0x72, 0x69, 0x6D, 0x2D, 0x50,
        0x72, 0x65, 0x52, 0x65, 0x70, 0x61, 0x69, 0x72};
    const auto source = workspace / "source.bin";
    const auto destination = workspace / "destination.bin";

    REQUIRE(PartyQuestStableStorage::WriteFileDurably(
                source,
                payload.data(),
                payload.size()) == PartyQuestStableStorageStatus::Success);
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(workspace) ==
        PartyQuestStableStorageStatus::Success);

    REQUIRE(PartyQuestStableStorage::CopyFileDurably(source, destination) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(ReadAll(source) == payload);
    REQUIRE(ReadAll(destination) == payload);

    REQUIRE(PartyQuestStableStorage::RemoveFileDurably(destination) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(destination));
    REQUIRE(ReadAll(source) == payload);

    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableFileCopyPrimitive());
    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableFileRemovalPrimitive());

    const auto empty = workspace / "empty";
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(empty) ==
        PartyQuestStableStorageStatus::Success);
#ifdef _WIN32
    REQUIRE_FALSE(PartyQuestStableStorage::HasDocumentedDurableEmptyDirectoryRemovalPrimitive());
    REQUIRE(PartyQuestStableStorage::RemoveEmptyDirectoryDurably(empty) ==
        PartyQuestStableStorageStatus::Unsupported);
    REQUIRE(std::filesystem::is_directory(empty));
#else
    REQUIRE(PartyQuestStableStorage::HasDocumentedDurableEmptyDirectoryRemovalPrimitive());
    REQUIRE(PartyQuestStableStorage::RemoveEmptyDirectoryDurably(empty) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(empty));

    const auto nonEmpty = workspace / "non-empty";
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nonEmpty) ==
        PartyQuestStableStorageStatus::Success);
    const auto child = nonEmpty / "child.bin";
    REQUIRE(PartyQuestStableStorage::WriteFileDurably(
                child,
                payload.data(),
                payload.size()) == PartyQuestStableStorageStatus::Success);
    REQUIRE(PartyQuestStableStorage::EnsureDirectoryTreeDurably(nonEmpty) ==
        PartyQuestStableStorageStatus::Success);

    REQUIRE(PartyQuestStableStorage::RemoveEmptyDirectoryDurably(nonEmpty) ==
        PartyQuestStableStorageStatus::RemoveFailed);
    REQUIRE(std::filesystem::is_directory(nonEmpty));
    REQUIRE(ReadAll(child) == payload);

    REQUIRE(PartyQuestStableStorage::RemoveFileDurably(child) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE(PartyQuestStableStorage::RemoveEmptyDirectoryDurably(nonEmpty) ==
        PartyQuestStableStorageStatus::Success);
    REQUIRE_FALSE(std::filesystem::exists(nonEmpty));
#endif

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
