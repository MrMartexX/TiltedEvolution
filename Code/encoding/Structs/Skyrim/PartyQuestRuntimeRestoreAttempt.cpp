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
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'R', 'A', 'T', 'T', 'M'};
constexpr uint16_t kFormatVersion = 1;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kArchiveSize =
    kMagic.size() + sizeof(uint16_t) +
    sizeof(uint64_t) * 5 + sizeof(uint32_t) + sizeof(uint64_t);

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

std::string FormatAttemptOrdinal(uint32_t aOrdinal)
{
    std::ostringstream stream;
    stream << "Attempt_" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << aOrdinal;
    return stream.str();
}

bool IsValidState(const PartyQuestRuntimeRestoreAttemptState& acState) noexcept
{
    return acState.CampaignId.IsValid() &&
        acState.PlayerProfileId.IsValid() &&
        acState.TransactionId != 0 &&
        acState.CurrentOrdinal <
            PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction;
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
        result.RestoreId = PartyQuestRuntimeRestoreAttemptStore::GetRestoreId(
            apState->CurrentOrdinal);
        result.AttemptDirectory =
            PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(
                acPaths, aTransactionId, apState->CurrentOrdinal);
        result.JournalPath = result.AttemptDirectory / "journal.bin";
    }
    return result;
}

std::vector<uint8_t> Encode(const PartyQuestRuntimeRestoreAttemptState& acState)
{
    if (!IsValidState(acState))
        return {};

    std::vector<uint8_t> bytes;
    bytes.reserve(kArchiveSize);
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    WriteInteger(bytes, kFormatVersion);
    const size_t payloadOffset = bytes.size();
    WriteInteger(bytes, acState.CampaignId.High);
    WriteInteger(bytes, acState.CampaignId.Low);
    WriteInteger(bytes, acState.PlayerProfileId.High);
    WriteInteger(bytes, acState.PlayerProfileId.Low);
    WriteInteger(bytes, acState.TransactionId);
    WriteInteger(bytes, acState.CurrentOrdinal);
    const uint64_t checksum = ComputeChecksum(
        bytes.data() + payloadOffset,
        bytes.size() - payloadOffset);
    WriteInteger(bytes, checksum);
    return bytes;
}

PartyQuestRuntimeRestoreAttemptStatus Decode(
    const std::vector<uint8_t>& acBytes,
    PartyQuestRuntimeRestoreAttemptState& aState) noexcept
{
    if (acBytes.size() != kArchiveSize ||
        !std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    size_t offset = kMagic.size();
    uint16_t version{};
    if (!ReadInteger(acBytes, offset, version) || version != kFormatVersion)
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    const size_t payloadOffset = offset;
    if (!ReadInteger(acBytes, offset, aState.CampaignId.High) ||
        !ReadInteger(acBytes, offset, aState.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, aState.PlayerProfileId.Low) ||
        !ReadInteger(acBytes, offset, aState.TransactionId) ||
        !ReadInteger(acBytes, offset, aState.CurrentOrdinal))
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

PartyQuestRuntimeRestoreAttemptStatus ReadStateFile(
    const std::filesystem::path& acPath,
    PartyQuestRuntimeRestoreAttemptState& aState) noexcept
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
        if (ec || size != kArchiveSize)
            return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

        std::ifstream file(acPath, std::ios::binary);
        if (!file.is_open())
            return PartyQuestRuntimeRestoreAttemptStatus::IoError;

        std::vector<uint8_t> bytes(kArchiveSize);
        if (!file.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
        {
            return PartyQuestRuntimeRestoreAttemptStatus::IoError;
        }
        return Decode(bytes, aState);
    }
    catch (...)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::IoError;
    }
}

PartyQuestRuntimeRestoreAttemptStatus SaveStatePowerLossDurably(
    const std::filesystem::path& acStatePath,
    const PartyQuestRuntimeRestoreAttemptState& acState) noexcept
{
#ifdef _WIN32
    (void)acStatePath;
    (void)acState;
    return PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform;
#else
    const auto bytes = Encode(acState);
    if (bytes.empty() ||
        !PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acStatePath))
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    const auto parent = acStatePath.parent_path();
    if (PartyQuestStableStorage::EnsureDirectoryTreeDurably(parent) !=
        PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }

    std::filesystem::path temporary;
    try
    {
        temporary = acStatePath;
        temporary += ".tmp";
    }
    catch (...)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;
    }

    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(temporary))
        return PartyQuestRuntimeRestoreAttemptStatus::InvalidData;

    if (PartyQuestStableStorage::WriteFileDurably(
            temporary, bytes.data(), bytes.size()) !=
        PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }

    if (PartyQuestStableStorage::PublishFileRename(
            temporary, acStatePath, true) != PartyQuestStableStorageStatus::Success)
    {
        return PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed;
    }
    return PartyQuestRuntimeRestoreAttemptStatus::Success;
