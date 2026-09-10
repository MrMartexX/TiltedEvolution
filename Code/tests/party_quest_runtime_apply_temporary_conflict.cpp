#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x191A1B1C1D1E1F20ull,
    0x292A2B2C2D2E2F30ull};
const PartyQuestPlayerProfileId kOldPlayer{
    0x393A3B3C3D3E3F40ull,
    0x494A4B4C4D4E4F50ull};
const PartyQuestPlayerProfileId kNewPlayer{
    0x595A5B5C5D5E5F60ull,
    0x696A6B6C6D6E6F70ull};

struct Sandbox
{
    std::filesystem::path Root;
    std::filesystem::path Journal;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_apply_tmp_conflict_" +
             std::to_string(nonce));
        Journal = Root / "runtime-apply.bin";
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~Sandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestRuntimeRecoveryState BuildState(
    const PartyQuestPlayerProfileId& acPlayer)
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = acPlayer;
    return state;
}

void WriteBytes(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(
        reinterpret_cast<const char*>(acBytes.data()),
        static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}
} // namespace

TEST_CASE(
    "valid runtime apply temporary cannot hide a corrupt present primary",
    "[quest.party-state.runtime-apply.persistence][temporary][conflict]")
{
    Sandbox sandbox;
    const auto oldState = BuildState(kOldPlayer);
    const auto newState = BuildState(kNewPlayer);

    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                sandbox.Journal,
                oldState) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto temporary = sandbox.Journal;
    temporary += ".tmp";
    auto backup = sandbox.Journal;
    backup += ".bak";
    REQUIRE_FALSE(std::filesystem::exists(temporary));
    REQUIRE_FALSE(std::filesystem::exists(backup));

    WriteBytes(
        temporary,
        PartyQuestRuntimeApplyPersistence::Encode(newState));

    auto corruptPrimary = PartyQuestRuntimeApplyPersistence::Encode(oldState);
    REQUIRE(corruptPrimary.size() > 24);
    corruptPrimary[20] ^= 0x5A;
    WriteBytes(sandbox.Journal, corruptPrimary);

    const auto conflict = PartyQuestRuntimeApplyPersistence::Load(sandbox.Journal);
    REQUIRE(conflict.Status ==
        PartyQuestRuntimeApplyPersistenceStatus::ChecksumMismatch);
    REQUIRE_FALSE(conflict.State.has_value());
    REQUIRE_FALSE(conflict.UsedTemporary);
    REQUIRE_FALSE(conflict.UsedBackup);
    REQUIRE(std::filesystem::exists(temporary));

    // The exact same complete temporary is legitimate once the primary is
    // physically absent, which is the interrupted primary->backup rotation
    // window that temporary recovery is designed to cover.
    std::error_code ec;
    REQUIRE(std::filesystem::remove(sandbox.Journal, ec));
    REQUIRE_FALSE(ec);

    const auto missingPrimary =
        PartyQuestRuntimeApplyPersistence::Load(sandbox.Journal);
    REQUIRE(missingPrimary.Status ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(missingPrimary.State.has_value());
    REQUIRE(*missingPrimary.State == newState);
    REQUIRE(missingPrimary.UsedTemporary);
    REQUIRE_FALSE(missingPrimary.UsedBackup);
}
