#include <Structs/Skyrim/PartyQuestRuntimeRestoreAttempt.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{
constexpr std::array<uint8_t, 8> kStateMagic{'T', 'P', 'Q', 'R', 'A', 'T', 'T', 'M'};
constexpr std::array<uint8_t, 8> kAllocatorMagic{'T', 'P', 'Q', 'R', 'A', 'L', 'O', 'C'};
constexpr uint16_t kStateFormatVersion = 2;
constexpr uint16_t kAllocatorFormatVersion = 1;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kStateArchiveSize =
    kStateMagic.size() + sizeof(uint16_t) + sizeof(uint64_t) * 8 + sizeof(uint32_t);
constexpr size_t kAllocatorArchiveSize =
    kAllocatorMagic.size() + sizeof(uint16_t) + sizeof(uint64_t) * 6;

struct RestoreIdAllocatorState
{
    PartyQuestCampaignId CampaignId;
    PartyQuestPlayerProfileId PlayerProfileId;
    uint64_t NextRestoreId{1};
};

enum class NodeProbe : uint8_t
{
    Missing,
    Occupied,
    Error
};

template <class T>
void WriteInteger(std::vector<uint8_t>& aBytes, T aValue)
{
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    const auto value = static_cast<Unsigned>(aValue);
    for (size_t i = 0; i < sizeof(Unsigned); ++i)
        aBytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}

template <class T>
bool ReadInteger(
    const std::vector<uint8_t>& acBytes,
    size_t& aOffset,
    T& aValue) noexcept
{
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    if (aOffset > acBytes.size() || acBytes.size() - aOffset < sizeof(Unsigned))
        return false;

    Unsigned value{};
    for (size_t i = 0; i < sizeof(Unsigned); ++i)
        value |= static_cast<Unsigned>(acBytes[aOffset + i]) << (i * 8);
    aOffset += sizeof(Unsigned);
    aValue = static_cast<T>(value);
    return true;
}

uint64_t ComputeChecksum(const uint8_t* apData, size_t aSize) noexcept
{
    uint64_t checksum = kFnvOffsetBasis;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= kFnvPrime;
    }
    return checksum;
}

std::string FormatTransactionId(uint64_t aTransactionId)
{
    std::ostringstream stream;
    stream << "RuntimeTransaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aTransactionId;
    return stream.str();
}

std::string FormatRestoreId(uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return stream.str();
}

bool IsValidState(const PartyQuestRuntimeRestoreAttemptState& acState) noexcept
{
    if (!acState.CampaignId.IsValid() ||
        !acState.PlayerProfileId.IsValid() ||
        acState.TransactionId == 0 ||
        acState.CurrentOrdinal >= PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction ||
        acState.CurrentRestoreId == 0)
    {
        return false;
    }

    if (acState.CurrentOrdinal == 0)
        return acState.LastRolledBackRestoreId == 0;

    return acState.LastRolledBackRestoreId != 0 &&
        acState.LastRolledBackRestoreId != acState.CurrentRestoreId;
}

bool IsValidAllocator(const RestoreIdAllocatorState& acState) noexcept
{
    return acState.CampaignId.IsValid() &&
        acState.PlayerProfileId.IsValid() &&
        acState.NextRestoreId != 0;
}

bool MatchesIdentity(
    const PartyQuestRuntimeRestoreAttemptState& acState,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId) noexcept
{
    return IsValidState(acState) &&
        acState.CampaignId == acCampaignId &&
        acState.PlayerProfileId == acPlayerProfileId &&
        acState.TransactionId == aTransactionId;
}

bool MatchesAllocatorIdentity(
    const RestoreIdAllocatorState& acState,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    return IsValidAllocator(acState) &&
        acState.CampaignId == acCampaignId &&
        acState.PlayerProfileId == acPlayerProfileId;
}

bool HasValidContext(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId) noexcept
{
    return acCampaignId.IsValid() &&
        acPlayerProfileId.IsValid() &&
        aTransactionId != 0 &&
        PartyQuestCoopSaveLayout::Matches(acPaths, acCampaignId, acPlayerProfileId);
}