#endif
}

bool JournalMatchesAttempt(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    uint64_t aTransactionId,
    uint32_t aOrdinal,
    const PartyQuestReplicaRestoreJournalState& acJournal) noexcept
{
    if (aOrdinal >= PartyQuestRuntimeRestoreAttemptStore::MaxAttemptsPerTransaction ||
        acJournal.Phase != PartyQuestReplicaRestoreJournalPhase::RolledBack ||
        acJournal.CampaignId != acCampaignId ||
        acJournal.PlayerProfileId != acPlayerProfileId ||
        acJournal.RestoreId != PartyQuestRuntimeRestoreAttemptStore::GetRestoreId(aOrdinal))
    {
        return false;
    }

    const auto expectedDirectory =
        PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(
            acPaths, aTransactionId, aOrdinal);
    return !expectedDirectory.empty() &&
        acJournal.TransactionDirectory.lexically_normal() ==
            expectedDirectory.lexically_normal();
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

std::filesystem::path PartyQuestRuntimeRestoreAttemptStore::GetAttemptDirectory(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId,
    uint32_t aOrdinal) noexcept
{
    if (aTransactionId == 0 || aOrdinal >= MaxAttemptsPerTransaction)
        return {};
    try
    {
        return (acPaths.MetadataDirectory / "restore" /
            FormatTransactionId(aTransactionId) /
            FormatAttemptOrdinal(aOrdinal)).lexically_normal();
    }
    catch (...)
    {
        return {};
    }
}

std::filesystem::path PartyQuestRuntimeRestoreAttemptStore::GetJournalPath(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId,
    uint32_t aOrdinal) noexcept
{
    const auto directory = GetAttemptDirectory(acPaths, aTransactionId, aOrdinal);
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
        if (!MatchesIdentity(recovered, acCampaignId, acPlayerProfileId, aTransactionId) ||
            recovered.CurrentOrdinal != 0)
        {
            return MakeResult(PartyQuestRuntimeRestoreAttemptStatus::InvalidData, acPaths, aTransactionId);
        }
        if (PartyQuestStableStorage::PublishFileRename(
                temporary, statePath, true) != PartyQuestStableStorageStatus::Success)
        {
            return MakeResult(
                PartyQuestRuntimeRestoreAttemptStatus::PersistenceFailed,
                acPaths,
                aTransactionId);
        }
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::RecoveredInitialization,
            acPaths,
            aTransactionId,
            &recovered);
    }
    if (temporaryStatus != PartyQuestRuntimeRestoreAttemptStatus::FileNotFound)
        return MakeResult(temporaryStatus, acPaths, aTransactionId);

    PartyQuestRuntimeRestoreAttemptState initial;
    initial.CampaignId = acCampaignId;
    initial.PlayerProfileId = acPlayerProfileId;
    initial.TransactionId = aTransactionId;
    initial.CurrentOrdinal = 0;
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
    const PartyQuestReplicaRestoreJournalState& acRolledBackJournal,
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
    (void)acRolledBackJournal;
    return MakeResult(
        PartyQuestRuntimeRestoreAttemptStatus::UnsupportedPlatform,
        acPaths,
        aTransactionId);
#else
    if (acRolledBackJournal.RestoreId == 0 ||
        acRolledBackJournal.RestoreId > MaxAttemptsPerTransaction)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch,
            acPaths,
            aTransactionId);
    }

    const uint32_t journalOrdinal =
        static_cast<uint32_t>(acRolledBackJournal.RestoreId - 1);
    if (!JournalMatchesAttempt(
            acPaths,
            acCampaignId,
            acPlayerProfileId,
            aTransactionId,
            journalOrdinal,
            acRolledBackJournal))
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch,
            acPaths,
            aTransactionId);
    }

    auto loaded = Load(acPaths, acCampaignId, acPlayerProfileId, aTransactionId);
    if (loaded.Status != PartyQuestRuntimeRestoreAttemptStatus::Success || !loaded.State)
        return loaded;

    const uint32_t current = loaded.State->CurrentOrdinal;
    if (current == journalOrdinal + 1)
    {
        loaded.Status = PartyQuestRuntimeRestoreAttemptStatus::AlreadyAdvanced;
        return loaded;
    }
    if (current > journalOrdinal + 1)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::StaleJournal,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }
    if (current != journalOrdinal)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::JournalMismatch,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }
    if (current + 1 >= MaxAttemptsPerTransaction)
    {
        return MakeResult(
            PartyQuestRuntimeRestoreAttemptStatus::AttemptLimitReached,
            acPaths,
            aTransactionId,
            &*loaded.State);
    }

    PartyQuestRuntimeRestoreAttemptState advanced = *loaded.State;
    ++advanced.CurrentOrdinal;
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
