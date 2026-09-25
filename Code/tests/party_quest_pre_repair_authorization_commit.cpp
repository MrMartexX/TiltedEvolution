#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestPreRepairAuthorizationCommit.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
const PartyQuestCampaignId kCampaign{
    0x1111222233334444ull,
    0xAAAABBBBCCCCDDDDull};
const PartyQuestPlayerProfileId kPlayer{
    0x1011121314151617ull,
    0x2122232425262728ull};

constexpr size_t kVersionOffset = 8;
constexpr size_t kPayloadSizeOffset = 10;
constexpr size_t kPayloadOffset = 18;
constexpr size_t kActionsPayloadOffset = 88;
constexpr size_t kVerificationSchemaPayloadOffset = 92;
constexpr size_t kVerificationRequiredPayloadOffset = 94;
constexpr size_t kVerificationQuestDigestPayloadOffset = 98;
constexpr size_t kFileCountPayloadOffset = 146;
constexpr size_t kFirstFilePayloadOffset = 150;
constexpr size_t kFileKindWidth = 1;
constexpr size_t kPathLengthWidth = 4;
constexpr size_t kFileTailWidth = 16;

uint64_t ReadLittleEndian(
    const std::vector<uint8_t>& acBytes,
    size_t aOffset,
    size_t aWidth)
{
    REQUIRE(aOffset <= acBytes.size());
    REQUIRE(aWidth <= acBytes.size() - aOffset);

    uint64_t value{};
    for (size_t i = 0; i < aWidth; ++i)
        value |= static_cast<uint64_t>(acBytes[aOffset + i]) << (i * 8);
    return value;
}

void WriteLittleEndian(
    std::vector<uint8_t>& aBytes,
    size_t aOffset,
    uint64_t aValue,
    size_t aWidth)
{
    REQUIRE(aOffset <= aBytes.size());
    REQUIRE(aWidth <= aBytes.size() - aOffset);
    for (size_t i = 0; i < aWidth; ++i)
        aBytes[aOffset + i] =
            static_cast<uint8_t>((aValue >> (i * 8)) & 0xFF);
}

uint64_t CommitChecksum(const uint8_t* apData, size_t aSize)
{
    uint64_t checksum = 14695981039346656037ull;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= 1099511628211ull;
    }
    return checksum;
}

void RefreshChecksum(std::vector<uint8_t>& aBytes)
{
    const uint64_t payloadSize =
        ReadLittleEndian(aBytes, kPayloadSizeOffset, sizeof(uint64_t));
    REQUIRE(payloadSize <= aBytes.size());
    const size_t payloadEnd =
        kPayloadOffset + static_cast<size_t>(payloadSize);
    REQUIRE(payloadEnd + sizeof(uint64_t) == aBytes.size());

    WriteLittleEndian(
        aBytes,
        payloadEnd,
        CommitChecksum(
            aBytes.data() + kPayloadOffset,
            static_cast<size_t>(payloadSize)),
        sizeof(uint64_t));
}

size_t FileEntryOffset(
    const std::vector<uint8_t>& acBytes,
    size_t aIndex)
{
    const uint32_t fileCount = static_cast<uint32_t>(
        ReadLittleEndian(
            acBytes,
            kPayloadOffset + kFileCountPayloadOffset,
            sizeof(uint32_t)));
    REQUIRE(aIndex < fileCount);

    size_t offset = kPayloadOffset + kFirstFilePayloadOffset;
    for (size_t index = 0; index < aIndex; ++index)
    {
        REQUIRE(offset + kFileKindWidth + kPathLengthWidth <= acBytes.size());
        const uint32_t pathLength = static_cast<uint32_t>(
            ReadLittleEndian(
                acBytes,
                offset + kFileKindWidth,
                kPathLengthWidth));
        offset += kFileKindWidth +
            kPathLengthWidth +
            pathLength +
            kFileTailWidth;
    }
    return offset;
}

