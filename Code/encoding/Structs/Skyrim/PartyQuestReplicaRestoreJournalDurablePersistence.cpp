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

PartyQuestReplicaRestoreJournalPersistenceResult LoadArchiveExact(
    const std::filesystem::path& acPath)
{
    PartyQuestReplicaRestoreJournalPersistenceResult result;
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found ||
            ec == std::errc::no_such_file_or_directory)
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound;
            return result;
        }
        if (ec || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status))
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            return result;
        }

        std::vector<uint8_t> bytes;
        if (!ReadArchive(acPath, bytes))
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            return result;
        }
        return PartyQuestReplicaRestoreJournalPersistence::Decode(bytes);
    }
    catch (...)
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        return result;
    }
}

PartyQuestReplicaRestoreJournalPersistenceResult RequirePowerLossArchive(
    PartyQuestReplicaRestoreJournalPersistenceResult aResult) noexcept
{
    if (aResult.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return aResult;
    if (!aResult.State)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return aResult;
    }
    if (aResult.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityAmbiguous;
        return aResult;
    }
    if (aResult.ArchiveDurability !=
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityMismatch;
        return aResult;
    }
    return aResult;
}

PartyQuestReplicaRestoreJournalPersistenceStatus ValidateExistingStrongEvidence(
    const std::filesystem::path& acPath) noexcept
{
    const auto decoded = LoadArchiveExact(acPath);
    if (decoded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    if (decoded.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    return RequirePowerLossArchive(decoded).Status;
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

    const auto encoded = EncodeForArchiveDurability(
        acState,
        PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable);
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

    for (const auto& evidence : {path, temporary, backup})
    {
        const auto domainStatus = ValidateExistingStrongEvidence(evidence);
        if (domainStatus != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            return domainStatus;
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

    const auto verified = RequirePowerLossArchive(Decode(verifiedBytes));
    if (verified.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
        !verified.State || *verified.State != acState)
    {
        return verified.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success
            ? PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData
            : verified.Status;
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

PartyQuestReplicaRestoreJournalPersistenceResult
PartyQuestReplicaRestoreJournalPersistence::LoadPowerLossDurably(
    const std::filesystem::path& acPath)
{
    PartyQuestReplicaRestoreJournalPersistenceResult result;
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(acPath))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        return result;
    }

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            return result;
        }
    }
    catch (...)
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        return result;
    }

    auto primary = LoadArchiveExact(path);
    if (primary.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return RequirePowerLossArchive(std::move(primary));

    auto temporaryPath = path;
    temporaryPath += ".tmp";
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(temporaryPath))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        return result;
    }
    auto temporary = LoadArchiveExact(temporaryPath);
    if (temporary.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        temporary = RequirePowerLossArchive(std::move(temporary));
        if (temporary.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            return temporary;

        // A present but invalid primary is conflicting recovery evidence. Never
        // overwrite it merely because a newer-looking temporary also decodes.
        if (primary.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            return result;
        }

        const auto stable = PartyQuestStableStorage::PublishFileRename(
            temporaryPath,
            path,
            false);
        if (stable != PartyQuestStableStorageStatus::Success)
        {
            result.Status = MapStableStatus(stable);
            return result;
        }

        auto promoted = RequirePowerLossArchive(LoadArchiveExact(path));
        if (promoted.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        {
            if (promoted.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityAmbiguous &&
                promoted.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityMismatch)
            {
                promoted.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
                promoted.State.reset();
            }
            return promoted;
        }
        promoted.UsedTemporary = true;
        return promoted;
    }

    auto backupPath = path;
    backupPath += ".bak";
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(backupPath))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        return result;
    }
    auto backup = LoadArchiveExact(backupPath);
    if (backup.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        backup = RequirePowerLossArchive(std::move(backup));
        if (backup.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            return backup;
        backup.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired;
        return backup;
    }

    return primary;
}
