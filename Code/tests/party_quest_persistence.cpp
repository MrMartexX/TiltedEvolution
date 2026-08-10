#include <Structs/Skyrim/PartyQuestStatePersistence.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

const std::filesystem::path& GetTPTestsExecutablePath() noexcept;

namespace
{
const PartyQuestCampaignId kPersistenceCampaign{
    0x1234567890ABCDEFull,
    0x0FEDCBA098765432ull};

constexpr uint64_t kTestFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kTestFnvPrime = 1099511628211ull;

uint64_t ComputeTestChecksum(const uint8_t* apData, size_t aSize) noexcept
{
    uint64_t checksum = kTestFnvOffsetBasis;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= kTestFnvPrime;
    }
    return checksum;
}

void WriteLittleEndian64(std::vector<uint8_t>& aBytes, size_t aOffset, uint64_t aValue)
{
    REQUIRE(aOffset <= aBytes.size());
    REQUIRE(aBytes.size() - aOffset >= sizeof(aValue));
    for (size_t i = 0; i < sizeof(aValue); ++i)
        aBytes[aOffset + i] = static_cast<uint8_t>((aValue >> (i * 8)) & 0xFF);
}

std::vector<uint8_t> MakeLegacyUnboundArchive(const PartyQuestState& acState)
{
    const auto bound = PartyQuestStatePersistence::Encode(kPersistenceCampaign, acState);
    constexpr size_t kHeaderSize = 8 + sizeof(uint16_t) + sizeof(uint64_t);
    constexpr size_t kCampaignIdSize = sizeof(uint64_t) * 2;
    constexpr size_t kChecksumSize = sizeof(uint64_t);
    REQUIRE(bound.size() >= kHeaderSize + kCampaignIdSize + kChecksumSize);

    std::vector<uint8_t> legacy(bound.begin(), bound.begin() + kHeaderSize);
    legacy[8] = 1;
    legacy[9] = 0;
    const size_t payloadEnd = bound.size() - kChecksumSize;
    legacy.insert(
        legacy.end(),
        bound.begin() + kHeaderSize + kCampaignIdSize,
        bound.begin() + payloadEnd);

    const uint64_t legacyPayloadSize =
        static_cast<uint64_t>(payloadEnd - kHeaderSize - kCampaignIdSize);
    WriteLittleEndian64(legacy, 10, legacyPayloadSize);
    const uint64_t checksum = ComputeTestChecksum(
        legacy.data() + kHeaderSize,
        static_cast<size_t>(legacyPayloadSize));
    const size_t checksumOffset = legacy.size();
    legacy.resize(checksumOffset + kChecksumSize);
    WriteLittleEndian64(legacy, checksumOffset, checksum);
    return legacy;
}

QuestSnapshot BuildPersistentSnapshot(GameId aQuestId, uint16_t aStage)
{
    QuestSnapshot snapshot;
    snapshot.QuestId = aQuestId;
    snapshot.Status = QuestSnapshotStatus::Running;
    snapshot.CurrentStage = aStage;
    snapshot.SceneParticipantPlayerId = 12;
    snapshot.CompletedStages = {aStage, 10, 20};
    snapshot.Objectives = {
        {30, QuestObjectiveState::Displayed},
        {10, QuestObjectiveState::Completed},
        {20, QuestObjectiveState::Hidden}
    };
    snapshot.ReferenceAliases = {
        {7, GameId(1, 0x220), true},
        {2, std::nullopt, false}
    };
    snapshot.LocationAliases = {
        {5, GameId(1, 0x330)},
        {1, std::nullopt}
    };
    snapshot.CreatedReferences = {
        GameId(2, 0x440),
        GameId(2, 0x441)
    };
    return snapshot;
}

PartyQuestTransaction BuildPersistentTransaction(
    uint64_t aTransactionId,
    GameId aQuestId,
    uint64_t aExpectedRevision,
    uint16_t aStage,
    uint32_t aInitiator = 7)
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = aTransactionId;
    transaction.InitiatorPlayerId = aInitiator;
    transaction.QuestId = aQuestId;
    transaction.ExpectedQuestRevision = aExpectedRevision;
    transaction.ProposedSnapshot = BuildPersistentSnapshot(aQuestId, aStage);
    return transaction;
}

