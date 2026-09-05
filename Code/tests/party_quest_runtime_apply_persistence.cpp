#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Structs/Skyrim/PartyQuestCheckpointSidecars.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>
#include <Structs/Skyrim/PartyQuestRuntimeCompatibility.h>

#include <catch2/catch.hpp>

#include "TPTestsSubprocess.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
const PartyQuestCampaignId kCampaignId{0x1111222233334444ull, 0xAAAABBBBCCCCDDDDull};
const PartyQuestCampaignId kOtherCampaignId{0x9999888877776666ull, 0x5555444433332222ull};
const PartyQuestPlayerProfileId kPlayerProfileId{0x1011121314151617ull, 0x2122232425262728ull};

PartyQuestAdmissionDecision BuildRecoveryAdmission(GameId aQuestId)
{
    PartyQuestSyncFacts facts;
    facts.QuestType = 1;
    facts.HasStages = true;
    facts.IsDisplayedInHud = true;
    facts.HasDisplayName = true;
    const auto admission = PartyQuestAdmissionPolicy::Evaluate(aQuestId, facts);
    REQUIRE(admission.IsAdmitted());
    return admission;
}

PartyQuestRuntimeSafetyProfile BuildRecoveryAuthorization(GameId aQuestId)
{
    PartyQuestRuntimeCompatibilityRequirement requirement;
    requirement.QuestId = aQuestId;
    requirement.ProfileVersion = 2;
    requirement.ResolvedRecordFingerprint = 0x5151515151515151ull;
    requirement.WinningOverrideFingerprint = 0x6262626262626262ull;
    requirement.ScriptFingerprint = 0x7373737373737373ull;
    requirement.NativeAdapterFingerprint = 0x8484848484848484ull;
    requirement.AdapterMutationComponents = PartyQuestVerificationComponent::QuestSnapshot;

    PartyQuestRuntimeCompatibilityFacts facts;
    facts.ProfileVersion = requirement.ProfileVersion;
    facts.ResolvedRecordFingerprint = requirement.ResolvedRecordFingerprint;
    facts.WinningOverrideFingerprint = requirement.WinningOverrideFingerprint;
    facts.ScriptFingerprint = requirement.ScriptFingerprint;
    facts.NativeAdapterFingerprint = requirement.NativeAdapterFingerprint;
    facts.AdapterMutationComponents = requirement.AdapterMutationComponents;

    const auto decision = PartyQuestRuntimeCompatibilityPolicy::Evaluate(requirement, facts);
    REQUIRE(decision.IsAuthorized());
    return decision.SafetyProfile;
}

PartyQuestRuntimeApplyRequest BuildRecoveryRequest(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aTargetWorldRevision,
    bool aDeferred = false)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = 40;
    snapshot.Revision = 5;
    snapshot.InitiatorPlayerId = 4;
    snapshot.CompletedStages = {10, 20, 40};
    snapshot.Objectives = {{40, QuestObjectiveState::Displayed}};
    if (aDeferred)
        snapshot.ReferenceAliases = {{1, GameId(0, 0x1234), false}};
    snapshot.Canonicalize();

    PartyQuestRuntimeApplyRequest request;
    request.TransactionId = aTransactionId;
    request.TargetWorldRevision = aTargetWorldRevision;
    request.SidecarManifestFingerprint = PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    request.CanonicalSnapshot = snapshot;
    request.Plan = PartyQuestRuntimeSafetyPolicy::BuildApplyPlan(
        BuildRecoveryAdmission(aQuestId),
        snapshot,
        BuildRecoveryAuthorization(aQuestId));
    REQUIRE(request.Plan.Safety.IsRuntimeSafe());
    return request;
}