std::string FilePathAt(
    const std::vector<uint8_t>& acBytes,
    size_t aIndex)
{
    const size_t entry = FileEntryOffset(acBytes, aIndex);
    const uint32_t pathLength = static_cast<uint32_t>(
        ReadLittleEndian(
            acBytes,
            entry + kFileKindWidth,
            kPathLengthWidth));
    const size_t pathOffset =
        entry + kFileKindWidth + kPathLengthWidth;
    REQUIRE(pathOffset + pathLength <= acBytes.size());
    return std::string(
        reinterpret_cast<const char*>(acBytes.data() + pathOffset),
        pathLength);
}

std::vector<uint8_t> PatchFilePath(
    const std::vector<uint8_t>& acBytes,
    size_t aIndex,
    const std::string& acNewPath)
{
    const size_t entry = FileEntryOffset(acBytes, aIndex);
    const size_t lengthOffset = entry + kFileKindWidth;
    const uint32_t oldLength = static_cast<uint32_t>(
        ReadLittleEndian(acBytes, lengthOffset, kPathLengthWidth));
    const size_t oldPathOffset = lengthOffset + kPathLengthWidth;
    const uint64_t oldPayloadSize =
        ReadLittleEndian(acBytes, kPayloadSizeOffset, sizeof(uint64_t));
    const size_t oldPayloadEnd =
        kPayloadOffset + static_cast<size_t>(oldPayloadSize);

    REQUIRE(oldPathOffset + oldLength <= oldPayloadEnd);

    std::vector<uint8_t> patched;
    patched.reserve(
        acBytes.size() - oldLength + acNewPath.size());
    patched.insert(
        patched.end(),
        acBytes.begin(),
        acBytes.begin() + static_cast<std::ptrdiff_t>(oldPathOffset));
    patched.insert(
        patched.end(),
        acNewPath.begin(),
        acNewPath.end());
    patched.insert(
        patched.end(),
        acBytes.begin() +
            static_cast<std::ptrdiff_t>(oldPathOffset + oldLength),
        acBytes.begin() + static_cast<std::ptrdiff_t>(oldPayloadEnd));
    patched.resize(patched.size() + sizeof(uint64_t), 0);

    const uint64_t newPayloadSize =
        oldPayloadSize - oldLength + acNewPath.size();
    WriteLittleEndian(
        patched,
        kPayloadSizeOffset,
        newPayloadSize,
        sizeof(uint64_t));
    WriteLittleEndian(
        patched,
        lengthOffset,
        acNewPath.size(),
        kPathLengthWidth);
    RefreshChecksum(patched);
    return patched;
}

PartyQuestPreRepairAuthorizationCommit BuildCommit()
{
    PartyQuestPreRepairAuthorizationCommit commit;
    commit.CampaignId = kCampaign;
    commit.PlayerProfileId = kPlayer;
    commit.TransactionId = 70001;
    commit.RuntimeGeneration = 41;
    commit.CaptureEpochId = 0xAABBCCDD11223344ull;
    commit.TargetWorldRevision = 1901;
    commit.QuestId = GameId(0x17, 0x12345);
    commit.CanonicalDigest = 0x11112222AAAABBBBull;
    commit.SidecarManifestFingerprint = 0x33334444CCCCDDDDull;
    commit.Actions =
        PartyQuestApplyAction::StageTransition |
        PartyQuestApplyAction::WaitForPapyrusQuiescence |
        PartyQuestApplyAction::ResnapshotAndVerify;

    const auto expected = PartyQuestVerificationPolicy::BuildExpected(
        commit.Actions,
        commit.CanonicalDigest,
        0x5152535455565758ull);
    REQUIRE(expected.has_value());
    commit.ExpectedVerification = *expected;

    commit.Files = {
        {
            PartyQuestReplicaFileKind::SkyrimSave,
            std::filesystem::path("saves") / "Hero.ess",
            1024,
            0x1010101010101010ull},
        {
            PartyQuestReplicaFileKind::SkseCosave,
            std::filesystem::path("saves") / "Hero.skse",
            512,
            0x2020202020202020ull},
        {
            PartyQuestReplicaFileKind::ExternalSidecar,
            std::filesystem::path("sidecars") / "external" / "Plugin" / "Hero.dat",
            64,
            0x3030303030303030ull}};
    return commit;
}

struct CommitSandbox
{
    std::filesystem::path Root;