std::filesystem::path GetAllocatorPath(const PartyQuestCoopSavePaths& acPaths) noexcept
{
    try
    {
        return (acPaths.MetadataDirectory / "runtime_restore_attempts" /
            "restore_id_allocator.bin").lexically_normal();
    }
    catch (...)
    {
        return {};
    }
}

PartyQuestRuntimeRestoreAttemptResult MakeResult(
    PartyQuestRuntimeRestoreAttemptStatus aStatus,
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId,
    const PartyQuestRuntimeRestoreAttemptState* apState = nullptr)
{
    PartyQuestRuntimeRestoreAttemptResult result;
    result.Status = aStatus;
    result.StatePath = PartyQuestRuntimeRestoreAttemptStore::GetStatePath(
        acPaths, aTransactionId);
    if (apState)
    {
        result.State = *apState;
        result.RestoreId = apState->CurrentRestoreId;
        result.AttemptDirectory =
            PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
                acPaths, apState->CurrentRestoreId);
        result.JournalPath =
            PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
                acPaths, apState->CurrentRestoreId);
    }
    return result;
}

std::vector<uint8_t> EncodeState(const PartyQuestRuntimeRestoreAttemptState& acState)
{
    if (!IsValidState(acState))
        return {};

    std::vector<uint8_t> bytes;
    bytes.reserve(kStateArchiveSize);
    bytes.insert(bytes.end(), kStateMagic.begin(), kStateMagic.end());
    WriteInteger(bytes, kStateFormatVersion);
    const size_t payloadOffset = bytes.size();
    WriteInteger(bytes, acState.CampaignId.High);
    WriteInteger(bytes, acState.CampaignId.Low);
    WriteInteger(bytes, acState.PlayerProfileId.High);
    WriteInteger(bytes, acState.PlayerProfileId.Low);
    WriteInteger(bytes, acState.TransactionId);
    WriteInteger(bytes, acState.CurrentOrdinal);
    WriteInteger(bytes, acState.CurrentRestoreId);
    WriteInteger(bytes, acState.LastRolledBackRestoreId);
    const uint64_t checksum = ComputeChecksum(
        bytes.data() + payloadOffset,
        bytes.size() - payloadOffset);
    WriteInteger(bytes, checksum);
    return bytes;
}

std::vector<uint8_t> EncodeAllocator(const RestoreIdAllocatorState& acState)
{
    if (!IsValidAllocator(acState))
        return {};

    std::vector<uint8_t> bytes;
    bytes.reserve(kAllocatorArchiveSize);
    bytes.insert(bytes.end(), kAllocatorMagic.begin(), kAllocatorMagic.end());
    WriteInteger(bytes, kAllocatorFormatVersion);
    const size_t payloadOffset = bytes.size();
    WriteInteger(bytes, acState.CampaignId.High);
    WriteInteger(bytes, acState.CampaignId.Low);
    WriteInteger(bytes, acState.PlayerProfileId.High);
    WriteInteger(bytes, acState.PlayerProfileId.Low);
    WriteInteger(bytes, acState.NextRestoreId);
    const uint64_t checksum = ComputeChecksum(
        bytes.data() + payloadOffset,
        bytes.size() - payloadOffset);
    WriteInteger(bytes, checksum);
    return bytes;
}

PartyQuestRuntimeRestoreAttemptStatus DecodeState(
    const std::vector<uint8_t>& acBytes,
    PartyQuestRuntimeRestoreAttemptState& aState) noexcept
{
    if (acBytes.size() != kStateArchiveSize ||
        !std::equal(kStateMagic.begin(), kStateMagic.end(), acBytes.begin()))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    size_t offset = kStateMagic.size();
    uint16_t version{};
    if (!ReadInteger(acBytes, offset, version) || version != kStateFormatVersion)
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    const size_t payloadOffset = offset;
    if (!ReadInteger(acBytes, offset, aState.CampaignId.High) ||
        !ReadInteger(acBytes, offset, aState.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.Low) ||
        !ReadInteger(acBytes, offset, aState.TransactionId) ||
        !ReadInteger(acBytes, offset, aState.CurrentOrdinal) ||
        !ReadInteger(acBytes, offset, aState.CurrentRestoreId) ||
        !ReadInteger(acBytes, offset, aState.LastRolledBackRestoreId))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    const size_t checksumOffset = offset;
    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, offset, storedChecksum) || offset != acBytes.size())
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    const uint64_t computedChecksum = ComputeChecksum(
        acBytes.data() + payloadOffset,
        checksumOffset - payloadOffset);
    if (storedChecksum != computedChecksum || !IsValidState(aState))
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    return PartyQuestRuntimeRestoreAttemptStatus::Success;
}