PartyQuestRuntimeCommittedRecord BuildCommittedRecord(
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    const auto identity =
        PartyQuestRuntimeApplyCoordinator::BuildValidatedIdentity(acRequest);
    REQUIRE(identity.has_value());

    PartyQuestRuntimeCommittedRecord record;
    record.TransactionId = acRequest.TransactionId;
    record.TargetWorldRevision = identity->TargetWorldRevision;
    record.QuestId = identity->QuestId;
    record.CanonicalDigest = identity->CanonicalDigest;
    record.SidecarManifestFingerprint = identity->SidecarManifestFingerprint;
    record.Actions = identity->Actions;
    record.ExpectedVerification = identity->ExpectedVerification;
    return record;
}

void CommitRecoveryRequest(
    PartyQuestRuntimeApplyCoordinator& aCoordinator,
    const PartyQuestRuntimeApplyRequest& acRequest)
{
    const auto begin = aCoordinator.Begin(acRequest);
    if (begin == PartyQuestRuntimeApplyBeginStatus::Deferred)
        REQUIRE(aCoordinator.MarkWorldReady(acRequest));
    else
        REQUIRE(begin == PartyQuestRuntimeApplyBeginStatus::Started);

    REQUIRE(aCoordinator.MarkCheckpointCreated(acRequest.TransactionId));
    REQUIRE(aCoordinator.MarkApplyDispatched(acRequest.TransactionId));

    PartyQuestPapyrusQuiescenceTracker tracker;
    REQUIRE(tracker.Begin(acRequest.TransactionId));
    REQUIRE(tracker.Observe(acRequest.TransactionId, 0, 1) ==
        PartyQuestPapyrusQuiescenceStatus::Waiting);
    REQUIRE(tracker.Observe(acRequest.TransactionId, 0, 1) ==
        PartyQuestPapyrusQuiescenceStatus::Quiescent);
    auto authorization = tracker.Authorize();
    REQUIRE(authorization.has_value());
    REQUIRE(aCoordinator.MarkPapyrusQuiescent(
        tracker,
        std::move(*authorization)));

    REQUIRE(aCoordinator.SubmitResnapshot(acRequest.TransactionId, acRequest.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::NeedsStableSample);
    REQUIRE(aCoordinator.SubmitResnapshot(acRequest.TransactionId, acRequest.CanonicalSnapshot) ==
        PartyQuestRuntimeVerificationStatus::Stable);
    REQUIRE(aCoordinator.Commit(acRequest.TransactionId));
}

void RemoveRuntimeApplyArchive(const std::filesystem::path& acPath)
{
    std::error_code ec;
    std::filesystem::remove(acPath, ec);
    auto backup = acPath;
    backup += ".bak";
    std::filesystem::remove(backup, ec);
    auto temporary = acPath;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ec);
}

bool WriteBytes(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(acBytes.data()), static_cast<std::streamsize>(acBytes.size()));
    file.flush();
    return file.good();
}

struct RuntimeApplyPersistenceSandbox
{
    std::filesystem::path Root;

    RuntimeApplyPersistenceSandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_runtime_apply_crash_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~RuntimeApplyPersistenceSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

bool SetRuntimeApplyEnvironment(const char* apName, const std::string& acValue)
{
#ifdef _WIN32
    return _putenv_s(apName, acValue.c_str()) == 0;
#else
    return setenv(apName, acValue.c_str(), 1) == 0;
#endif
}

void ClearRuntimeApplyEnvironment(const char* apName)
{
#ifdef _WIN32
    _putenv_s(apName, "");
#else
    unsetenv(apName);
#endif
}

struct RuntimeApplyCrashBoundary
{
    PartyQuestRuntimeApplyPersistenceBoundary Boundary;
};

PartyQuestRuntimeApplyPersistenceDirective CrashRuntimeApplyAtBoundary(
    PartyQuestRuntimeApplyPersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto& crash = *static_cast<const RuntimeApplyCrashBoundary*>(apContext);
    if (crash.Boundary == aBoundary)
        std::_Exit(89);
    return PartyQuestRuntimeApplyPersistenceDirective::Continue;
}

PartyQuestRuntimeRecoveryState BuildOldCrashState()
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    CommitRecoveryRequest(
        coordinator,
        BuildRecoveryRequest(91001, GameId(19, 0x1000), 160));
    return coordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);
}

