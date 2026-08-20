#include <Structs/Skyrim/PartyQuestCoopSaveLayout.h>
#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{
const PartyQuestCampaignId kAttemptFaultCampaign{
    0xB701B701B701B701ull,
    0xC802C802C802C802ull};
const PartyQuestPlayerProfileId kAttemptFaultPlayer{
    0xD903D903D903D903ull,
    0xEA04EA04EA04EA04ull};

struct AttemptFaultSandbox
{
    std::filesystem::path Root;

    AttemptFaultSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_restore_attempt_fault_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~AttemptFaultSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestRuntimeRestoreAttemptResult EnsureAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId)
{
    PartyQuestReplicaWorkspaceLease lease;
    REQUIRE(lease.Acquire(
                acPaths,
                kAttemptFaultCampaign,
                kAttemptFaultPlayer) ==
        PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
    const auto capability = lease.CreatePublicationCapability(
        acPaths,
        kAttemptFaultCampaign,
        kAttemptFaultPlayer);
    REQUIRE(capability.Protects(
        acPaths,
        kAttemptFaultCampaign,
        kAttemptFaultPlayer));
    return PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
        acPaths,
        kAttemptFaultCampaign,
        kAttemptFaultPlayer,
        aTransactionId,
        capability);
}

void PersistRolledBackJournal(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId,
    uint64_t aWorldRevision)
{
    PartyQuestReplicaRestoreJournalState state;
    state.CampaignId = kAttemptFaultCampaign;
    state.PlayerProfileId = kAttemptFaultPlayer;
    state.RestoreId = aRestoreId;
    state.CheckpointKind = PartyQuestCheckpointKind::PreRepair;
    state.CampaignWorldRevision = aWorldRevision;
    state.Phase = PartyQuestReplicaRestoreJournalPhase::RolledBack;
    state.TransactionDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            acPaths,
            aRestoreId);

    PartyQuestReplicaRestoreJournalOperation operation;
    operation.Kind = PartyQuestReplicaFileKind::SkyrimSave;
    operation.CheckpointSourcePath =
        acPaths.CheckpointsDirectory / "PreRepair" / "source.ess";
    operation.ReplicaDestinationPath = acPaths.SavesDirectory / "Hero.ess";
    operation.RollbackPath = state.TransactionDirectory / "rollback" / "Hero.ess";
    operation.ExpectedRestoredSize = 10;
    operation.ExpectedRestoredDigest = 0x1111111111111111ull;
    operation.DestinationExisted = true;
    operation.OriginalSize = 11;
    operation.OriginalDigest = 0x2222222222222222ull;
    state.Operations.push_back(std::move(operation));

    std::error_code ec;
    std::filesystem::create_directories(state.TransactionDirectory, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
                PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                    acPaths,
                    aRestoreId),
                state) ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
}
} // namespace

TEST_CASE(
    "Durable allocator advancement burns an unpublished restore id without forking",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][fault-window]")
{
    AttemptFaultSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kAttemptFaultCampaign,
        kAttemptFaultPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 29101;

#ifdef _WIN32
    const auto unsupported = EnsureAttempt(*paths, transactionId);
    REQUIRE(unsupported.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetStatePath(*paths, transactionId)));
#else
    const auto published = EnsureAttempt(*paths, transactionId);
    REQUIRE(published.IsUsable());
    REQUIRE(published.State.has_value());
    REQUIRE(published.State->TransactionId == transactionId);
    REQUIRE(published.State->CurrentOrdinal == 0);
    REQUIRE(published.State->CurrentRestoreId != 0);
    const uint64_t burnedRestoreId = published.State->CurrentRestoreId;
    const auto statePath =
        PartyQuestRuntimeRestoreAttemptStore::GetStatePath(*paths, transactionId);
    REQUIRE(statePath == published.StatePath);
    REQUIRE(std::filesystem::exists(statePath));
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            *paths,
            burnedRestoreId)));

    // Reproduce the exact externally visible state of a crash after the global
    // allocator barrier is durably advanced but before the transaction mapping
    // is published: allocator state survives, while neither mapping nor restore
    // journal exists for the returned id. Deleting the already-published mapping
    // here constructs that state without a production-only fault hook.
    std::error_code ec;
    REQUIRE(std::filesystem::remove(statePath, ec));
    REQUIRE_FALSE(ec);
    auto tempStatePath = statePath;
    tempStatePath += ".tmp";
    REQUIRE_FALSE(std::filesystem::exists(tempStatePath));
    REQUIRE_FALSE(std::filesystem::exists(statePath));
    REQUIRE(PartyQuestRuntimeRestoreAttemptStore::Load(
                *paths,
                kAttemptFaultCampaign,
                kAttemptFaultPlayer,
                transactionId)
                .Status == PartyQuestRuntimeRestoreAttemptStatus::FileNotFound);

    const auto recoveredInitialization = EnsureAttempt(*paths, transactionId);
    REQUIRE(recoveredInitialization.IsUsable());
    REQUIRE(recoveredInitialization.State.has_value());
    REQUIRE(recoveredInitialization.State->TransactionId == transactionId);
    REQUIRE(recoveredInitialization.State->CurrentOrdinal == 0);
    REQUIRE(recoveredInitialization.State->CurrentRestoreId > burnedRestoreId);
    REQUIRE(recoveredInitialization.State->CurrentRestoreId != burnedRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            *paths,
            burnedRestoreId)));
    const uint64_t currentRestoreId =
        recoveredInitialization.State->CurrentRestoreId;

    // A later process/lease epoch with a published mapping but no journal must
    // resume the same exact logical attempt rather than allocating another id.
    const auto restarted = EnsureAttempt(*paths, transactionId);
    REQUIRE(restarted.IsUsable());
    REQUIRE(restarted.State.has_value());
    REQUIRE(restarted.State->CurrentOrdinal == 0);
    REQUIRE(restarted.State->CurrentRestoreId == currentRestoreId);
    REQUIRE(restarted.RestoreId == currentRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(restarted.JournalPath));

    const auto loaded = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kAttemptFaultCampaign,
        kAttemptFaultPlayer,
        transactionId);
    REQUIRE(loaded.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(loaded.State == restarted.State);
#endif
}