PartyQuestRuntimeRestoreAttemptStatus DecodeAllocator(
    const std::vector<uint8_t>& acBytes,
    RestoreIdAllocatorState& aState) noexcept
{
    if (acBytes.size() != kAllocatorArchiveSize ||
        !std::equal(kAllocatorMagic.begin(), kAllocatorMagic.end(), acBytes.begin()))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    size_t offset = kAllocatorMagic.size();
    uint16_t version{};
    if (!ReadInteger(acBytes, offset, version) || version != kAllocatorFormatVersion)
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    const size_t payloadOffset = offset;
    if (!ReadInteger(acBytes, offset, aState.CampaignId.High) ||
        !ReadInteger(acBytes, offset, aState.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.Low) ||
        !ReadInteger(acBytes, offset, aState.NextRestoreId))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    const size_t checksumOffset = offset;
    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, offset, storedChecksum) || offset != acBytes.size())
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    const uint64_t computedChecksum = ComputeChecksum(
        acBytes.data() + payloadOffset,
        checksumOffset - payloadOffset);
    if (storedChecksum != computedChecksum || !IsValidAllocator(aState))
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    return PartyQuestRuntimeRestoreAttemptStatus::Success;
}

template <class T, class DecodeFn>
PartyQuestRuntimeRestoreAttemptStatus ReadFixedArchive(
    const std::filesystem::path& acPath,
    size_t aExpectedSize,
    T& aState,
    DecodeFn aDecode) noexcept
{
    try
    {
        std::error_code ec;
        const auto node = std::filesystem::symlink_status(acPath, ec);
        if (node.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory)
        {
            return PartyQuestRuntimeRestoreAttemptStatus::FileNotFound;
        }
        if (ec || std::filesystem::is_symlink(node) ||
            !std::filesystem::is_regular_file(node))
        {
            return PartyQuestRuntimeRestoreAttemptStatus::IoError;
        }

        const auto size = std::filesystem::file_size(acPath, ec);
        if (ec || size != aExpectedSize)
            return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

        std::ifstream file(acPath, std::ios::binary);
        if (!file.is_open())
            return PartyQuestRuntimeRestoreAttemptStatus::IoError;

        std::vector<uint8_t> bytes(aExpectedSize);
        if (!file.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
        {
            return PartyQuestRuntimeRestoreAttemptStatus::IoError;
        }
        return aDecode(bytes, aState);
    }
    catch (...)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::IoError;
    }
}

PartyQuestRuntimeRestoreAttemptStatus ReadStateFile(
    const std::filesystem::path& acPath,
    PartyQuestRuntimeRestoreAttemptState& aState) noexcept
{
    return ReadFixedArchive(
        acPath, kStateArchiveSize, aState, DecodeState);
}

PartyQuestRuntimeRestoreAttemptStatus ReadAllocatorFile(
    const std::filesystem::path& acPath,
    RestoreIdAllocatorState& aState) noexcept
{
    return ReadFixedArchive(
        acPath, kAllocatorArchiveSize, aState, DecodeAllocator);
}