    CommitSandbox()
    {
        static std::atomic<uint64_t> counter{0};
        const auto nonce =
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count();
        Root =
            std::filesystem::temp_directory_path() /
            ("tp_pre_repair_authorization_commit_" +
                std::to_string(nonce) + "_" +
                std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        ec.clear();
        std::filesystem::create_directories(Root, ec);
        REQUIRE_FALSE(ec);
    }

    ~CommitSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
};

PartyQuestCoopSavePaths BuildPaths(const CommitSandbox& acSandbox)
{
    const auto paths = PartyQuestCoopSaveLayout::Build(
        acSandbox.Root / "CoopCampaigns",
        kCampaign,
        kPlayer);
    REQUIRE(paths.has_value());
    return *paths;
}

void WriteRaw(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes)
{
    std::error_code ec;
    std::filesystem::create_directories(acPath.parent_path(), ec);
    REQUIRE_FALSE(ec);

    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    if (!acBytes.empty())
    {
        file.write(
            reinterpret_cast<const char*>(acBytes.data()),
            static_cast<std::streamsize>(acBytes.size()));
    }
    file.flush();
    REQUIRE(file.good());
}

std::vector<uint8_t> ReadRaw(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    REQUIRE(file.is_open());
    const auto end = file.tellg();
    REQUIRE(end >= 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.good());
    }
    return bytes;
}

struct FaultBoundary
{
    PartyQuestPreRepairAuthorizationCommitBoundary Boundary;
    size_t Calls{};
};

PartyQuestPreRepairAuthorizationCommitDirective FailAtBoundary(
    PartyQuestPreRepairAuthorizationCommitBoundary aBoundary,
    void* apContext) noexcept
{
    auto& fault = *static_cast<FaultBoundary*>(apContext);
    ++fault.Calls;
    return aBoundary == fault.Boundary
        ? PartyQuestPreRepairAuthorizationCommitDirective::FailClosed
        : PartyQuestPreRepairAuthorizationCommitDirective::Continue;
}
} // namespace

TEST_CASE(
    "PreRepair authorization commit V1 is deterministic and round-trips exact runtime identity",
    "[quest.party-state.pre-repair-authorization][codec]")
{
    const auto commit = BuildCommit();

    const auto encoded =
        PartyQuestPreRepairAuthorizationCommitStore::Encode(commit);
    REQUIRE_FALSE(encoded.empty());
    REQUIRE(encoded.size() <=
        PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes);

    const std::array<uint8_t, 8> expectedMagic{
        'T', 'P', 'Q', 'P', 'R', 'A', 'C', 'M'};
    REQUIRE(std::equal(
        expectedMagic.begin(),
        expectedMagic.end(),
        encoded.begin()));

    const auto decoded =
        PartyQuestPreRepairAuthorizationCommitStore::Decode(encoded);
    REQUIRE(decoded.Status ==
        PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success);
    REQUIRE(decoded.Record.has_value());
    REQUIRE(*decoded.Record == commit);

    auto reordered = commit;
    std::reverse(reordered.Files.begin(), reordered.Files.end());
    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::Encode(reordered) ==
        encoded);

    CommitSandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
            paths,
            commit.TargetWorldRevision) ==
        PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
            paths,
            PartyQuestCheckpointKind::PreRepair,
            commit.TargetWorldRevision) /
            "pre_repair_commit.bin");
}

