#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

#include <party_quest_runtime_session_owner_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kProcessBindCampaign{
    0xD101D102D103D104ull,
    0xD105D106D107D108ull};
const PartyQuestPlayerProfileId kProcessBindProfile{
    0xE101E102E103E104ull,
    0xE105E106E107E108ull};
} // namespace

TEST_CASE(
    "Process runtime owner rejects direct bind without bootstrap authority",
    "[quest.party-state.runtime-owner][runtime-bootstrap][authority]")
{
    const auto nonce =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("tp_party_quest_process_bind_authority_" + std::to_string(nonce));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::create_directories(root, ec);
    REQUIRE_FALSE(ec);

    const auto paths = PartyQuestCoopSaveLayout::Build(
        root / "CoopCampaigns",
        kProcessBindCampaign,
        kProcessBindProfile);
    REQUIRE(paths.has_value());

    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    PartyQuestRuntimeSessionOwnerTestAccess::RevokeDirectProcessBindForTesting();
    auto& owner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    REQUIRE_FALSE(owner.IsBound());

    const auto rejected = owner.Bind(
        kProcessBindCampaign,
        kProcessBindProfile,
        *paths);
    REQUIRE(rejected.Status ==
        PartyQuestRuntimeSessionOwnerBindStatus::ProcessBootstrapRequired);
    REQUIRE_FALSE(rejected.IsBound());
    REQUIRE_FALSE(owner.IsBound());
    REQUIRE(rejected.LeaseStatus ==
        PartyQuestReplicaWorkspaceLeaseStatus::NotAttempted);
    REQUIRE_FALSE(PartyQuestSaveGuard::GetProcessGuard().IsActive());
    REQUIRE_FALSE(std::filesystem::exists(paths->RuntimeApplySidecar));

    PartyQuestRuntimeSessionOwnerTestAccess::ForceClearProcessOwner();
    std::filesystem::remove_all(root, ec);
}
