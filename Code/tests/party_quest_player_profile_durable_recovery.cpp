#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestPlayerProfileId kProfile{
    0xCAFEBABE11223344ull,
    0x5566778899AABBCCull};

struct ProfileRecoverySandbox
{
    std::filesystem::path Root;
    std::filesystem::path Path;

    ProfileRecoverySandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_profile_durable_recovery_" + std::to_string(nonce));
        Path = Root / "profile.bin";
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~ProfileRecoverySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

struct Fault
{
    PartyQuestPlayerProfilePersistenceBoundary Boundary;
};

PartyQuestPlayerProfilePersistenceDirective FailAt(
    PartyQuestPlayerProfilePersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const Fault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestPlayerProfilePersistenceDirective::FailClosed
        : PartyQuestPlayerProfilePersistenceDirective::Continue;
}

std::filesystem::path Temporary(std::filesystem::path aPath)
{
    aPath += ".tmp";
    return aPath;
}
}

TEST_CASE(
    "durable player profile loader preserves first-publication identity from verified temporary",
    "[quest.party-state.player-profile][durability][recovery]")
{
    ProfileRecoverySandbox sandbox;
    Fault fault{PartyQuestPlayerProfilePersistenceBoundary::TemporaryVerified};

    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                sandbox.Path,
                kProfile,
                {FailAt, &fault}) ==
        PartyQuestPlayerProfilePersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(sandbox.Path));
    REQUIRE(std::filesystem::exists(Temporary(sandbox.Path)));

    // Legacy loading intentionally does not adopt the new strong temporary.
    const auto legacy = PartyQuestPlayerProfilePersistence::Load(sandbox.Path);
    REQUIRE(legacy.Status == PartyQuestPlayerProfilePersistenceStatus::FileNotFound);

    const auto recovered =
        PartyQuestPlayerProfilePersistence::LoadPowerLossDurably(sandbox.Path);
    REQUIRE(recovered.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(recovered.ProfileId == kProfile);
    REQUIRE(recovered.UsedTemporary);
    REQUIRE_FALSE(recovered.UsedBackup);
    REQUIRE(std::filesystem::exists(Temporary(sandbox.Path)));
    REQUIRE_FALSE(std::filesystem::exists(sandbox.Path));

    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                sandbox.Path,
                *recovered.ProfileId) ==
        PartyQuestPlayerProfilePersistenceStatus::Success);
    const auto durable =
        PartyQuestPlayerProfilePersistence::LoadPowerLossDurably(sandbox.Path);
    REQUIRE(durable.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(durable.ProfileId == kProfile);
    REQUIRE_FALSE(durable.UsedTemporary);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
