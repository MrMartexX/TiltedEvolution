#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>
#include <Structs/Skyrim/PartyQuestReplicaSnapshotManager.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const PartyQuestCampaignId kIntegrityCampaign{0x1111111122222222ull, 0x3333333344444444ull};
const PartyQuestPlayerProfileId kIntegrityPlayer{0x5555555566666666ull, 0x7777777788888888ull};
constexpr uint64_t kIntegrityTransactionId = 22001;

struct IntegritySandbox
{
    std::filesystem::path Root;

    IntegritySandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_recovery_integrity_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~IntegritySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

void WriteIntegrityBytes(
    const std::filesystem::path& acPath,
    const std::string& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(acBytes.data(), static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    REQUIRE(file.good());
}

PartyQuestRuntimeRecoveryState BuildIntegrityRecoveryState(uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = kIntegrityTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(52, 0x1000);
    active.CanonicalDigest = 0x1234567890ABCDEFull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kIntegrityCampaign;
    state.PlayerProfileId = kIntegrityPlayer;
    state.Active = active;
    return state;
}
} // namespace

TEST_CASE("Committed restore journal cannot clear runtime barrier after live replica changes", "[quest.party-state.runtime-recovery]")
{
    IntegritySandbox sandbox;
    const auto paths = PartyQuestCoopSaveLayout::Build(
        sandbox.Root / "CoopCampaigns",
        kIntegrityCampaign,
        kIntegrityPlayer);
    REQUIRE(paths.has_value());

    constexpr uint64_t kWorldRevision = 1640;
    const auto liveSave = paths->SavesDirectory / "Hero.ess";
    WriteIntegrityBytes(liveSave, "PRE_REPAIR_1640");

    const auto spec = PartyQuestReplicaFileExecutor::InspectSource(
        PartyQuestReplicaFileKind::SkyrimSave,
        liveSave,
        "Hero.ess");
    REQUIRE(spec.has_value());
    const auto checkpointPlan = PartyQuestReplicaFilePlanner::BuildRevisionCheckpointPlan(
        *paths,
        PartyQuestCheckpointKind::PreRepair,
        kWorldRevision,
        {*spec});
    REQUIRE(checkpointPlan.IsReady());

    PartyQuestReplicaSnapshotManager manager(
        *paths,
        kIntegrityCampaign,
        kIntegrityPlayer);
    REQUIRE(manager.EnsureRevisionCheckpoint(
                PartyQuestCheckpointKind::PreRepair,
                kWorldRevision,
                checkpointPlan).IsReady());

    WriteIntegrityBytes(liveSave, "MUTATED_1640");

    bool allowPersistence = false;
    PartyQuestRuntimeApplySession session(
        kIntegrityCampaign,
        kIntegrityPlayer,
        [&allowPersistence](const PartyQuestRuntimeRecoveryState&)
        {
            return allowPersistence;
        });
    REQUIRE(session.RestoreRecoveryState(BuildIntegrityRecoveryState(kWorldRevision)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);

    const auto first = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        *paths);
    REQUIRE(first.Status == PartyQuestRuntimeRecoveryStatus::RuntimeStatePersistenceFailed);
    REQUIRE(first.RestoreId == kIntegrityTransactionId);
    REQUIRE(first.RestoreStatus == PartyQuestReplicaRestoreExecutionStatus::Success);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());

    // The restore transaction is committed, but an external actor changes the
    // live replica before the runtime recovery barrier can be durably cleared.
    WriteIntegrityBytes(liveSave, "CHANGED_AFTER_RESTORE_COMMIT");
    allowPersistence = true;

    const auto retry = PartyQuestRuntimeRecoveryCoordinator::ResolveCrashRecovery(
        session,
        *paths);
    REQUIRE(retry.Status == PartyQuestRuntimeRecoveryStatus::RestoreFailed);
    REQUIRE(retry.RestoreId == kIntegrityTransactionId);
    REQUIRE(retry.RestoreStatus ==
        PartyQuestReplicaRestoreExecutionStatus::RestoredVerificationFailed);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
}