PartyQuestRuntimeRestoreAttemptStatus SaveBytesPowerLossDurably(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes) noexcept
{
#ifdef _WIN32
    (void)acPath;
    (void)acBytes;
    return PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform;
#else
    if (acBytes.empty() ||
        !PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    const auto parent = acPath.parent_path();
    if (PartyQuestStableStorage::EnsureDirectoryTreeDurably(parent) !=
        PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }

    std::filesystem::path temporary;
    try
    {
        temporary = acPath;
        temporary += ".tmp";
    }
    catch (...)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(temporary))
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    if (PartyQuestStableStorage::WriteFileDurably(
            temporary, acBytes.data(), acBytes.size()) !=
        PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }

    if (PartyQuestStableStorage::PublishFileRename(
            temporary, acPath, true) != PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }
    return PartyQuestRuntimeRestoreAttemptStatus::Success;
#endif
}

PartyQuestRuntimeRestoreAttemptStatus SaveStatePowerLossDurably(
    const std::filesystem::path& acStatePath,
    const PartyQuestRuntimeRestoreAttemptState& acState) noexcept
{
    return SaveBytesPowerLossDurably(acStatePath, EncodeState(acState));
}

PartyQuestRuntimeRestoreAttemptStatus SaveAllocatorPowerLossDurably(
    const std::filesystem::path& acPath,
    const RestoreIdAllocatorState& acState) noexcept
{
    return SaveBytesPowerLossDurably(acPath, EncodeAllocator(acState));
}

NodeProbe ProbeNode(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto node = std::filesystem::symlink_status(acPath, ec);
        if (node.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory)
        {
            return NodeProbe::Missing;
        }
        if (ec)
            return NodeProbe::Error;
        return NodeProbe::Occupied;
    }
    catch (...)
    {
        return NodeProbe::Error;
    }
}

PartyQuestRuntimeRestoreAttemptStatus RecoverTemporaryArchive(
    const std::filesystem::path& acPrimary,
    const std::filesystem::path& acTemporary) noexcept
{
#ifdef _WIN32
    (void)acPrimary;
    (void)acTemporary;
    return PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform;
#else
    if (PartyQuestStableStorage::PublishFileRename(
            acTemporary, acPrimary, true) != PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }
    return PartyQuestRuntimeRestoreAttemptStatus::Success;
#endif
}