PartyQuestState BuildPersistentState()
{
    PartyQuestState state;
    const GameId firstQuest(1, 0x1000);
    const GameId secondQuest(2, 0x2000);

    REQUIRE(state.Apply(BuildPersistentTransaction(10001, firstQuest, 0, 10, 3)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildPersistentTransaction(10002, secondQuest, 0, 40, 4)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(state.Apply(BuildPersistentTransaction(10003, firstQuest, 1, 30, 5)).Status == PartyQuestApplyStatus::Accepted);
    return state;
}

void RemoveArchiveFiles(const std::filesystem::path& acPath)
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

bool SetStatePersistenceEnvironment(const char* apName, const std::string& acValue)
{
#ifdef _WIN32
    return _putenv_s(apName, acValue.c_str()) == 0;
#else
    return setenv(apName, acValue.c_str(), 1) == 0;
#endif
}

void ClearStatePersistenceEnvironment(const char* apName)
{
#ifdef _WIN32
    _putenv_s(apName, "");
#else
    unsetenv(apName);
#endif
}

struct StatePersistenceCrashBoundary
{
    PartyQuestStatePersistenceBoundary Boundary;
};

PartyQuestStatePersistenceDirective CrashStatePersistenceAtBoundary(
    PartyQuestStatePersistenceBoundary aBoundary,
    void* apContext) noexcept
{
    const auto& crash = *static_cast<const StatePersistenceCrashBoundary*>(apContext);
    if (crash.Boundary == aBoundary)
        std::_Exit(91);
    return PartyQuestStatePersistenceDirective::Continue;
}

PartyQuestState BuildNewerPersistentState()
{
    auto state = BuildPersistentState();
    REQUIRE(state.Apply(
                BuildPersistentTransaction(10004, GameId(2, 0x2000), 1, 50, 9)).Status ==
        PartyQuestApplyStatus::Accepted);
    return state;
}

void RunStatePersistenceCrashProcess(
    const std::filesystem::path& acRoot,
    const char* apBoundary)
{
    REQUIRE(SetStatePersistenceEnvironment(
        "TP_STATE_PERSISTENCE_CRASH_ROOT", acRoot.string()));
    REQUIRE(SetStatePersistenceEnvironment(
        "TP_STATE_PERSISTENCE_CRASH_BOUNDARY", apBoundary));

    const auto& executable = GetTPTestsExecutablePath();
    REQUIRE_FALSE(executable.empty());
    const std::string command =
        "\"" + executable.string() +
        "\" \"Canonical state atomic publication crash helper\" --reporter compact";
    const int exitCode = std::system(command.c_str());

    ClearStatePersistenceEnvironment("TP_STATE_PERSISTENCE_CRASH_BOUNDARY");
    ClearStatePersistenceEnvironment("TP_STATE_PERSISTENCE_CRASH_ROOT");
    REQUIRE(exitCode != 0);

    const auto loaded = PartyQuestStatePersistence::Load(acRoot / "canonical.bin");
    REQUIRE(loaded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(loaded.CampaignId == kPersistenceCampaign);
    REQUIRE(loaded.State.has_value());
    const bool beforePrimaryMove = std::string(apBoundary) == "TemporaryVerified";
    REQUIRE(loaded.State->GetWorldRevision() == (beforePrimaryMove ? 3 : 4));
    REQUIRE(loaded.UsedTemporary ==
        (std::string(apBoundary) == "PrimaryMovedToBackup"));
    REQUIRE_FALSE(loaded.UsedBackup);
}
} // namespace

TEST_CASE("Canonical state atomic publication crash helper", "[.][quest.party-state.persistence][fault-helper]")
{
    const char* rootValue = std::getenv("TP_STATE_PERSISTENCE_CRASH_ROOT");
    const char* boundaryValue = std::getenv("TP_STATE_PERSISTENCE_CRASH_BOUNDARY");
    REQUIRE(rootValue != nullptr);
    REQUIRE(boundaryValue != nullptr);

    const std::string boundary = boundaryValue;
    REQUIRE((boundary == "TemporaryVerified" ||
        boundary == "PrimaryMovedToBackup" ||
        boundary == "TemporaryPublished"));

    const auto path = std::filesystem::path(rootValue) / "canonical.bin";
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(
                path, kPersistenceCampaign, BuildPersistentState()) ==
        PartyQuestPersistenceStatus::Success);

    StatePersistenceCrashBoundary crash{
        boundary == "TemporaryVerified"
            ? PartyQuestStatePersistenceBoundary::TemporaryVerified
            : boundary == "PrimaryMovedToBackup"
                ? PartyQuestStatePersistenceBoundary::PrimaryMovedToBackup
                : PartyQuestStatePersistenceBoundary::TemporaryPublished};
    const auto status = PartyQuestStatePersistence::SaveAtomically(
        path,
        kPersistenceCampaign,
        BuildNewerPersistentState(),
        {CrashStatePersistenceAtBoundary, &crash});
    FAIL("Crash boundary returned with status " << static_cast<int>(status));
}

TEST_CASE("Party quest persistence round-trips checkpoint and journal", "[quest.party-state.persistence]")
{
    PartyQuestState original = BuildPersistentState();
    const auto firstEncoding = PartyQuestStatePersistence::Encode(
        kPersistenceCampaign, original);
    const auto secondEncoding = PartyQuestStatePersistence::Encode(
        kPersistenceCampaign, original);

    REQUIRE(firstEncoding == secondEncoding);

    auto loaded = PartyQuestStatePersistence::Decode(firstEncoding);
    REQUIRE(loaded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(loaded.CampaignId == kPersistenceCampaign);
    REQUIRE(loaded.State.has_value());
    REQUIRE_FALSE(loaded.UsedBackup);
    REQUIRE(loaded.State->GetWorldRevision() == original.GetWorldRevision());
    REQUIRE(loaded.State->GetQuestCount() == original.GetQuestCount());
    REQUIRE(loaded.State->GetJournal() == original.GetJournal());

    for (const auto& [questId, snapshot] : original.GetQuests())
    {
        const QuestSnapshot* pLoadedSnapshot = loaded.State->FindQuest(questId);
        REQUIRE(pLoadedSnapshot != nullptr);
        REQUIRE(*pLoadedSnapshot == snapshot);
    }

    const QuestSnapshot* pFirstQuest = loaded.State->FindQuest(GameId(1, 0x1000));
    REQUIRE(pFirstQuest != nullptr);
    REQUIRE(pFirstQuest->InitiatorPlayerId == 5);

    const auto duplicate = loaded.State->Apply(original.GetJournal().back().Transaction);
    REQUIRE(duplicate.Status == PartyQuestApplyStatus::Duplicate);
    REQUIRE(duplicate.WorldRevision == original.GetWorldRevision());
}

TEST_CASE("Party quest persistence rejects corrupted and truncated archives", "[quest.party-state.persistence]")
{
    REQUIRE(PartyQuestStatePersistence::Encode({}, BuildPersistentState()).empty());

    const auto encoded = PartyQuestStatePersistence::Encode(
        kPersistenceCampaign, BuildPersistentState());

    auto corrupted = encoded;
    REQUIRE(corrupted.size() > 20);
    corrupted[20] ^= 0x5A;
    REQUIRE(PartyQuestStatePersistence::Decode(corrupted).Status == PartyQuestPersistenceStatus::ChecksumMismatch);

    auto truncated = encoded;
    truncated.pop_back();
    REQUIRE(PartyQuestStatePersistence::Decode(truncated).Status == PartyQuestPersistenceStatus::Truncated);

    auto unsupported = encoded;
    unsupported[8] = 0xFF;
    unsupported[9] = 0x7F;
    REQUIRE(PartyQuestStatePersistence::Decode(unsupported).Status == PartyQuestPersistenceStatus::UnsupportedVersion);

    std::vector<uint8_t> oversized(
        PartyQuestDurableResourcePolicy::MaxCanonicalStateArchiveBytes + 1);
    REQUIRE(PartyQuestStatePersistence::Decode(oversized).Status ==
        PartyQuestPersistenceStatus::InvalidData);
}

TEST_CASE("Canonical state backup is never silently promoted", "[quest.party-state.persistence]")
{
    const auto uniqueSuffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("tp_party_quest_state_" + std::to_string(uniqueSuffix) + ".bin");
    RemoveArchiveFiles(path);

    PartyQuestState state = BuildPersistentState();
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(
                path, kPersistenceCampaign, state) ==
        PartyQuestPersistenceStatus::Success);

    auto firstLoad = PartyQuestStatePersistence::Load(path);
    REQUIRE(firstLoad.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(firstLoad.State.has_value());
    REQUIRE(firstLoad.State->GetWorldRevision() == 3);
    REQUIRE_FALSE(firstLoad.UsedBackup);

    REQUIRE(state.Apply(BuildPersistentTransaction(10004, GameId(2, 0x2000), 1, 50, 9)).Status == PartyQuestApplyStatus::Accepted);
    REQUIRE(PartyQuestStatePersistence::SaveAtomically(
                path, kPersistenceCampaign, state) ==
        PartyQuestPersistenceStatus::Success);

    {
        std::ofstream corruptPrimary(path, std::ios::binary | std::ios::trunc);
        REQUIRE(corruptPrimary.is_open());
        corruptPrimary.write("broken", 6);
    }

    auto recovered = PartyQuestStatePersistence::Load(path);
    REQUIRE(recovered.Status == PartyQuestPersistenceStatus::BackupRecoveryRequired);
    REQUIRE(recovered.CampaignId == kPersistenceCampaign);
    REQUIRE(recovered.State.has_value());
    REQUIRE(recovered.UsedBackup);
    REQUIRE(recovered.State->GetWorldRevision() == 3);

    RemoveArchiveFiles(path);
}

TEST_CASE("Legacy canonical state decodes unbound and rewrites as campaign-bound v2", "[quest.party-state.persistence][migration]")
{
    const auto state = BuildPersistentState();
    const auto legacy = MakeLegacyUnboundArchive(state);
    const auto decodedLegacy = PartyQuestStatePersistence::Decode(legacy);
    REQUIRE(decodedLegacy.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE_FALSE(decodedLegacy.CampaignId.has_value());
    REQUIRE(decodedLegacy.State.has_value());
    REQUIRE(decodedLegacy.State->GetWorldRevision() == state.GetWorldRevision());

    const auto migrated = PartyQuestStatePersistence::Decode(
        PartyQuestStatePersistence::Encode(kPersistenceCampaign, *decodedLegacy.State));
    REQUIRE(migrated.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(migrated.CampaignId == kPersistenceCampaign);
    REQUIRE(migrated.State.has_value());
    REQUIRE(migrated.State->GetJournal() == state.GetJournal());
}

TEST_CASE("Canonical state archive exposes exact campaign binding for bootstrap validation", "[quest.party-state.persistence][campaign-id]")
{
    const PartyQuestCampaignId otherCampaign{0xAAAAAAAAAAAAAAAAull, 0xBBBBBBBBBBBBBBBBull};
    const auto decoded = PartyQuestStatePersistence::Decode(
        PartyQuestStatePersistence::Encode(kPersistenceCampaign, BuildPersistentState()));
    REQUIRE(decoded.Status == PartyQuestPersistenceStatus::Success);
    REQUIRE(decoded.CampaignId == kPersistenceCampaign);
    REQUIRE(decoded.CampaignId != otherCampaign);
}

TEST_CASE("Canonical state publication windows survive abrupt process termination", "[quest.party-state.persistence][fault][process]")
{
    const auto runBoundary = [](const char* apBoundary)
    {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_state_crash_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        REQUIRE_FALSE(ec);
        RunStatePersistenceCrashProcess(root, apBoundary);
        std::filesystem::remove_all(root, ec);
        REQUIRE_FALSE(ec);
    };

    SECTION("verified temporary leaves the prior primary authoritative")
    {
        runBoundary("TemporaryVerified");
    }
    SECTION("primary moved aside exposes the complete newer temporary state")
    {
        runBoundary("PrimaryMovedToBackup");
    }
    SECTION("published primary exposes the complete newer state")
    {
        runBoundary("TemporaryPublished");
    }
}