TEST_CASE(
    "PreRepair authorization commit codec rejects corruption truncation version and hostile archive lengths",
    "[quest.party-state.pre-repair-authorization][codec][fail-closed]")
{
    const auto encoded =
        PartyQuestPreRepairAuthorizationCommitStore::Encode(BuildCommit());
    REQUIRE_FALSE(encoded.empty());

    SECTION("bad magic")
    {
        auto bytes = encoded;
        bytes[0] ^= 0x5A;
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidMagic);
    }

    SECTION("unsupported version")
    {
        auto bytes = encoded;
        WriteLittleEndian(bytes, kVersionOffset, 2, sizeof(uint16_t));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                UnsupportedVersion);
    }

    SECTION("checksum mismatch")
    {
        auto bytes = encoded;
        bytes[kPayloadOffset + 4] ^= 0x01;
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ChecksumMismatch);
    }

    SECTION("truncation boundaries")
    {
        const std::vector<size_t> sizes{
            0,
            1,
            7,
            8,
            17,
            18,
            encoded.size() - 1};
        for (const size_t size : sizes)
        {
            INFO("truncated size " << size);
            std::vector<uint8_t> bytes(
                encoded.begin(),
                encoded.begin() + static_cast<std::ptrdiff_t>(size));
            REQUIRE(
                PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Truncated);
        }
    }

    SECTION("payload length resource overflow")
    {
        auto bytes = encoded;
        WriteLittleEndian(
            bytes,
            kPayloadSizeOffset,
            std::numeric_limits<uint64_t>::max(),
            sizeof(uint64_t));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded);
    }

    SECTION("payload length mismatch and trailing bytes")
    {
        auto shortDeclared = encoded;
        const uint64_t payloadSize = ReadLittleEndian(
            shortDeclared,
            kPayloadSizeOffset,
            sizeof(uint64_t));
        REQUIRE(payloadSize > 0);
        WriteLittleEndian(
            shortDeclared,
            kPayloadSizeOffset,
            payloadSize - 1,
            sizeof(uint64_t));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                shortDeclared).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);

        auto trailing = encoded;
        trailing.push_back(0x7E);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(trailing).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);
    }

    SECTION("archive cap")
    {
        std::vector<uint8_t> oversized(
            PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes + 1,
            0);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                oversized).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded);
    }
}

TEST_CASE(
    "PreRepair authorization commit codec enforces allocation and file resource bounds before accepting payload",
    "[quest.party-state.pre-repair-authorization][codec][resource-budget]")
{
    const auto encoded =
        PartyQuestPreRepairAuthorizationCommitStore::Encode(BuildCommit());
    REQUIRE_FALSE(encoded.empty());

    SECTION("file count is bounded before reserve")
    {
        auto bytes = encoded;
        WriteLittleEndian(
            bytes,
            kPayloadOffset + kFileCountPayloadOffset,
            PartyQuestReplicaResourcePolicy::MaxFiles + 1,
            sizeof(uint32_t));
        RefreshChecksum(bytes);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded);
    }

    SECTION("serialized path length is bounded before string allocation")
    {
        auto bytes = encoded;
        const size_t firstEntry = FileEntryOffset(bytes, 0);
        WriteLittleEndian(
            bytes,
            firstEntry + kFileKindWidth,
            PartyQuestDurableResourcePolicy::MaxSerializedPathBytes + 1,
            sizeof(uint32_t));
        RefreshChecksum(bytes);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(bytes).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                ResourceLimitExceeded);
    }

    SECTION("individual and total file sizes reuse replica policy")
    {
        auto commit = BuildCommit();
        commit.Files[0].Size =
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes + 1;
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit).empty());

        commit = BuildCommit();
        commit.Files[0].Size =
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        commit.Files[1].Size =
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        commit.Files[2].Size =
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes;
        commit.Files.push_back({
            PartyQuestReplicaFileKind::ExternalSidecar,
            "sidecars/external/Plugin/Extra.dat",
            PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes,
            0x4040404040404040ull});
        commit.Files.push_back({
            PartyQuestReplicaFileKind::ExternalSidecar,
            "sidecars/external/Plugin/Extra2.dat",
            1,
            0x5050505050505050ull});
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit).empty());
    }
}