PartyQuestRuntimeRestoreAttemptStatus LoadOrRecoverAllocator(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    RestoreIdAllocatorState& aState) noexcept
{
    const auto allocatorPath = GetAllocatorPath(acPaths);
    if (allocatorPath.empty() ||
        !PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(allocatorPath))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    auto status = ReadAllocatorFile(allocatorPath, aState);
    if (status == PartyQuestRuntimeRestoreAttemptStatus::Success)
    {
        return MatchesAllocatorIdentity(aState, acCampaignId, acPlayerProfileId)
            ? status
            : PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity;
    }
    if (status != PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        return status;

    std::filesystem::path temporary;
    try
    {
        temporary = allocatorPath;
        temporary += ".tmp";
    }
    catch (...)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    RestoreIdAllocatorState recovered;
    const auto temporaryStatus = ReadAllocatorFile(temporary, recovered);
    if (temporaryStatus == PartyQuestRuntimeRestoreAttemptStatus::Success)
    {
        if (!MatchesAllocatorIdentity(
                recovered, acCampaignId, acPlayerProfileId))
        {
            return PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity;
        }
        const auto recovery = RecoverTemporaryArchive(allocatorPath, temporary);
        if (recovery != PartyQuestRuntimeRestoreAttemptStatus::Success)
            return recovery;
        aState = recovered;
        return PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization;
    }
    if (temporaryStatus != PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        return temporaryStatus;

    aState.CampaignId = acCampaignId;
    aState.PlayerProfileId = acPlayerProfileId;
    aState.NextRestoreId = 1;
    return PartyQuestRuntimeRestoreAttemptStatus::Created;
}

std::pair<PartyQuestRuntimeRestoreAttemptStatus, uint64_t> AllocateRestoreIdPowerLossDurably(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
#ifdef _WIN32
    (void)acPaths;
    (void)acCampaignId;
    (void)acPlayerProfileId;
    return {PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform, 0};
#else
    RestoreIdAllocatorState allocator;
    const auto loaded = LoadOrRecoverAllocator(
        acPaths, acCampaignId, acPlayerProfileId, allocator);
    if (loaded != PartyQuestRuntimeRestoreAttemptStatus::Success &&
        loaded != PartyQuestRuntimeRestoreAttemptStatus::Created &&
        loaded != PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization)
    {
        return {loaded, 0};
    }

    uint64_t candidate = allocator.NextRestoreId;
    for (uint32_t probe = 0;
         probe < PartyQuestRuntimeRestoreAttemptStore::MaxRestoreIdAllocationProbe;
         ++probe)
    {
        if (candidate == 0 || candidate == std::numeric_limits<uint64_t>::max())
        {
            return {
                PartyQuestRuntimeRestoreAttemptStatus::RestoreIdAllocationLimitReached,
                0};
        }

        const auto transactionDirectory =
            PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
                acPaths, candidate);
        if (transactionDirectory.empty() ||
            !PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(
                transactionDirectory))
        {
            return {PartyQuestRuntimeRestoreAttemptStatus::InvalidData, 0};
        }

        const NodeProbe node = ProbeNode(transactionDirectory);
        if (node == NodeProbe::Error)
            return {PartyQuestRuntimeRestoreAttemptStatus::IoError, 0};
        if (node == NodeProbe::Occupied)
        {
            ++candidate;
            continue;
        }

        RestoreIdAllocatorState advanced = allocator;
        advanced.NextRestoreId = candidate + 1;
        const auto allocatorPath = GetAllocatorPath(acPaths);
        const auto persisted = SaveAllocatorPowerLossDurably(
            allocatorPath, advanced);
        if (persisted != PartyQuestRuntimeRestoreAttemptStatus::Success)
            return {persisted, 0};

        // The next-id barrier is intentionally durable before this id is
        // returned to the transaction mapping. A crash after this point can
        // leak candidate, but no later allocation can reuse it.
        return {PartyQuestRuntimeRestoreAttemptStatus::Success, candidate};
    }

    return {
        PartyQuestRuntimeRestoreAttemptStatus::RestoreIdAllocationLimitReached,
        0};
#endif
}

bool JournalMatchesRestoreId(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aRestoreId,
    const PartyQuestReplicaRestoreJournalState& acJournal) noexcept
{
    if (aRestoreId == 0 ||
        acJournal.Phase != PartyQuestReplicaRestoreJournalPhase::RolledBack ||
        acJournal.CampaignId != acCampaignId ||
        acJournal.PlayerProfileId != acPlayerProfileId ||
        acJournal.RestoreId != aRestoreId)
    {
        return false;
    }

    const auto expectedDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
            acPaths, aRestoreId);
    return !expectedDirectory.empty() &&
        acJournal.TransactionDirectory.lexically_normal() ==
            expectedDirectory.lexically_normal();
}

PartyQuestRuntimeRestoreAttemptStatus VerifyTerminalRollback(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aRestoreId) noexcept
{
    const auto journalPath = PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
        acPaths, aRestoreId);
    const auto journal =
        PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(journalPath);
    if (journal.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        return PartyQuestRuntimeRestoreAttemptStatus::FileNotFound;
    if (journal.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
        !journal.State ||
        !JournalMatchesRestoreId(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            aRestoreId,
            *journal.State))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch;
    }
    return PartyQuestRuntimeRestoreAttemptStatus::Success;
}
} // namespace

std::filesystem::path PartyQuestRuntimeRestoreAttemptStore::GetStatePath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId) noexcept
{
    if (aTransactionId == 0)
        return {};
    try
    {
        return (acPaths.MetadataDirectory / "runtime_restore_attempts" /
            (FormatTransactionId(aTransactionId) + ".bin")).lexically_normal();
    }
    catch (...)
    {
        return {};
    }
}

std::filesystem::path PartyQuestRuntimeRestoreAttemptStore::GetRestoreDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId) noexcept
{
    if (aRestoreId == 0)
        return {};
    try
    {
        return (acPaths.MetadataDirectory / "restore" /
            FormatRestoreId(aRestoreId)).lexically_normal();
    }
    catch (...)
    {
        return {};
    }
}

