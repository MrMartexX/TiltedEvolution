#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionStore.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kStoreCampaign{0x0A0B0C0D0E0F1011ull, 0x1213141516171819ull};
const PartyQuestPlayerProfileId kStorePlayer{0x2122232425262728ull, 0x292A2B2C2D2E2F30ull};

struct StoreSandbox
{
    std::filesystem::path Root;

    StoreSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_store_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~StoreSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildStorePaths(const StoreSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kStoreCampaign,
        kStorePlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

PartyQuestRuntimeApplyEntry BuildStoreEntry(
    uint64_t aTransactionId,
    uint64_t aWorldRevision,
    PartyQuestRuntimeApplyState aState,
    bool aSaveGuard,
    bool aCheckpoint,
    bool aMutation)
{
    PartyQuestRuntimeApplyEntry entry;
    entry.TransactionId = aTransactionId;
    entry.TargetWorldRevision = aWorldRevision;
    entry.QuestId = GameId(61, static_cast<uint32_t>(0x1000 + aTransactionId));
    entry.CanonicalDigest = 0xBADC0FFEE0000000ull ^ aTransactionId;
    entry.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    entry.State = aState;
    entry.SaveGuardActive = aSaveGuard;
    entry.CheckpointCreated = aCheckpoint;
    entry.RuntimeMutationMayHaveOccurred = aMutation;
    return entry;
}

PartyQuestRuntimeRecoveryState BuildStoreState(
    std::optional<PartyQuestRuntimeApplyEntry> aActive = std::nullopt)
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kStoreCampaign;
    state.PlayerProfileId = kStorePlayer;
    state.Active = std::move(aActive);
    return state;
}

PartyQuestRuntimeApplySession BuildStoreSession()
{
    return PartyQuestRuntimeApplySession(kStoreCampaign, kStorePlayer);
}
} // namespace

TEST_CASE("Runtime session store binds a fresh player sidecar without inventing state", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    auto session = BuildStoreSession();

    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::NewSession);
    REQUIRE(result.PersistenceStatus == PartyQuestRuntimeApplyPersistenceStatus::FileNotFound);
    REQUIRE_FALSE(std::filesystem::exists(paths.RuntimeApplySidecar));
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());
}

TEST_CASE("Deferred runtime state resumes and future transitions persist through the bound store", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    constexpr uint64_t kTransactionId = 23001;
    const auto state = BuildStoreState(BuildStoreEntry(
        kTransactionId,
        1700,
        PartyQuestRuntimeApplyState::DeferredWorld,
        false,
        false,
        false));
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto session = BuildStoreSession();
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::DeferredRestored);
    REQUIRE(result.RecoveryDisposition == PartyQuestRuntimeRecoveryDisposition::DeferredRestored);
    REQUIRE(session.GetCoordinator().GetActive() != nullptr);
    REQUIRE(session.GetCoordinator().GetActive()->State == PartyQuestRuntimeApplyState::DeferredWorld);

    REQUIRE(session.MarkWorldReady(kTransactionId) ==
        PartyQuestRuntimeDurableTransitionStatus::Applied);
    const auto reloaded = PartyQuestRuntimeApplyPersistence::Load(paths.RuntimeApplySidecar);
    REQUIRE(reloaded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(reloaded.State.has_value());
    REQUIRE(reloaded.State->Active.has_value());
    REQUIRE(reloaded.State->Active->State == PartyQuestRuntimeApplyState::AwaitingCheckpoint);
    REQUIRE(reloaded.State->Active->SaveGuardActive);
}

TEST_CASE("Pre-mutation stale runtime entry is discarded and cleanup is immediately durable", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    const auto state = BuildStoreState(BuildStoreEntry(
        23002,
        1710,
        PartyQuestRuntimeApplyState::ReadyToApply,
        true,
        true,
        false));
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto session = BuildStoreSession();
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::PreMutationRestarted);
    REQUIRE(result.RecoveryDisposition ==
        PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
    REQUIRE_FALSE(session.GetCoordinator().IsRecoveryBlocked());

    const auto reloaded = PartyQuestRuntimeApplyPersistence::Load(paths.RuntimeApplySidecar);
    REQUIRE(reloaded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(reloaded.State.has_value());
    REQUIRE(reloaded.State->Active == std::nullopt);
}

TEST_CASE("Armed runtime mutation bootstraps into a blocked recovery state", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    constexpr uint64_t kTransactionId = 23003;
    const auto state = BuildStoreState(BuildStoreEntry(
        kTransactionId,
        1720,
        PartyQuestRuntimeApplyState::WaitingForPapyrus,
        true,
        true,
        true));
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto session = BuildStoreSession();
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::RecoveryRequired);
    REQUIRE(result.RecoveryDisposition ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);
    REQUIRE(session.GetCoordinator().GetRecoveryRecord()->TransactionId == kTransactionId);
}

TEST_CASE("Runtime store refuses a journal belonging to another player profile", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    auto state = BuildStoreState();
    state.PlayerProfileId.Low ^= 1;
    REQUIRE(state.PlayerProfileId.IsValid());
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                state) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto session = BuildStoreSession();
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::JournalIdentityMismatch);
    REQUIRE(result.RecoveryDisposition ==
        PartyQuestRuntimeRecoveryDisposition::PlayerProfileMismatch);
}

TEST_CASE("Runtime store never silently promotes a stale backup-only journal", "[quest.party-state.runtime-store]")
{
    StoreSandbox sandbox;
    const auto paths = BuildStorePaths(sandbox);
    const auto first = BuildStoreState();
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                first) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    auto second = first;
    PartyQuestRuntimeCommittedRecord committed;
    committed.TransactionId = 23004;
    committed.TargetWorldRevision = 1730;
    committed.QuestId = GameId(61, 0x2000);
    committed.CanonicalDigest = 0xDEADBEEF12345678ull;
    committed.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    committed.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    second.Committed.push_back(committed);
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                paths.RuntimeApplySidecar,
                second) == PartyQuestRuntimeApplyPersistenceStatus::Success);

    std::error_code ec;
    std::filesystem::remove(paths.RuntimeApplySidecar, ec);
    REQUIRE_FALSE(ec);
    auto temporary = paths.RuntimeApplySidecar;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
    ec.clear();

    auto session = BuildStoreSession();
    const auto result = PartyQuestRuntimeSessionStore::BindAndLoad(session, paths);
    REQUIRE(result.Status == PartyQuestRuntimeSessionStoreStatus::JournalRecoveryRequired);
    REQUIRE(result.PersistenceStatus ==
        PartyQuestRuntimeApplyPersistenceStatus::BackupRecoveryRequired);
    REQUIRE(session.GetCoordinator().GetActive() == nullptr);
}