TEST_CASE(
    "PreRepair authorization commit rejects path traversal noncanonical paths duplicates and invalid file kinds",
    "[quest.party-state.pre-repair-authorization][codec][path-confinement]")
{
    const auto base = BuildCommit();

    for (const std::string& invalidPath : {
             std::string("../Hero.ess"),
             std::string("/absolute/Hero.ess"),
             std::string("saves/./Hero.ess"),
             std::string("saves/sub/../Hero.ess"),
             std::string("saves\\..\\Hero.ess"),
             std::string("C:/saves/Hero.ess")})
    {
        INFO("invalid path " << invalidPath);
        auto commit = base;
        commit.Files[0].RelativePath = invalidPath;
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit).empty());
    }

    SECTION("decoder rejects hostile path text with a valid checksum")
    {
        const auto encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);
        for (const std::string& invalidPath : {
                 std::string("../Hero.ess"),
                 std::string("/abs/Hero.ess"),
                 std::string("saves/./Hero.ess"),
                 std::string("saves/sub/../Hero.ess"),
                 std::string("saves\\..\\Hero.ess")})
        {
            INFO("decoded invalid path " << invalidPath);
            const auto patched = PatchFilePath(encoded, 0, invalidPath);
            REQUIRE(
                PartyQuestPreRepairAuthorizationCommitStore::Decode(
                    patched).Status ==
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::
                    InvalidData);
        }
    }

    SECTION("duplicate relative paths are rejected")
    {
        auto commit = base;
        commit.Files.push_back({
            PartyQuestReplicaFileKind::ExternalSidecar,
            "sidecars/external/Plugin/Other.dat",
            32,
            0x4141414141414141ull});
        const auto encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit);
        REQUIRE_FALSE(encoded.empty());

        const std::string firstExternal = FilePathAt(encoded, 2);
        const auto duplicate = PatchFilePath(encoded, 3, firstExternal);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                duplicate).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);
    }

    SECTION("invalid file kind is rejected")
    {
        auto encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);
        const size_t firstEntry = FileEntryOffset(encoded, 0);
        encoded[firstEntry] = 0xFF;
        RefreshChecksum(encoded);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                encoded).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);
    }

    SECTION("exactly one Skyrim save is required")
    {
        auto missing = base;
        missing.Files.erase(missing.Files.begin());
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(missing).empty());

        auto multiple = base;
        multiple.Files.push_back({
            PartyQuestReplicaFileKind::SkyrimSave,
            "saves/Other.ess",
            256,
            0x5151515151515151ull});
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(multiple).empty());
    }

    SECTION("kind-specific paths remain structural")
    {
        auto commit = base;
        commit.Files[0].RelativePath = "sidecars/external/Hero.ess";
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit).empty());

        commit = base;
        commit.Files[2].RelativePath = "sidecars/external";
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit).empty());

        REQUIRE_FALSE(
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base).empty());
    }
}

TEST_CASE(
    "PreRepair authorization commit validates every runtime identity and full verification envelope",
    "[quest.party-state.pre-repair-authorization][codec][identity]")
{
    const auto base = BuildCommit();

    auto invalid = base;
    invalid.CampaignId = {};
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.PlayerProfileId = {};
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.TransactionId = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.RuntimeGeneration = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.CaptureEpochId = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.TargetWorldRevision = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.QuestId = GameId(0, 0);
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.CanonicalDigest = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.SidecarManifestFingerprint = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.Actions = static_cast<PartyQuestApplyAction>(1u << 31);
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.ExpectedVerification.SchemaVersion =
        PartyQuestVerificationEnvelopeV1::kSchemaVersion + 1;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.ExpectedVerification.Required =
        PartyQuestVerificationComponent::QuestSnapshot;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.ExpectedVerification.QuestSnapshotDigest ^= 1;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.ExpectedVerification.AliasDigest = 1;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.ExpectedVerification.CompatibilityFingerprint = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    invalid = base;
    invalid.Files[0].Digest = 0;
    REQUIRE(PartyQuestPreRepairAuthorizationCommitStore::Encode(invalid).empty());

    SECTION("decoder rejects unknown actions and malformed envelope fields")
    {
        auto encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);

        WriteLittleEndian(
            encoded,
            kPayloadOffset + kActionsPayloadOffset,
            1u << 31,
            sizeof(uint32_t));
        RefreshChecksum(encoded);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                encoded).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);

        encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);
        WriteLittleEndian(
            encoded,
            kPayloadOffset + kVerificationSchemaPayloadOffset,
            2,
            sizeof(uint16_t));
        RefreshChecksum(encoded);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                encoded).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);

        encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);
        WriteLittleEndian(
            encoded,
            kPayloadOffset + kVerificationRequiredPayloadOffset,
            static_cast<uint32_t>(
                PartyQuestVerificationComponent::QuestSnapshot),
            sizeof(uint32_t));
        RefreshChecksum(encoded);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                encoded).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);

        encoded =
            PartyQuestPreRepairAuthorizationCommitStore::Encode(base);
        WriteLittleEndian(
            encoded,
            kPayloadOffset + kVerificationQuestDigestPayloadOffset,
            base.CanonicalDigest ^ 1,
            sizeof(uint64_t));
        RefreshChecksum(encoded);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Decode(
                encoded).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::InvalidData);
    }
}

