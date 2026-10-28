#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>
#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>
#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>
#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x123456789ABCDEF0ull,
    0x0FEDCBA987654321ull};
const PartyQuestPlayerProfileId kProfile{
    0x1122334455667788ull,
    0x8877665544332211ull};

struct MetadataSandbox
{
    std::filesystem::path Root;

    MetadataSandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_metadata_power_loss_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~MetadataSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

std::filesystem::path WithSuffix(std::filesystem::path aPath, const char* apSuffix)
{
    aPath += apSuffix;
    return aPath;
}

QuestSnapshot BuildSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.CompletedStages = {10, aStage};
    snapshot.Objectives = {{aStage, QuestObjectiveState::Displayed}};
    snapshot.Canonicalize();
    return snapshot;
}

PartyQuestTransaction BuildTransaction(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = 7;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildSnapshot(aQuestId, aStage);
    return transaction;
}

PartyQuestState BuildState(bool aNewer)
{
    PartyQuestState state;
    const GameId questId(3, 0x1234);
    REQUIRE(state.Apply(BuildTransaction(50001, questId, 0, 10)).Status ==
        PartyQuestApplyStatus::Accepted);
    if (aNewer)
    {
        REQUIRE(state.Apply(BuildTransaction(50002, questId, 1, 20)).Status ==
            PartyQuestApplyStatus::Accepted);
    }
    return state;
}

struct CampaignFault
{
    PartyQuestCampaignPersistenceBoundary Boundary;
};

PartyQuestCampaignPersistenceDirective FailCampaignAt(
    PartyQuestCampaignPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const CampaignFault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestCampaignPersistenceDirective::FailClosed
        : PartyQuestCampaignPersistenceDirective::Continue;
}

struct ProfileFault
{
    PartyQuestPlayerProfilePersistenceBoundary Boundary;
};

PartyQuestPlayerProfilePersistenceDirective FailProfileAt(
    PartyQuestPlayerProfilePersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const ProfileFault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestPlayerProfilePersistenceDirective::FailClosed
        : PartyQuestPlayerProfilePersistenceDirective::Continue;
}

struct StateFault
{
    PartyQuestStatePersistenceBoundary Boundary;
};

PartyQuestStatePersistenceDirective FailStateAt(
    PartyQuestStatePersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto* fault = static_cast<const StateFault*>(apContext);
    return fault && fault->Boundary == aBoundary
        ? PartyQuestStatePersistenceDirective::FailClosed
        : PartyQuestStatePersistenceDirective::Continue;
}
}

TEST_CASE(
    "durable campaign metadata keeps exact primary and bootstrap backup",
    "[quest.party-state.campaign][durability][publication]")
{
    MetadataSandbox sandbox;
    const auto path = sandbox.Root / "campaign.bin";

    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(path, kCampaign) ==
        PartyQuestCampaignPersistenceStatus::Success);
    const auto primary = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(primary.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(primary.CampaignId == kCampaign);
    REQUIRE(primary.CanonicalArchiveRequired);
    REQUIRE_FALSE(primary.BackupRefreshRequired);
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));

    CampaignFault fault{PartyQuestCampaignPersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(
                path,
                kCampaign,
                {FailCampaignAt, &fault}) ==
        PartyQuestCampaignPersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".tmp")));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));

    const auto recovered = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(recovered.CampaignId == kCampaign);
    REQUIRE(recovered.UsedTemporary);

    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(path, kCampaign) ==
        PartyQuestCampaignPersistenceStatus::Success);
    const auto durable = PartyQuestCampaignPersistence::Load(path);
    REQUIRE(durable.Status == PartyQuestCampaignPersistenceStatus::Success);
    REQUIRE(durable.CampaignId == kCampaign);
    REQUIRE_FALSE(durable.UsedTemporary);
    REQUIRE_FALSE(durable.BackupRefreshRequired);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable player profile rotation keeps immutable identity recoverable",
    "[quest.party-state.player-profile][durability][fault]")
{
    MetadataSandbox sandbox;
    const auto path = sandbox.Root / "player-profile.bin";

    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(path, kProfile) ==
        PartyQuestPlayerProfilePersistenceStatus::Success);

    ProfileFault fault{
        PartyQuestPlayerProfilePersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                path,
                kProfile,
                {FailProfileAt, &fault}) ==
        PartyQuestPlayerProfilePersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".tmp")));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));

    // Profile identity is immutable, so the exact older backup is safe fallback.
    const auto recovered = PartyQuestPlayerProfilePersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(recovered.ProfileId == kProfile);
    REQUIRE(recovered.UsedBackup);

    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(path, kProfile) ==
        PartyQuestPlayerProfilePersistenceStatus::Success);
    const auto durable = PartyQuestPlayerProfilePersistence::Load(path);
    REQUIRE(durable.Status == PartyQuestPlayerProfilePersistenceStatus::Success);
    REQUIRE(durable.ProfileId == kProfile);
    REQUIRE_FALSE(durable.UsedBackup);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "durable canonical state rotation exposes the newer verified temporary after cut",
    "[quest.party-state.persistence][durability][fault]")
{
    MetadataSandbox sandbox;
    const auto path = sandbox.Root / "canonical.bin";
    const auto oldState = BuildState(false);
    const auto newState = BuildState(true);

    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                path,
                kCampaign,
                oldState) == PartyQuestPersistenceStatus::Success);

    StateFault fault{PartyQuestStatePersistenceBoundary::PrimaryMovedToBackup};
    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                path,
                kCampaign,
                newState,
                {FailStateAt, &fault}) == PartyQuestPersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".tmp")));
    REQUIRE(std::filesystem::exists(WithSuffix(path, ".bak")));

    const auto recovered = PartyQuestStatePersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(recovered.CampaignId == kCampaign);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.State->GetWorldRevision() == 2);
    REQUIRE(recovered.UsedTemporary);
    REQUIRE_FALSE(recovered.UsedBackup);

    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                path,
                kCampaign,
                newState) == PartyQuestPersistenceStatus::Success);
    const auto durable = PartyQuestStatePersistence::Load(path);
    REQUIRE(durable.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(durable.CampaignId == kCampaign);
    REQUIRE(durable.State.has_value());
    REQUIRE(durable.State->GetWorldRevision() == 2);
    REQUIRE_FALSE(durable.UsedTemporary);

    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}

TEST_CASE(
    "strong metadata writers refuse to invent durable parent-directory creation",
    "[quest.party-state.durability][boundary]")
{
    MetadataSandbox sandbox;
    const auto missing = sandbox.Root / "missing";
    const auto state = BuildState(false);

    REQUIRE(PartyQuestCampaignPersistence::SavePowerLossDurably(
                missing / "campaign.bin",
                kCampaign) == PartyQuestCampaignPersistenceStatus::IoError);
    REQUIRE(PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
                missing / "profile.bin",
                kProfile) == PartyQuestPlayerProfilePersistenceStatus::IoError);
    REQUIRE(PartyQuestStatePersistence::SavePowerLossDurably(
                missing / "state.bin",
                kCampaign,
                state) == PartyQuestPersistenceStatus::IoError);
    REQUIRE_FALSE(std::filesystem::exists(missing));

    REQUIRE(PartyQuestPersistenceDurabilityPolicy::CurrentLocalGuarantee ==
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE_FALSE(PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation());
}