std::filesystem::path PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aRestoreId) noexcept
{
    const auto directory = GetRestoreDirectory(acPaths, aRestoreId);
    if (directory.empty())
        return {};
    try
    {
        return (directory / "journal.bin").lexically_normal();
    }
    catch (...)
    {
        return {};
    }
}

PartyQuestRuntimeRestoreAttemptResult PartyQuestRuntimeRestoreAttemptStore::Load(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId) noexcept
{
    if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity, acPaths, aTransactionId);
    if (aTransactionId == 0)
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidTransactionId, acPaths, aTransactionId);
    if (!PartyQuestCoopSaveLayout::Matches(
            acPaths, acCampaignId, acPlayerProfileId))
    {
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidLayout, acPaths, aTransactionId);
    }

    const auto statePath = GetStatePath(acPaths, aTransactionId);
    if (statePath.empty() ||
        !PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(statePath))
    {
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidData, acPaths, aTransactionId);
    }

    PartyQuestRuntimeRestoreAttemptState state;
    const auto status = ReadStateFile(statePath, state);
    if (status != PartyQuestRuntimeRestoreAttemptStatus::Success)
        return MakeResult(status, acPaths, aTransactionId);
    if (!MatchesIdentity(state, acCampaignId, acPlayerProfileId, aTransactionId))
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity, acPaths, aTransactionId);

    return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::Success, acPaths, aTransactionId, &state);
}

PartyQuestRuntimeRestoreAttemptResult
PartyQuestRuntimeRestoreAttemptStore::EnsureInitializedAuthorized(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId,
    const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept
{
    if (!HasValidContext(acPaths, acCampaignId, acPlayerProfileId, aTransactionId))
    {
        if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity, acPaths, aTransactionId);
        if (aTransactionId == 0)
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidTransactionId, acPaths, aTransactionId);
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidLayout, acPaths, aTransactionId);
    }
    if (!acWorkspaceCapability.Protects(
            acPaths, acCampaignId, acPlayerProfileId))
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::WorkspaceCapabilityRequired,
            acPaths,
            aTransactionId);
    }

#ifdef _WIN32
    return MakeResult(
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform,
        acPaths,
        aTransactionId);
#else
    auto loaded = Load(acPaths, acCampaignId, acPlayerProfileId, aTransactionId);
    if (loaded.Status == PartyQuestRuntimeRestoreAttemptStatus::Success)
        return loaded;
    if (loaded.Status != PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        return loaded;

    const auto statePath = GetStatePath(acPaths, aTransactionId);
    std::filesystem::path temporary;
    try
    {
        temporary = statePath;
        temporary += ".tmp";
    }
    catch (...)
    {
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidData, acPaths, aTransactionId);
    }

    PartyQuestRuntimeRestoreAttemptState recovered;
    const auto temporaryStatus = ReadStateFile(temporary, recovered);
    if (temporaryStatus == PartyQuestRuntimeRestoreAttemptStatus::Success)
    {
        if (!MatchesIdentity(recovered, acCampaignId, acPlayerProfileId, aTransactionId))
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity, acPaths, aTransactionId);
        const auto recovery = RecoverTemporaryArchive(statePath, temporary);
        if (recovery != PartyQuestRuntimeRestoreAttemptStatus::Success)
            return MakeResult(recovery, acPaths, aTransactionId);
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization,
            acPaths,
            aTransactionId,
            &recovered);
    }
    if (temporaryStatus != PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        return MakeResult(temporaryStatus, acPaths, aTransactionId);

    const auto allocation = AllocateRestoreIdPowerLossDurably(
        acPaths, acCampaignId, acPlayerProfileId);
    if (allocation.first != PartyQuestRuntimeRestoreAttemptStatus::Success ||
        allocation.second == 0)
    {
        return MakeResult(allocation.first, acPaths, aTransactionId);
    }

    PartyQuestRuntimeRestoreAttemptState initial;
    initial.CampaignId = acCampaignId;
    initial.PlayerProfileId = acPlayerProfileId;
    initial.TransactionId = aTransactionId;
    initial.CurrentOrdinal = 0;
    initial.CurrentRestoreId = allocation.second;
    initial.LastRolledBackRestoreId = 0;
    const auto saveStatus = SaveStatePowerLossDurably(statePath, initial);
    if (saveStatus != PartyQuestRuntimeRestoreAttemptStatus::Success)
        return MakeResult(saveStatus, acPaths, aTransactionId);

    return MakeResult(
        PartyQuestRuntimeRestoreAttemptStatus::Created,
        acPaths,
        aTransactionId,
        &initial);
#endif
}