TEST_CASE(
    "PreRepair authorization commit store is immutable idempotent and conflicts on changed identity or files",
    "[quest.party-state.pre-repair-authorization][store][immutable]")
{
    CommitSandbox sandbox;
    const auto paths = BuildPaths(sandbox);
    const auto commit = BuildCommit();
    const auto finalPath =
        PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
            paths,
            commit.TargetWorldRevision);

    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
            paths,
            commit) ==
        PartyQuestPreRepairAuthorizationCommitPublishStatus::Published);

    const auto firstBytes = ReadRaw(finalPath);
    const auto loaded =
        PartyQuestPreRepairAuthorizationCommitStore::Load(finalPath);
    REQUIRE(loaded.Status ==
        PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success);
    REQUIRE(loaded.Record.has_value());
    REQUIRE(*loaded.Record == commit);

    auto backup = finalPath;
    backup += ".bak";
    REQUIRE_FALSE(std::filesystem::exists(backup));

    FaultBoundary observer{
        PartyQuestPreRepairAuthorizationCommitBoundary::DirectoryDurable};
    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
            paths,
            commit,
            {FailAtBoundary, &observer}) ==
        PartyQuestPreRepairAuthorizationCommitPublishStatus::AlreadyCommitted);
    REQUIRE(observer.Calls == 0);
    REQUIRE(ReadRaw(finalPath) == firstBytes);

    auto changedIdentity = commit;
    ++changedIdentity.TransactionId;
    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
            paths,
            changedIdentity) ==
        PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict);
    REQUIRE(ReadRaw(finalPath) == firstBytes);

    auto changedFiles = commit;
    changedFiles.Files[2].Digest ^= 1;
    REQUIRE(
        PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
            paths,
            changedFiles) ==
        PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict);
    REQUIRE(ReadRaw(finalPath) == firstBytes);
}

TEST_CASE(
    "PreRepair authorization authority never adopts temporary or backup siblings",
    "[quest.party-state.pre-repair-authorization][store][authority]")
{
    SECTION("tmp-only is uncommitted but can be overwritten and published")
    {
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();
        const auto finalPath =
            PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
                paths,
                commit.TargetWorldRevision);
        auto temporary = finalPath;
        temporary += ".tmp";

        WriteRaw(
            temporary,
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Load(
                finalPath).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::FileNotFound);

        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit) ==
            PartyQuestPreRepairAuthorizationCommitPublishStatus::Published);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Load(
                finalPath).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success);
    }

    SECTION("bak-only is uncommitted and never promoted")
    {
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();
        const auto finalPath =
            PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
                paths,
                commit.TargetWorldRevision);
        auto backup = finalPath;
        backup += ".bak";

        WriteRaw(
            backup,
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Load(
                finalPath).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::FileNotFound);

        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit) ==
            PartyQuestPreRepairAuthorizationCommitPublishStatus::Published);
        REQUIRE(std::filesystem::exists(backup));
    }

    SECTION("corrupt final is a hard conflict even with a valid tmp")
    {
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();
        const auto finalPath =
            PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
                paths,
                commit.TargetWorldRevision);
        auto temporary = finalPath;
        temporary += ".tmp";

        WriteRaw(finalPath, {0x42, 0x41, 0x44});
        WriteRaw(
            temporary,
            PartyQuestPreRepairAuthorizationCommitStore::Encode(commit));
        const auto corruptBytes = ReadRaw(finalPath);

        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit) ==
            PartyQuestPreRepairAuthorizationCommitPublishStatus::Conflict);
        REQUIRE(ReadRaw(finalPath) == corruptBytes);
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Load(
                finalPath).Status !=
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success);
    }
}

