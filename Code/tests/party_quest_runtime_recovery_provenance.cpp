#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestRuntimeRecovery.h>

#include <party_quest_runtime_recovery_coordinator_test_access.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>

namespace
{
const PartyQuestCampaignId kCampaign{0xABCDEF0123456789ull, 0x1020304050607080ull};
const PartyQuestPlayerProfileId kPlayer{0x8877665544332211ull, 0x1122334455667788ull};

struct Sandbox
{
    std::filesystem::path Root;

    Sandbox()
    {
        const auto nonce = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_recovery_provenance_" + std::to_string(nonce));
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

PartyQuestRuntimeRecoveryState BuildPostMutationState(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = aTransactionId;
    active.TargetWorldRevision = aWorldRevision;
    active.QuestId = GameId(77, 0x2200);
    active.CanonicalDigest = 0xA1B2C3D4E5F60718ull;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions,
        active.CanonicalDigest,
        0x77002200);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = true;
    active.RuntimeMutationMayHaveOccurred = true;

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaign;
    state.PlayerProfileId = kPlayer;
    state.Active = active;
    return state;
}

PartyQuestRuntimeApplySession BuildBlockedSession(
    uint64_t aTransactionId,
    uint64_t aWorldRevision)
{
    PartyQuestRuntimeApplySession session(
        kCampaign,
        kPlayer,
        [](const PartyQuestRuntimeRecoveryState&) { return true; },
        PartyQuestPersistenceGuarantee::ProcessCrashResilient);
    REQUIRE(session.RestoreRecoveryState(
                BuildPostMutationState(aTransactionId, aWorldRevision)) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
    REQUIRE(session.GetCoordinator().GetRecoveryRecord() != nullptr);
    return session;
}

PartyQuestCoopSavePaths BuildPaths(const Sandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());
    REQUIRE(PartyQuestCoopSaveLayout::Matches(*paths, kCampaign, kPlayer));
    return *paths;
}
} // namespace

TEST_CASE(
    "crash recovery keeps restore identity unset before a restore domain is selected",
    "[quest.party-state.runtime-recovery][provenance][crash]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 28301;
    constexpr uint64_t worldRevision = 1940;
    auto session = BuildBlockedSession(transactionId, worldRevision);

    const auto result =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveCrashRecovery(
            session,
            paths);

    REQUIRE(result.Status == PartyQuestRuntimeRecoveryStatus::CheckpointMissing);
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.TargetWorldRevision == worldRevision);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);
    REQUIRE(result.RestoreId == 0);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
}

TEST_CASE(
    "live recovery keeps restore identity unset while crash recovery owns the barrier",
    "[quest.party-state.runtime-recovery][provenance][live-recovery]")
{
    Sandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    constexpr uint64_t transactionId = 28302;
    constexpr uint64_t worldRevision = 1950;
    auto session = BuildBlockedSession(transactionId, worldRevision);

    const auto result =
        PartyQuestRuntimeRecoveryCoordinatorTestAccess::ResolveLiveRecovery(
            session,
            paths);

    REQUIRE(result.Status ==
        PartyQuestRuntimeRecoveryStatus::InvalidRecoveryState);
    REQUIRE(result.TransactionId == transactionId);
    REQUIRE(result.TargetWorldRevision == worldRevision);
    REQUIRE(result.RestoreDomain ==
        PartyQuestRuntimeRestoreDurabilityDomain::None);
    REQUIRE(result.RestoreId == 0);
    REQUIRE(session.GetCoordinator().IsRecoveryBlocked());
}