PartyQuestRuntimeRestoreAttemptResult
PartyQuestRuntimeRestoreAttemptStore::AdvanceAfterRolledBackAuthorized(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId,
    uint32_t aRolledBackOrdinal,
    const PartyQuestReplicaWorkspacePublicationCapability& acWorkspaceCapability) noexcept
{
    if (!HasValidContext(acPaths, acCampaignId, acPlayerProfileId, aTransactionId))
    {
        if (!acCampaignId.IsValid() || !acPlayerProfileId.IsValid())
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidIdentity, acPaths, aTransactionId);
        if (aTransactionId == 0)
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidTransactionId, acPaths, aTransactionId);
        return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidLayout, acPaths, aTransactionId);
    }
    if (aRolledBackOrdinal >= MaxAttemptsPerTransaction)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::InvalidOrdinal,
            acPaths,
            aTransactionId);
    }
    if (!acWorkspaceCapability.Protects(
            acPaths, acCampaignId, acPlayerProfileId))
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::WorkspaceCapabilityRequired,
            acPaths,
            aTransactionId);
    }

#ifdef _WIN32
    return MakeResult(
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform,
        acPaths,
        aTransactionId);
#else
    auto loaded = Load(acPaths, acCampaignId, acPlayerProfileId, aTransactionId);
    if (loaded.Status != PartyQuestRuntimeRestoreAttemptStatus::Success || !loaded.State)
        return loaded;

    const uint32_t current = loaded.State->CurrentOrdinal;
    if (current == aRolledBackOrdinal + 1)
    {
        const auto terminal = VerifyTerminalRollback(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            loaded.State->LastRolledBackRestoreId);
        if (terminal != PartyQuestRuntimeRestoreAttemptStatus::Success)
            return MakeResult(terminal, acPaths, aTransactionId, &*loaded.State);
        loaded.Status = PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced;
        return loaded;
    }
    if (current > aRolledBackOrdinal + 1)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::StaleJournal,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }
    if (current != aRolledBackOrdinal)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }

    const auto terminal = VerifyTerminalRollback(
        acPaths,
        acCampaignId,
        acPlayerProfileId,
        loaded.State->CurrentRestoreId);
    if (terminal != PartyQuestRuntimeRestoreAttemptStatus::Success)
        return MakeResult(terminal, acPaths, aTransactionId, &*loaded.State);

    if (current + 1 >= MaxAttemptsPerTransaction)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::AttemptLimitReached,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }

    const uint64_t rolledBackRestoreId = loaded.State->CurrentRestoreId;
    const auto allocation = AllocateRestoreIdPowerLossDurably(
        acPaths, acCampaignId, acPlayerProfileId);
    if (allocation.first != PartyQuestRuntimeRestoreAttemptStatus::Success ||
        allocation.second == 0)
    {
        return MakeResult(allocation.first, acPaths, aTransactionId, &*loaded.State);
    }

    PartyQuestRuntimeRestoreAttemptState advanced = *loaded.State;
    ++advanced.CurrentOrdinal;
    advanced.LastRolledBackRestoreId = rolledBackRestoreId;
    advanced.CurrentRestoreId = allocation.second;
    const auto saveStatus = SaveStatePowerLossDurably(loaded.StatePath, advanced);
    if (saveStatus != PartyQuestRuntimeRestoreAttemptStatus::Success)
        return MakeResult(saveStatus, acPaths, aTransactionId, &*loaded.State);

    return MakeResult(
        PartyQuestRuntimeRestoreAttemptStatus::Success,
        acPaths,
        aTransactionId,
        &advanced);
#endif
}
