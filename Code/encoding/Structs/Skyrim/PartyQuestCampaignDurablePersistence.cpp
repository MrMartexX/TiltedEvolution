#include <Structs/Skyrim/PartyQuestCampaignPersistence.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
PartyQuestCampaignPersistenceStatus MapStableStatus(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    if (aStatus == PartyQuestStableStorageStatus::Success)
        return PartyQuestCampaignPersistenceStatus::Success;
    if (aStatus == PartyQuestStableStorageStatus::Unsupported)
        return PartyQuestCampaignPersistenceStatus::PowerLossDurabilityUnsupported;
    return PartyQuestCampaignPersistenceStatus::IoError;
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

bool VerifyCampaignArchive(
    const std::filesystem::path& acPath,
    const PartyQuestCampaignId& acCampaignId,
    const std::vector<uint8_t>& acEncoded)
{
    std::vector<uint8_t> bytes;
    if (!ReadExactArchive(acPath, bytes) || bytes != acEncoded)
        return false;
    const auto decoded = PartyQuestCampaignPersistence::Decode(bytes);
    return decoded.Status == PartyQuestCampaignPersistenceStatus::Success &&
        decoded.CampaignId == acCampaignId &&
        decoded.CanonicalArchiveRequired;
}
} // namespace

PartyQuestCampaignPersistenceStatus
PartyQuestCampaignPersistence::SavePowerLossDurably(
    const std::filesystem::path& acPath,
    const PartyQuestCampaignId& acCampaignId,
    PartyQuestCampaignPersistenceHooks aHooks)
{
    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    const auto encoded = Encode(acCampaignId);
    if (encoded.empty())
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestCampaignPersistenceStatus::IoError;

        const auto parentStatus = std::filesystem::symlink_status(path.parent_path(), ec);
        if (ec || std::filesystem::is_symlink(parentStatus) ||
            !std::filesystem::is_directory(parentStatus))
        {
            return PartyQuestCampaignPersistenceStatus::IoError;
        }
    }
    catch (...)
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    auto backupTemporary = backup;
    backupTemporary += ".tmp";
    if (!IsExistingRegularOrMissing(path) ||
        !IsExistingRegularOrMissing(temporary) ||
        !IsExistingRegularOrMissing(backup) ||
        !IsExistingRegularOrMissing(backupTemporary))
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    auto stable = PartyQuestStableStorage::WriteFileDurably(
        temporary,
        encoded.data(),
        encoded.size());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);
    if (!VerifyCampaignArchive(temporary, acCampaignId, encoded))
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    if (aHooks.Invoke(PartyQuestCampaignPersistenceBoundary::TemporaryVerified) ==
        PartyQuestCampaignPersistenceDirective::FailClosed)
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    std::error_code ec;
    const bool hadPrimary = std::filesystem::exists(path, ec);
    if (ec)
        return PartyQuestCampaignPersistenceStatus::IoError;
    if (hadPrimary)
    {
        stable = PartyQuestStableStorage::PublishFileRename(path, backup, true);
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableStatus(stable);

        if (aHooks.Invoke(PartyQuestCampaignPersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestCampaignPersistenceDirective::FailClosed)
        {
            return PartyQuestCampaignPersistenceStatus::IoError;
        }
    }

    stable = PartyQuestStableStorage::PublishFileRename(temporary, path, false);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);
    if (aHooks.Invoke(PartyQuestCampaignPersistenceBoundary::PrimaryPublished) ==
        PartyQuestCampaignPersistenceDirective::FailClosed)
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    // Campaign identity is immutable and bootstrap intentionally keeps a second
    // exact v2 copy rather than merely an older generation. Refresh that copy
    // only after the new primary is durably published.
    stable = PartyQuestStableStorage::WriteFileDurably(
        backupTemporary,
        encoded.data(),
        encoded.size());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);
    if (!VerifyCampaignArchive(backupTemporary, acCampaignId, encoded))
        return PartyQuestCampaignPersistenceStatus::InvalidData;

    if (aHooks.Invoke(PartyQuestCampaignPersistenceBoundary::BackupTemporaryVerified) ==
        PartyQuestCampaignPersistenceDirective::FailClosed)
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    stable = PartyQuestStableStorage::PublishFileRename(
        backupTemporary,
        backup,
        true);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    if (aHooks.Invoke(PartyQuestCampaignPersistenceBoundary::BackupPublished) ==
        PartyQuestCampaignPersistenceDirective::FailClosed)
    {
        return PartyQuestCampaignPersistenceStatus::IoError;
    }

    return PartyQuestCampaignPersistenceStatus::Success;
}
