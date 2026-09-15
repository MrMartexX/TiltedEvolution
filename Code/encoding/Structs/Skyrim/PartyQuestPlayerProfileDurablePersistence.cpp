#include <Structs/Skyrim/PartyQuestPlayerProfilePersistence.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
PartyQuestPlayerProfilePersistenceStatus MapStableStatus(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    if (aStatus == PartyQuestStableStorageStatus::Success)
        return PartyQuestPlayerProfilePersistenceStatus::Success;
    if (aStatus == PartyQuestStableStorageStatus::Unsupported)
        return PartyQuestPlayerProfilePersistenceStatus::PowerLossDurabilityUnsupported;
    return PartyQuestPlayerProfilePersistenceStatus::IoError;
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

bool ReadExactArchive(
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
    if (size > PartyQuestDurableResourcePolicy::MaxIdentityArchiveBytes ||
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

PartyQuestPlayerProfilePersistenceResult LoadArchiveExact(
    const std::filesystem::path& acPath)
{
    PartyQuestPlayerProfilePersistenceResult result;
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory)
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::FileNotFound;
            return result;
        }
        if (ec || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status))
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
            return result;
        }

        std::vector<uint8_t> bytes;
        if (!ReadExactArchive(acPath, bytes))
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
            return result;
        }
        return PartyQuestPlayerProfilePersistence::Decode(bytes);
    }
    catch (...)
    {
        result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
        return result;
    }
}
} // namespace

PartyQuestPlayerProfilePersistenceStatus
PartyQuestPlayerProfilePersistence::SavePowerLossDurably(
    const std::filesystem::path& acPath,
    const PartyQuestPlayerProfileId& acProfileId,
    PartyQuestPlayerProfilePersistenceHooks aHooks)
{
    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
        return PartyQuestPlayerProfilePersistenceStatus::InvalidData;

    const auto encoded = Encode(acProfileId);
    if (encoded.empty())
        return PartyQuestPlayerProfilePersistenceStatus::InvalidData;

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestPlayerProfilePersistenceStatus::IoError;

        const auto parentStatus = std::filesystem::symlink_status(path.parent_path(), ec);
        if (ec || std::filesystem::is_symlink(parentStatus) ||
            !std::filesystem::is_directory(parentStatus))
        {
            return PartyQuestPlayerProfilePersistenceStatus::IoError;
        }
    }
    catch (...)
    {
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    if (!IsExistingRegularOrMissing(path) ||
        !IsExistingRegularOrMissing(temporary) ||
        !IsExistingRegularOrMissing(backup))
    {
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    }

    auto stable = PartyQuestStableStorage::WriteFileDurably(
        temporary,
        encoded.data(),
        encoded.size());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    std::vector<uint8_t> verifiedBytes;
    if (!ReadExactArchive(temporary, verifiedBytes))
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    const auto verified = Decode(verifiedBytes);
    if (verified.Status != PartyQuestPlayerProfilePersistenceStatus::Success ||
        !verified.ProfileId || *verified.ProfileId != acProfileId ||
        verifiedBytes != encoded)
    {
        return PartyQuestPlayerProfilePersistenceStatus::InvalidData;
    }

    if (aHooks.Invoke(PartyQuestPlayerProfilePersistenceBoundary::TemporaryVerified) ==
        PartyQuestPlayerProfilePersistenceDirective::FailClosed)
    {
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    }

    std::error_code ec;
    const bool hadPrimary = std::filesystem::exists(path, ec);
    if (ec)
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    if (hadPrimary)
    {
        stable = PartyQuestStableStorage::PublishFileRename(path, backup, true);
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableStatus(stable);

        if (aHooks.Invoke(
                PartyQuestPlayerProfilePersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestPlayerProfilePersistenceDirective::FailClosed)
        {
            return PartyQuestPlayerProfilePersistenceStatus::IoError;
        }
    }

    stable = PartyQuestStableStorage::PublishFileRename(temporary, path, false);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    if (aHooks.Invoke(PartyQuestPlayerProfilePersistenceBoundary::TemporaryPublished) ==
        PartyQuestPlayerProfilePersistenceDirective::FailClosed)
    {
        return PartyQuestPlayerProfilePersistenceStatus::IoError;
    }

    return PartyQuestPlayerProfilePersistenceStatus::Success;
}

PartyQuestPlayerProfilePersistenceResult
PartyQuestPlayerProfilePersistence::LoadPowerLossDurably(
    const std::filesystem::path& acPath)
{
    PartyQuestPlayerProfilePersistenceResult result;
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(acPath))
    {
        result.Status = PartyQuestPlayerProfilePersistenceStatus::InvalidData;
        return result;
    }

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
            return result;
        }
    }
    catch (...)
    {
        result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
        return result;
    }

    auto primary = LoadArchiveExact(path);
    if (primary.Status == PartyQuestPlayerProfilePersistenceStatus::Success)
        return primary;

    auto temporaryPath = path;
    temporaryPath += ".tmp";
    auto backupPath = path;
    backupPath += ".bak";
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(temporaryPath) ||
        !PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(backupPath))
    {
        result.Status = PartyQuestPlayerProfilePersistenceStatus::InvalidData;
        return result;
    }

    auto temporary = LoadArchiveExact(temporaryPath);
    auto backup = LoadArchiveExact(backupPath);
    if (temporary.Status == PartyQuestPlayerProfilePersistenceStatus::Success)
    {
        // A present invalid primary is conflicting immutable-lineage evidence.
        // Do not silently route around it merely because a staged identity also
        // decodes. Normal strong-write crash cuts either leave the old primary
        // intact and valid, or move it completely to the backup name.
        if (primary.Status != PartyQuestPlayerProfilePersistenceStatus::FileNotFound)
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::IoError;
            return result;
        }
        if (backup.Status == PartyQuestPlayerProfilePersistenceStatus::Success &&
            temporary.ProfileId != backup.ProfileId)
        {
            result.Status = PartyQuestPlayerProfilePersistenceStatus::InvalidData;
            return result;
        }

        temporary.UsedTemporary = true;
        return temporary;
    }

    if (backup.Status == PartyQuestPlayerProfilePersistenceStatus::Success)
    {
        backup.UsedBackup = true;
        return backup;
    }

    return primary;
}
