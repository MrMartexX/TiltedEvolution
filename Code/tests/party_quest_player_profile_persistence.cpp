#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
void RemoveProfileFiles(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);
    auto backup = acPath;
    backup += ".bak";
    std::filesystem::remove(backup, ec);
    auto temporary = acPath;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
}
} // namespace

TEST_CASE("Player profile identity encoding is deterministic and round-trips", "[quest.party-state.player-profile]")
{
    const PartyQuestPlayerProfileId profile{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull};
    const auto first = PartyQuestPlayerProfilePersistence::Encode(profile);
    const auto second = PartyQuestPlayerProfilePersistence::Encode(profile);

    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);

    const auto decoded = PartyQuestPlayerProfilePersistence::Decode(first);
    REQUIRE(decoded.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(decoded.ProfileId.has_value());
    REQUIRE(*decoded.ProfileId == profile);
    REQUIRE_FALSE(decoded.UsedBackup);
}

TEST_CASE("Player profile identity rejects zero corruption truncation and unsupported versions", "[quest.party-state.player-profile]")
{
    REQUIRE(PartyQuestPlayerProfilePersistence::Encode({}).empty());

    const auto encoded = PartyQuestPlayerProfilePersistence::Encode({0x11, 0x22});
    REQUIRE(encoded.size() > 20);

    auto corrupted = encoded;
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestPlayerProfilePersistence::Decode(corrupted).Status ==
        PartyQuestPlayerProfilePersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestPlayerProfilePersistence::Decode(truncated).Status ==
        PartyQuestPlayerProfilePersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestPlayerProfilePersistence::Decode(unsupported).Status ==
        PartyQuestPlayerProfilePersistenceStatus::UnsupportedVersion);

    std::vector<uint8_t> oversized(
        PartyQuestDurableResourcePolicy::MaxIdentityArchiveBytes + 1);
    REQUIRE(PartyQuestPlayerProfilePersistence::Decode(oversized).Status ==
        PartyQuestPlayerProfilePersistenceStatus::InvalidData);
}

TEST_CASE("Generated player profile identities are valid and distinct in-process", "[quest.party-state.player-profile]")
{
    const auto first = PartyQuestPlayerProfilePersistence::GenerateProfileId();
    const auto second = PartyQuestPlayerProfilePersistence::GenerateProfileId();

    REQUIRE(first.IsValid());
    REQUIRE(second.IsValid());
    REQUIRE(first != second);
}

TEST_CASE("Player profile identity atomically saves and recovers immutable metadata backup", "[quest.party-state.player-profile]")
{
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_player_profile_" + std::to_string(suffix) + ".bin");
    RemoveProfileFiles(path);

    const PartyQuestPlayerProfileId profile{0xAABBCCDDEEFF0011ull, 0x2233445566778899ull};
    REQUIRE(PartyQuestPlayerProfilePersistence::SaveAtomically(path, profile) ==
        PartyQuestPlayerProfilePersistenceStatus::Success);

    auto load = PartyQuestPlayerProfilePersistence::Load(path);
    REQUIRE(load.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(load.ProfileId.has_value());
    REQUIRE(*load.ProfileId == profile);
    REQUIRE_FALSE(load.UsedBackup);

    // Rewriting immutable metadata with the same value creates an equivalent backup.
    REQUIRE(PartyQuestPlayerProfilePersistence::SaveAtomically(path, profile) ==
        PartyQuestPlayerProfilePersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    load = PartyQuestPlayerProfilePersistence::Load(path);
    REQUIRE(load.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(load.ProfileId.has_value());
    REQUIRE(*load.ProfileId == profile);
    REQUIRE(load.UsedBackup);

    RemoveProfileFiles(path);
}
