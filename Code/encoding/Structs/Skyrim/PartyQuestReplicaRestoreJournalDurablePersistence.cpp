#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
PartyQuestReplicaRestoreJournalPersistenceStatus MapStableStatus(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    if (aStatus == PartyQuestStableStorageStatus::Success)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    if (aStatus == PartyQuestStableStorageStatus::Unsupported)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::PowerLossDurabilityUnsupported;
    return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
}

bool IsExistingRegularOrMissing(const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory)
        {
            return true;
        }
        if (ec)
            return false;
        return !std::filesystem::is_symlink(status) &&
            std::filesystem::is_regular_file(status);
    }
    catch (...)
    {
        return false;
    }
}

bool ReadArchive(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    const std::streampos end = file.tellg();
    if (end < 0)
        return false;
    const auto size = static_cast<uint64_t>(end);
    if (size > PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes ||
        size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return false;
    }

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty())
    {
        file.read(
            reinterpret_cast<char*>(aBytes.data()),
            static_cast<std::streamsize>(aBytes.size()));
    }
    return file.good();
}
} // namespace

PartyQuestReplicaRestoreJournalPersistenceStatus
PartyQuestReplicaRestoreJournalPersistence::SavePowerLossDurably(
    const std::filesystem::path& acPath,
    const PartyQuestReplicaRestoreJournalState& acState,
    PartyQuestReplicaRestoreJournalPersistenceHooks aHooks)
{
    if (acPath.empty())
        return PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
        return PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;

    const auto encoded = Encode(acState);
    if (encoded.empty())
        return PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

        const auto parentStatus = std::filesystem::symlink_status(path.parent_path(), ec);
        if (ec || std::filesystem::is_symlink(parentStatus) ||
            !std::filesystem::is_directory(parentStatus))
        {
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }
    }
    catch (...)
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";

    if (!IsExistingRegularOrMissing(path) ||
        !IsExistingRegularOrMissing(temporary) ||
        !IsExistingRegularOrMissing(backup))
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }

    auto stable = PartyQuestStableStorage::WriteFileDurably(
        temporary,
        encoded.data(),
        encoded.size());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    std::vector<uint8_t> verifiedBytes;
    if (!ReadArchive(temporary, verifiedBytes))
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

    const auto verified = Decode(verifiedBytes);
    if (verified.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
        !verified.State || *verified.State != acState)
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
    }

    if (aHooks.Invoke(
            PartyQuestReplicaRestoreJournalPersistenceBoundary::TemporaryVerified) ==
        PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }

    std::error_code ec;
    const bool hadPrimary = std::filesystem::exists(path, ec);
    if (ec)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

    if (hadPrimary)
    {
        stable = PartyQuestStableStorage::PublishFileRename(path, backup, true);
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableStatus(stable);

        if (aHooks.Invoke(
                PartyQuestReplicaRestoreJournalPersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
        {
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }
    }

    stable = PartyQuestStableStorage::PublishFileRename(temporary, path, false);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    if (aHooks.Invoke(
            PartyQuestReplicaRestoreJournalPersistenceBoundary::TemporaryPublished) ==
        PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }

    return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
}
