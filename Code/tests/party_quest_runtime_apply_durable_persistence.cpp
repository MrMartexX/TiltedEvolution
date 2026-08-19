#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaignId{
    0x1111222233334444ull,
    0xAAAABBBBCCCCDDDDull};
const PartyQuestPlayerProfileId kOldProfileId{
    0x1011121314151617ull,
    0x2122232425262728ull};
const PartyQuestPlayerProfileId kNewProfileId{
    0x1011121314151617ull,
    0x3132333435363738ull};
const PartyQuestPlayerProfileId kNewestProfileId{
    0x1011121314151617ull,
    0x4142434445464748ull};

PartyQuestRuntimeRecoveryState BuildState(
    const PartyQuestPlayerProfileId& acProfileId)
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaignId;
    state.PlayerProfileId = acProfileId;
    return state;
}

std::filesystem::path WithSuffix(
    std::filesystem::path aPath,
    const char* apSuffix)
{
    aPath += apSuffix;
    return aPath;
}

struct DurablePersistenceSandbox
{
    std::filesystem::path Root;
    std::filesystem::path Journal;

    DurablePersistenceSandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_apply_durable_" + std::to_string(nonce));
        Journal = Root / "runtime-apply.bin";

        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~DurablePersistenceSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

struct FailBoundary
{
    PartyQuestRuntimeApplyPersistenceBoundary Boundary;
};

PartyQuestRuntimeApplyPersistenceDirective FailAtBoundary(
    PartyQuestRuntimeApplyPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* pFail = static_cast<const FailBoundary*>(apContext);
    return pFail && pFail->Boundary == aBoundary
        ? PartyQuestRuntimeApplyPersistenceDirective::FailClosed
        : PartyQuestRuntimeApplyPersistenceDirective::Continue;
}

void RequireLoadedState(
    const std::filesystem::path& acPath,
    const PartyQuestRuntimeRecoveryState& acExpected)
{
    const auto loaded = PartyQuestRuntimeApplyPersistence::Load(acPath);
    REQUIRE(loaded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());
    REQUIRE(*loaded.State == acExpected);
}
}

TEST_CASE(
    "power-loss durable runtime apply writer publishes and repeatedly rotates exact recovery authority",
    "[quest.party-state.runtime-apply.persistence][durability][publication]")
{
    DurablePersistenceSandbox sandbox;
    const auto oldState = BuildState(kOldProfileId);
    const auto newState = BuildState(kNewProfileId);
    const auto newestState = BuildState(kNewestProfileId);

    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                oldState) == PartyQuestRuntimeApplyPersistenceStatus::Success);
    RequireLoadedState(sandbox.Journal, oldState);
    REQUIRE_FALSE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".tmp")));
    REQUIRE_FALSE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".bak")));

    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newState) == PartyQuestRuntimeApplyPersistenceStatus::Success);
    RequireLoadedState(sandbox.Journal, newState);
    RequireLoadedState(WithSuffix(sandbox.Journal, ".bak"), oldState);
    REQUIRE_FALSE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".tmp")));

    // A normal third update exercises replace-existing publication for .bak.
    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newestState) == PartyQuestRuntimeApplyPersistenceStatus::Success);
    RequireLoadedState(sandbox.Journal, newestState);
    RequireLoadedState(WithSuffix(sandbox.Journal, ".bak"), newState);
    REQUIRE_FALSE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".tmp")));

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable runtime apply rotation cut preserves old backup and new staged authority",
    "[quest.party-state.runtime-apply.persistence][durability][fault]")
{
    DurablePersistenceSandbox sandbox;
    const auto oldState = BuildState(kOldProfileId);
    const auto newState = BuildState(kNewProfileId);

    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                oldState) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    FailBoundary fail{PartyQuestRuntimeApplyPersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newState,
                {FailAtBoundary, &fail}) == PartyQuestRuntimeApplyPersistenceStatus::IoError);

    const auto backup = WithSuffix(sandbox.Journal, ".bak");
    const auto temporary = WithSuffix(sandbox.Journal, ".tmp");
    REQUIRE_FALSE(std::filesystem::exists(sandbox.Journal));
    REQUIRE(std::filesystem::exists(backup));
    REQUIRE(std::filesystem::exists(temporary));
    RequireLoadedState(backup, oldState);
    RequireLoadedState(temporary, newState);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable runtime apply publication cut exposes the new primary without pretending success",
    "[quest.party-state.runtime-apply.persistence][durability][fault]")
{
    DurablePersistenceSandbox sandbox;
    const auto oldState = BuildState(kOldProfileId);
    const auto newState = BuildState(kNewProfileId);

    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                oldState) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    FailBoundary fail{PartyQuestRuntimeApplyPersistenceBoundary::TemporaryPublished};
    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                sandbox.Journal,
                newState,
                {FailAtBoundary, &fail}) == PartyQuestRuntimeApplyPersistenceStatus::IoError);

    REQUIRE(std::filesystem::exists(sandbox.Journal));
    REQUIRE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".bak")));
    REQUIRE_FALSE(std::filesystem::exists(WithSuffix(sandbox.Journal, ".tmp")));
    RequireLoadedState(sandbox.Journal, newState);
    RequireLoadedState(WithSuffix(sandbox.Journal, ".bak"), oldState);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "power-loss durable runtime apply writer refuses to invent durable directory creation",
    "[quest.party-state.runtime-apply.persistence][durability][boundary]")
{
    const auto nonce = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
        ("tp_party_quest_runtime_apply_missing_parent_" + std::to_string(nonce));
    const auto path = root / "missing" / "runtime-apply.bin";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    REQUIRE_FALSE(std::filesystem::exists(path.parent_path()));

    REQUIRE(PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
                path,
                BuildState(kOldProfileId)) == PartyQuestRuntimeApplyPersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(path.parent_path()));
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());

    std::filesystem::remove_all(root, ec);
}