PartyQuestRuntimeRecoveryState BuildArmedCrashState()
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    const auto request = BuildRecoveryRequest(91002, GameId(19, 0x2000), 161);
    REQUIRE(coordinator.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(coordinator.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(coordinator.MarkApplyDispatched(request.TransactionId));
    return coordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);
}

void RunRuntimeApplyCrashProcess(
    const RuntimeApplyPersistenceSandbox& acSandbox,
    const char* apBoundary)
{
    REQUIRE(SetRuntimeApplyEnvironment(
        "TP_RUNTIME_APPLY_CRASH_ROOT", acSandbox.Root.string()));
    REQUIRE(SetRuntimeApplyEnvironment(
        "TP_RUNTIME_APPLY_CRASH_BOUNDARY", apBoundary));

    const int exitCode = RunTPTestsSubprocess(
        "Runtime apply journal atomic publication crash helper");

    ClearRuntimeApplyEnvironment("TP_RUNTIME_APPLY_CRASH_BOUNDARY");
    ClearRuntimeApplyEnvironment("TP_RUNTIME_APPLY_CRASH_ROOT");
    REQUIRE(exitCode != 0);

    const auto path = acSandbox.Root / "runtime-apply.bin";
    const auto loaded = PartyQuestRuntimeApplyPersistence::Load(path);
    REQUIRE(loaded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(loaded.State.has_value());

    const bool beforePrimaryMove =
        std::string(apBoundary) == "TemporaryVerified";
    REQUIRE(*loaded.State ==
        (beforePrimaryMove ? BuildOldCrashState() : BuildArmedCrashState()));
    REQUIRE(loaded.UsedTemporary ==
        (std::string(apBoundary) == "PrimaryMovedToBackup"));

    PartyQuestRuntimeApplyCoordinator recovered;
    const auto disposition = recovered.RestoreRecoveryState(
        *loaded.State, kCampaignId, kPlayerProfileId);
    REQUIRE(disposition ==
        (beforePrimaryMove
            ? PartyQuestRuntimeRecoveryDisposition::Clean
            : PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired));
}
} // namespace

TEST_CASE("Runtime apply journal atomic publication crash helper", "[.][quest.party-state.runtime-apply.persistence][fault-helper]")
{
    const char* rootValue = std::getenv("TP_RUNTIME_APPLY_CRASH_ROOT");
    const char* boundaryValue = std::getenv("TP_RUNTIME_APPLY_CRASH_BOUNDARY");
    REQUIRE(rootValue != nullptr);
    REQUIRE(boundaryValue != nullptr);

    const std::string boundary = boundaryValue;
    REQUIRE((boundary == "TemporaryVerified" ||
        boundary == "PrimaryMovedToBackup" ||
        boundary == "TemporaryPublished"));

    const auto path = std::filesystem::path(rootValue) / "runtime-apply.bin";
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                path, BuildOldCrashState()) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    RuntimeApplyCrashBoundary crash{
        boundary == "TemporaryVerified"
            ? PartyQuestRuntimeApplyPersistenceBoundary::TemporaryVerified
            : boundary == "PrimaryMovedToBackup"
                ? PartyQuestRuntimeApplyPersistenceBoundary::PrimaryMovedToBackup
                : PartyQuestRuntimeApplyPersistenceBoundary::TemporaryPublished};
    const auto status = PartyQuestRuntimeApplyPersistence::SaveAtomically(
        path,
        BuildArmedCrashState(),
        {CrashRuntimeApplyAtBoundary, &crash});
    FAIL("Crash boundary returned with status " << static_cast<int>(status));
}