TEST_CASE(
    "PreRepair authorization publication fails closed at every durable boundary and retries safely",
    "[quest.party-state.pre-repair-authorization][store][fault-injection]")
{
    const std::array boundaries{
        PartyQuestPreRepairAuthorizationCommitBoundary::DirectoryDurable,
        PartyQuestPreRepairAuthorizationCommitBoundary::TemporaryDurablyWritten,
        PartyQuestPreRepairAuthorizationCommitBoundary::TemporaryVerified,
        PartyQuestPreRepairAuthorizationCommitBoundary::FinalPublished,
        PartyQuestPreRepairAuthorizationCommitBoundary::FinalVerified};

    for (const auto boundary : boundaries)
    {
        INFO("fault boundary " << static_cast<int>(boundary));
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();
        const auto finalPath =
            PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
                paths,
                commit.TargetWorldRevision);

        FaultBoundary fault{boundary};
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit,
                {FailAtBoundary, &fault}) ==
            PartyQuestPreRepairAuthorizationCommitPublishStatus::Interrupted);
        REQUIRE(fault.Calls != 0);

        const bool finalShouldExist =
            boundary ==
                PartyQuestPreRepairAuthorizationCommitBoundary::FinalPublished ||
            boundary ==
                PartyQuestPreRepairAuthorizationCommitBoundary::FinalVerified;
        const auto afterFault =
            PartyQuestPreRepairAuthorizationCommitStore::Load(finalPath);
        REQUIRE(
            (afterFault.Status ==
                PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success) ==
            finalShouldExist);

        const auto retry =
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit);
        REQUIRE(
            retry ==
            (finalShouldExist
                ? PartyQuestPreRepairAuthorizationCommitPublishStatus::
                      AlreadyCommitted
                : PartyQuestPreRepairAuthorizationCommitPublishStatus::
                      Published));

        const auto final =
            PartyQuestPreRepairAuthorizationCommitStore::Load(finalPath);
        REQUIRE(final.Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::Success);
        REQUIRE(final.Record.has_value());
        REQUIRE(*final.Record == commit);
    }
}

TEST_CASE(
    "PreRepair authorization publication maps namespace and staged-node durability errors to fail-closed statuses",
    "[quest.party-state.pre-repair-authorization][store][durability]")
{
    SECTION("directory tree blocked by a regular node")
    {
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();

        std::error_code ec;
        std::filesystem::create_directories(paths.CheckpointsDirectory, ec);
        REQUIRE_FALSE(ec);
        const auto preRepairRoot =
            PartyQuestCoopSaveLayout::GetCheckpointDirectory(
                paths,
                PartyQuestCheckpointKind::PreRepair);
        WriteRaw(preRepairRoot, {0x01});

        const auto status =
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit);
        REQUIRE(
            (status ==
                PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    StableStorageFailure ||
             status ==
                PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    StableStorageUnsupported));
        REQUIRE(status !=
            PartyQuestPreRepairAuthorizationCommitPublishStatus::Published);
        REQUIRE(status !=
            PartyQuestPreRepairAuthorizationCommitPublishStatus::
                AlreadyCommitted);
    }

    SECTION("staged path that is a directory cannot become authority")
    {
        CommitSandbox sandbox;
        const auto paths = BuildPaths(sandbox);
        const auto commit = BuildCommit();
        const auto finalPath =
            PartyQuestPreRepairAuthorizationCommitStore::GetCommitPath(
                paths,
                commit.TargetWorldRevision);
        auto temporary = finalPath;
        temporary += ".tmp";

        std::error_code ec;
        std::filesystem::create_directories(temporary, ec);
        REQUIRE_FALSE(ec);

        const auto status =
            PartyQuestPreRepairAuthorizationCommitStore::PublishDurably(
                paths,
                commit);
        REQUIRE(
            (status ==
                PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    StableStorageFailure ||
             status ==
                PartyQuestPreRepairAuthorizationCommitPublishStatus::
                    StableStorageUnsupported));
        REQUIRE(
            PartyQuestPreRepairAuthorizationCommitStore::Load(
                finalPath).Status ==
            PartyQuestPreRepairAuthorizationCommitPersistenceStatus::FileNotFound);
    }
}
