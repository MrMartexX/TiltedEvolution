#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>
#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>
#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaignId{
    0x1111222233334444ull,
    0xAAAABBBBCCCCDDDDull};
const PartyQuestPlayerProfileId kPlayerProfileId{
    0x1011121314151617ull,
    0x2122232425262728ull};

std::filesystem::path BuildComponentBoundedPath(size_t aBytes)
{
    std::filesystem::path path;
    while (path.generic_u8string().size() < aBytes)
    {
        const size_t currentBytes = path.generic_u8string().size();
        const size_t separatorBytes = path.empty() ? 0 : 1;
        const size_t componentBytes = std::min<size_t>(
            120,
            aBytes - currentBytes - separatorBytes);
        REQUIRE(componentBytes > 0);
        path /= std::string(componentBytes, 'd');
    }
    REQUIRE(path.generic_u8string().size() == aBytes);
    return path;
}

PartyQuestRuntimeRecoveryState BuildRuntimeState()
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaignId;
    state.PlayerProfileId = kPlayerProfileId;
    return state;
}
} // namespace

TEST_CASE(
    "Durable persistence reserves crash-safe sibling path headroom before filesystem work",
    "[quest.party-state.persistence][resource-budget][path-budget]")
{
    const auto maximumMutable = BuildComponentBoundedPath(
        PartyQuestDurableResourcePolicy::MaxMutableFilesystemPathBytes);
    REQUIRE(PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(
        maximumMutable));

    auto longestCurrentSibling = maximumMutable;
    longestCurrentSibling += ".bak.tmp";
    REQUIRE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(
        longestCurrentSibling));

    const auto overBudgetMutable = BuildComponentBoundedPath(
        PartyQuestDurableResourcePolicy::MaxMutableFilesystemPathBytes + 1);
    REQUIRE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(
        overBudgetMutable));
    REQUIRE_FALSE(PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(
        overBudgetMutable));

    PartyQuestState canonicalState;
    const auto runtimeState = BuildRuntimeState();
    PartyQuestReplicaRestoreJournalState restoreState;

    REQUIRE(PartyQuestCampaignPersistence::SaveAtomically(
                overBudgetMutable,
                kCampaignId) ==
        PartyQuestCampaignPersistenceStatus::InvalidData);
    REQUIRE(PartyQuestPlayerProfilePersistence::SaveAtomically(
                overBudgetMutable,
                kPlayerProfileId) ==
        PartyQuestPlayerProfilePersistenceStatus::InvalidData);
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(
                overBudgetMutable,
                kCampaignId,
                canonicalState) ==
        PartyQuestPersistenceStatus::InvalidData);
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                overBudgetMutable,
                runtimeState) ==
        PartyQuestRuntimeApplyPersistenceStatus::ResourceLimitExceeded);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
                overBudgetMutable,
                restoreState) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded);
}

TEST_CASE(
    "Durable persistence load never probes an over-budget crash sibling",
    "[quest.party-state.persistence][resource-budget][path-budget]")
{
    const auto baseWithoutSiblingSpace = BuildComponentBoundedPath(
        PartyQuestDurableResourcePolicy::MaxFilesystemPathBytes);
    REQUIRE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(
        baseWithoutSiblingSpace));

    auto temporary = baseWithoutSiblingSpace;
    temporary += ".tmp";
    auto backup = baseWithoutSiblingSpace;
    backup += ".bak";
    REQUIRE_FALSE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(temporary));
    REQUIRE_FALSE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(backup));

    REQUIRE(PartyQuestCampaignPersistence::Load(baseWithoutSiblingSpace).Status ==
        PartyQuestCampaignPersistenceStatus::InvalidData);
    REQUIRE(PartyQuestPlayerProfilePersistence::Load(baseWithoutSiblingSpace).Status ==
        PartyQuestPlayerProfilePersistenceStatus::InvalidData);
    REQUIRE(PartyQuestStatePersistence::Load(baseWithoutSiblingSpace).Status ==
        PartyQuestPersistenceStatus::InvalidData);
    REQUIRE(PartyQuestRuntimeApplyPersistence::Load(baseWithoutSiblingSpace).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::ResourceLimitExceeded);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::Load(baseWithoutSiblingSpace).Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded);
}

TEST_CASE(
    "Durable persistence reads fail closed on platform component-limit errors",
    "[quest.party-state.persistence][resource-budget][path-budget][platform]")
{
    const std::filesystem::path overlongComponent{
        std::string(300, 'c')};
    REQUIRE(PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(
        overlongComponent));

    const auto campaignStatus =
        PartyQuestCampaignPersistence::Load(overlongComponent).Status;
    REQUIRE((
        campaignStatus == PartyQuestCampaignPersistenceStatus::FileNotFound ||
        campaignStatus == PartyQuestCampaignPersistenceStatus::IoError));

    const auto profileStatus =
        PartyQuestPlayerProfilePersistence::Load(overlongComponent).Status;
    REQUIRE((
        profileStatus == PartyQuestPlayerProfilePersistenceStatus::FileNotFound ||
        profileStatus == PartyQuestPlayerProfilePersistenceStatus::IoError));

    const auto stateStatus =
        PartyQuestStatePersistence::Load(overlongComponent).Status;
    REQUIRE((
        stateStatus == PartyQuestPersistenceStatus::FileNotFound ||
        stateStatus == PartyQuestPersistenceStatus::IoError));

    const auto runtimeStatus =
        PartyQuestRuntimeApplyPersistence::Load(overlongComponent).Status;
    REQUIRE((
        runtimeStatus == PartyQuestRuntimeApplyPersistenceStatus::FileNotFound ||
        runtimeStatus == PartyQuestRuntimeApplyPersistenceStatus::IoError));

    const auto restoreStatus =
        PartyQuestReplicaRestoreJournalPersistence::Load(overlongComponent).Status;
    REQUIRE((
        restoreStatus == PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound ||
        restoreStatus == PartyQuestReplicaRestoreJournalPersistenceStatus::IoError));
}