TEST_CASE("Runtime apply recovery state encodes deterministically and round-trips", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    CommitRecoveryRequest(coordinator, BuildRecoveryRequest(10001, GameId(11, 0x1000), 80));
    REQUIRE(coordinator.Begin(BuildRecoveryRequest(10002, GameId(11, 0x2000), 81, true)) ==
        PartyQuestRuntimeApplyBeginStatus::Deferred);

    const auto original = coordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);
    REQUIRE(original.CampaignId == kCampaignId);
    REQUIRE(original.PlayerProfileId == kPlayerProfileId);
    REQUIRE(original.Committed.size() == 1);
    REQUIRE(original.Active.has_value());

    const auto first = PartyQuestRuntimeApplyPersistence::Encode(original);
    const auto second = PartyQuestRuntimeApplyPersistence::Encode(original);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);

    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(first);
    REQUIRE(decoded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(decoded.State.has_value());
    REQUIRE(*decoded.State == original);
}

TEST_CASE("Runtime apply persistence rejects invalid campaign corruption truncation and hostile lengths", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeApplyCoordinator coordinator;
    CommitRecoveryRequest(coordinator, BuildRecoveryRequest(20001, GameId(12, 0x1000), 90));
    const auto state = coordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);
    const auto encoded = PartyQuestRuntimeApplyPersistence::Encode(state);
    REQUIRE(encoded.size() > 24);

    auto invalidCampaign = state;
    invalidCampaign.CampaignId = {};
    REQUIRE(PartyQuestRuntimeApplyPersistence::Encode(invalidCampaign).empty());

    auto incompleteEnvelope = state;
    REQUIRE_FALSE(incompleteEnvelope.Committed.empty());
    incompleteEnvelope.Committed[0].ExpectedVerification.Required =
        PartyQuestVerificationComponent::QuestSnapshot;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Encode(incompleteEnvelope).empty());

    auto corrupted = encoded;
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(corrupted).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(truncated).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(unsupported).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::UnsupportedVersion);

    auto legacyV3 = encoded;
    legacyV3[8] = 3;
    legacyV3[9] = 0;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(legacyV3).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::UnsupportedVersion);

    auto oversizedPayload = encoded;
    for (size_t i = 10; i < 18; ++i)
        oversizedPayload[i] = 0xFF;
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(oversizedPayload).Status !=
        PartyQuestRuntimeApplyPersistenceStatus::Success);
}

TEST_CASE("Older runtime apply backup is exposed only as uncertain recovery", "[quest.party-state.runtime-apply.persistence]")
{
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_runtime_apply_backup_" + std::to_string(suffix) + ".bin");
    RemoveRuntimeApplyArchive(path);

    PartyQuestRuntimeApplyCoordinator first;
    CommitRecoveryRequest(first, BuildRecoveryRequest(30001, GameId(13, 0x1000), 100));
    const auto firstState = first.ExportRecoveryState(kCampaignId, kPlayerProfileId);
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(path, firstState) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    PartyQuestRuntimeApplyCoordinator second;
    REQUIRE(second.RestoreRecoveryState(firstState, kCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::Clean);
    CommitRecoveryRequest(second, BuildRecoveryRequest(30002, GameId(13, 0x2000), 101));
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(
                path,
                second.ExportRecoveryState(kCampaignId, kPlayerProfileId)) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    const auto recovered = PartyQuestRuntimeApplyPersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestRuntimeApplyPersistenceStatus::BackupRecoveryRequired);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.UsedBackup);
    REQUIRE_FALSE(recovered.UsedTemporary);
    REQUIRE(*recovered.State == firstState);

    RemoveRuntimeApplyArchive(path);
}