TEST_CASE(
    "Rolled-back retry burns an allocated id when the advanced mapping was not published",
    "[quest.party-state.runtime-recovery][durability][restore-attempt][fault-window][retry]")
{
    AttemptFaultSandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kAttemptFaultCampaign,
        kAttemptFaultPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t transactionId = 29102;
    constexpr uint64_t worldRevision = 1910;

#ifdef _WIN32
    const auto unsupported = EnsureAttempt(*paths, transactionId);
    REQUIRE(unsupported.Status ==
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform);
#else
    const auto initial = EnsureAttempt(*paths, transactionId);
    REQUIRE(initial.IsUsable());
    REQUIRE(initial.State.has_value());
    REQUIRE(initial.State->CurrentOrdinal == 0);
    const uint64_t firstRestoreId = initial.State->CurrentRestoreId;
    const auto statePath = initial.StatePath;
    REQUIRE(std::filesystem::exists(statePath));

    auto oldStatePath = statePath;
    oldStatePath += ".before-advance";
    std::error_code ec;
    REQUIRE(std::filesystem::copy_file(
        statePath,
        oldStatePath,
        std::filesystem::copy_options::overwrite_existing,
        ec));
    REQUIRE_FALSE(ec);

    PersistRolledBackJournal(*paths, firstRestoreId, worldRevision);

    uint64_t unpublishedRestoreId{};
    {
        PartyQuestReplicaWorkspaceLease lease;
        REQUIRE(lease.Acquire(
                    *paths,
                    kAttemptFaultCampaign,
                    kAttemptFaultPlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
        const auto capability = lease.CreatePublicationCapability(
            *paths,
            kAttemptFaultCampaign,
            kAttemptFaultPlayer);
        REQUIRE(capability.Protects(
            *paths,
            kAttemptFaultCampaign,
            kAttemptFaultPlayer));

        const auto advanced =
            PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
                *paths,
                kAttemptFaultCampaign,
                kAttemptFaultPlayer,
                transactionId,
                0,
                capability);
        REQUIRE(advanced.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
        REQUIRE(advanced.State.has_value());
        REQUIRE(advanced.State->CurrentOrdinal == 1);
        unpublishedRestoreId = advanced.State->CurrentRestoreId;
        REQUIRE(unpublishedRestoreId != firstRestoreId);
    }

    // Construct the durable state of a crash after allocation committed but
    // before the new mapping publication: allocator is already beyond the new
    // id, while the authoritative mapping still names the rolled-back attempt.
    REQUIRE(std::filesystem::copy_file(
        oldStatePath,
        statePath,
        std::filesystem::copy_options::overwrite_existing,
        ec));
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::remove(oldStatePath, ec));
    REQUIRE_FALSE(ec);

    const auto staleMapping = PartyQuestRuntimeRestoreAttemptStore::Load(
        *paths,
        kAttemptFaultCampaign,
        kAttemptFaultPlayer,
        transactionId);
    REQUIRE(staleMapping.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(staleMapping.State.has_value());
    REQUIRE(staleMapping.State->CurrentOrdinal == 0);
    REQUIRE(staleMapping.State->CurrentRestoreId == firstRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            *paths,
            unpublishedRestoreId)));

    PartyQuestRuntimeRestoreAttemptResult recovered;
    {
        PartyQuestReplicaWorkspaceLease lease;
        REQUIRE(lease.Acquire(
                    *paths,
                    kAttemptFaultCampaign,
                    kAttemptFaultPlayer) ==
            PartyQuestReplicaWorkspaceLeaseStatus::Acquired);
        const auto capability = lease.CreatePublicationCapability(
            *paths,
            kAttemptFaultCampaign,
            kAttemptFaultPlayer);
        recovered =
            PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
                *paths,
                kAttemptFaultCampaign,
                kAttemptFaultPlayer,
                transactionId,
                0,
                capability);
    }

    REQUIRE(recovered.Status == PartyQuestRuntimeRestoreAttemptStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.State->CurrentOrdinal == 1);
    REQUIRE(recovered.State->LastRolledBackRestoreId == firstRestoreId);
    REQUIRE(recovered.State->CurrentRestoreId > unpublishedRestoreId);
    REQUIRE(recovered.State->CurrentRestoreId != unpublishedRestoreId);
    REQUIRE_FALSE(std::filesystem::exists(
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            *paths,
            unpublishedRestoreId)));

    const auto restarted = EnsureAttempt(*paths, transactionId);
    REQUIRE(restarted.IsUsable());
    REQUIRE(restarted.State == recovered.State);
    REQUIRE(restarted.RestoreId == recovered.State->CurrentRestoreId);

    const auto oldTerminal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
            PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                *paths,
                firstRestoreId));
    REQUIRE(oldTerminal.Status ==
        PartyQuestReplicaRestoreJournalPersistenceStatus::Success);
    REQUIRE(oldTerminal.State.has_value());
    REQUIRE(oldTerminal.State->Phase ==
        PartyQuestReplicaRestoreJournalPhase::RolledBack);
#endif
}