TEST_CASE("Valid temporary runtime journal wins after interrupted atomic replacement", "[quest.party-state.runtime-apply.persistence]")
{
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_runtime_apply_tmp_" + std::to_string(suffix) + ".bin");
    RemoveRuntimeApplyArchive(path);

    PartyQuestRuntimeApplyCoordinator oldCoordinator;
    CommitRecoveryRequest(oldCoordinator, BuildRecoveryRequest(35001, GameId(13, 0x3000), 105));
    const auto oldState = oldCoordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);
    REQUIRE(PartyQuestRuntimeApplyPersistence::SaveAtomically(path, oldState) ==
        PartyQuestRuntimeApplyPersistenceStatus::Success);

    PartyQuestRuntimeApplyCoordinator newCoordinator;
    REQUIRE(newCoordinator.RestoreRecoveryState(oldState, kCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::Clean);
    CommitRecoveryRequest(newCoordinator, BuildRecoveryRequest(35002, GameId(13, 0x4000), 106));
    const auto newState = newCoordinator.ExportRecoveryState(kCampaignId, kPlayerProfileId);

    auto temporaryPath = path;
    temporaryPath += ".tmp";
    auto backupPath = path;
    backupPath += ".bak";
    REQUIRE(WriteBytes(temporaryPath, PartyQuestRuntimeApplyPersistence::Encode(newState)));
    std::error_code ec;
    std::filesystem::rename(path, backupPath, ec);
    REQUIRE_FALSE(ec);

    const auto recovered = PartyQuestRuntimeApplyPersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.UsedTemporary);
    REQUIRE_FALSE(recovered.UsedBackup);
    REQUIRE(*recovered.State == newState);

    RemoveRuntimeApplyArchive(path);
}

TEST_CASE("Runtime mutation barrier publication survives abrupt process termination", "[quest.party-state.runtime-apply.persistence][fault][process]")
{
    SECTION("verified temporary before primary move preserves clean durable truth")
    {
        RuntimeApplyPersistenceSandbox sandbox;
        RunRuntimeApplyCrashProcess(sandbox, "TemporaryVerified");
    }

    SECTION("primary moved aside promotes the complete mutation barrier")
    {
        RuntimeApplyPersistenceSandbox sandbox;
        RunRuntimeApplyCrashProcess(sandbox, "PrimaryMovedToBackup");
    }

    SECTION("published mutation barrier blocks runtime work after restart")
    {
        RuntimeApplyPersistenceSandbox sandbox;
        RunRuntimeApplyCrashProcess(sandbox, "TemporaryPublished");
    }
}

TEST_CASE("Committed runtime transaction stays idempotent after restart", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(40001, GameId(14, 0x1000), 110);
    PartyQuestRuntimeApplyCoordinator beforeRestart;
    CommitRecoveryRequest(beforeRestart, request);

    const auto decoded = PartyQuestRuntimeApplyPersistence::Decode(
        PartyQuestRuntimeApplyPersistence::Encode(
            beforeRestart.ExportRecoveryState(kCampaignId, kPlayerProfileId)));
    REQUIRE(decoded.Status == PartyQuestRuntimeApplyPersistenceStatus::Success);
    REQUIRE(decoded.State.has_value());

    PartyQuestRuntimeApplyCoordinator afterRestart;
    REQUIRE(afterRestart.RestoreRecoveryState(*decoded.State, kCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::Clean);
    REQUIRE(afterRestart.IsCommitted(request.TransactionId));
    REQUIRE(afterRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted);
}

TEST_CASE("Runtime apply recovery journal cannot cross campaign boundaries", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(45001, GameId(14, 0x2000), 115);
    PartyQuestRuntimeApplyCoordinator original;
    CommitRecoveryRequest(original, request);
    const auto state = original.ExportRecoveryState(kCampaignId, kPlayerProfileId);

    PartyQuestRuntimeApplyCoordinator wrongCampaign;
    REQUIRE(wrongCampaign.RestoreRecoveryState(state, kOtherCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::CampaignMismatch);
    REQUIRE_FALSE(wrongCampaign.IsCommitted(request.TransactionId));
    REQUIRE_FALSE(wrongCampaign.IsRecoveryBlocked());
}

TEST_CASE("Crash after mutation blocks all new apply work until checkpoint restoration", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(50001, GameId(15, 0x1000), 120);
    PartyQuestRuntimeApplyCoordinator beforeCrash;
    REQUIRE(beforeCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(beforeCrash.MarkCheckpointCreated(request.TransactionId));
    REQUIRE(beforeCrash.MarkApplyDispatched(request.TransactionId));

    const auto state = beforeCrash.ExportRecoveryState(kCampaignId, kPlayerProfileId);
    PartyQuestRuntimeApplyCoordinator afterCrash;
    REQUIRE(afterCrash.RestoreRecoveryState(state, kCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::CheckpointRestoreRequired);
    REQUIRE(afterCrash.IsRecoveryBlocked());

    REQUIRE(afterCrash.Begin(BuildRecoveryRequest(50002, GameId(15, 0x2000), 121)) ==
        PartyQuestRuntimeApplyBeginStatus::RecoveryBlocked);
    REQUIRE_FALSE(afterCrash.AcknowledgeCheckpointRestored(99999));
    REQUIRE(afterCrash.AcknowledgeCheckpointRestored(request.TransactionId));
    REQUIRE_FALSE(afterCrash.IsRecoveryBlocked());
    REQUIRE(afterCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
}

TEST_CASE("Deferred world work is replanned after restart", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(60001, GameId(16, 0x1000), 130, true);
    PartyQuestRuntimeApplyCoordinator beforeRestart;
    REQUIRE(beforeRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Deferred);
    REQUIRE_FALSE(beforeRestart.IsSaveGuardActive());

    PartyQuestRuntimeApplyCoordinator afterRestart;
    REQUIRE(afterRestart.RestoreRecoveryState(
                beforeRestart.ExportRecoveryState(kCampaignId, kPlayerProfileId),
                kCampaignId,
                kPlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired);
    REQUIRE(afterRestart.GetActive() == nullptr);
    REQUIRE_FALSE(afterRestart.IsSaveGuardActive());
    REQUIRE_FALSE(afterRestart.MarkWorldReady(request));
    REQUIRE(afterRestart.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Deferred);
}

TEST_CASE("Pre-mutation crash discards stale apply entry and requests a fresh plan", "[quest.party-state.runtime-apply.persistence]")
{
    const auto request = BuildRecoveryRequest(70001, GameId(17, 0x1000), 140);
    PartyQuestRuntimeApplyCoordinator beforeCrash;
    REQUIRE(beforeCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
    REQUIRE(beforeCrash.MarkCheckpointCreated(request.TransactionId));
    REQUIRE_FALSE(beforeCrash.GetActive()->RuntimeMutationMayHaveOccurred);

    PartyQuestRuntimeApplyCoordinator afterCrash;
    REQUIRE(afterCrash.RestoreRecoveryState(
                beforeCrash.ExportRecoveryState(kCampaignId, kPlayerProfileId),
                kCampaignId,
                kPlayerProfileId) == PartyQuestRuntimeRecoveryDisposition::PreMutationRestartRequired);
    REQUIRE(afterCrash.GetActive() == nullptr);
    REQUIRE_FALSE(afterCrash.IsRecoveryBlocked());
    REQUIRE(afterCrash.Begin(request) == PartyQuestRuntimeApplyBeginStatus::Started);
}

TEST_CASE("Inconsistent recovery markers fail closed", "[quest.party-state.runtime-apply.persistence]")
{
    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaignId;
    state.PlayerProfileId = kPlayerProfileId;
    PartyQuestRuntimeApplyEntry active;
    active.TransactionId = 80001;
    active.TargetWorldRevision = 150;
    active.QuestId = GameId(18, 0x1000);
    active.CanonicalDigest = 0x12345678;
    active.SidecarManifestFingerprint =
        PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
    active.Actions = PartyQuestApplyAction::AdapterManaged |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    active.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
        active.Actions, active.CanonicalDigest, 0x80001001);
    active.State = PartyQuestRuntimeApplyState::WaitingForPapyrus;
    active.SaveGuardActive = true;
    active.CheckpointCreated = false;
    active.RuntimeMutationMayHaveOccurred = true;
    state.Active = active;

    PartyQuestRuntimeApplyCoordinator coordinator;
    REQUIRE(coordinator.RestoreRecoveryState(state, kCampaignId, kPlayerProfileId) ==
        PartyQuestRuntimeRecoveryDisposition::InvalidState);
    REQUIRE_FALSE(coordinator.IsRecoveryBlocked());
}

TEST_CASE("Runtime apply persistence rejects oversized archives before decode", "[quest.party-state.runtime-apply.persistence]")
{
    std::vector<uint8_t> oversized(
        PartyQuestDurableResourcePolicy::MaxRuntimeApplyArchiveBytes + 1,
        0);
    REQUIRE(PartyQuestRuntimeApplyPersistence::Decode(oversized).Status ==
        PartyQuestRuntimeApplyPersistenceStatus::ResourceLimitExceeded);
}

TEST_CASE("Runtime coordinator enforces committed identity bounds before admission", "[quest.party-state.runtime-apply][resource-bounds]")
{
    const auto committedRequest =
        BuildRecoveryRequest(90001, GameId(91, 0x1000), 900);

    PartyQuestRuntimeRecoveryState state;
    state.CampaignId = kCampaignId;
    state.PlayerProfileId = kPlayerProfileId;
    state.Committed.reserve(
        PartyQuestDurableResourcePolicy::MaxCommittedRuntimeRecords + 1);
    state.Committed.push_back(BuildCommittedRecord(committedRequest));

    constexpr PartyQuestApplyAction kActions =
        PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;
    for (uint64_t index = 1;
         index < PartyQuestDurableResourcePolicy::MaxCommittedRuntimeRecords;
         ++index)
    {
        PartyQuestRuntimeCommittedRecord record;
        record.TransactionId = 100000 + index;
        record.TargetWorldRevision = 1000 + index;
        record.QuestId = GameId(92, static_cast<uint32_t>(0x2000 + index));
        record.CanonicalDigest = 0xA000000000000000ull + index;
        record.SidecarManifestFingerprint =
            PartyQuestCheckpointSidecarManifest{}.ComputeFingerprint();
        record.Actions = kActions;
        record.ExpectedVerification = *PartyQuestVerificationPolicy::BuildExpected(
            record.Actions,
            record.CanonicalDigest,
            0x92001001);
        state.Committed.push_back(record);
    }

    auto overflow = state.Committed.back();
    overflow.TransactionId = 200000;
    state.Committed.push_back(overflow);
    PartyQuestRuntimeApplyCoordinator oversized;
    REQUIRE(oversized.RestoreRecoveryState(
                state,
                kCampaignId,
                kPlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::InvalidState);

    state.Committed.pop_back();
    PartyQuestRuntimeApplyCoordinator bounded;
    REQUIRE(bounded.RestoreRecoveryState(
                state,
                kCampaignId,
                kPlayerProfileId) ==
            PartyQuestRuntimeRecoveryDisposition::Clean);

    REQUIRE(bounded.Begin(committedRequest) ==
        PartyQuestRuntimeApplyBeginStatus::DuplicateCommitted);
    auto conflict = committedRequest;
    ++conflict.TargetWorldRevision;
    REQUIRE(bounded.Begin(conflict) ==
        PartyQuestRuntimeApplyBeginStatus::TransactionConflict);
    REQUIRE(bounded.Begin(BuildRecoveryRequest(90002, GameId(91, 0x2000), 901)) ==
        PartyQuestRuntimeApplyBeginStatus::ResourceLimitExceeded);
    REQUIRE(bounded.GetActive() == nullptr);
}
