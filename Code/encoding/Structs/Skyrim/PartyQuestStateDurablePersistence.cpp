#include <Structs/Skyrim/PartyQuestStatePersistence.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
PartyQuestPersistenceStatus MapStableStatus(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    if (aStatus == PartyQuestStableStorageStatus::Success)
        return PartyQuestPersistenceStatus::Success;
    if (aStatus == PartyQuestStableStorageStatus::Unsupported)
        return PartyQuestPersistenceStatus::PowerLossDurabilityUnsupported;
    return PartyQuestPersistenceStatus::IoError;
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
    if (size > PartyQuestDurableResourcePolicy::MaxCanonicalStateArchiveBytes ||
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

PartyQuestPersistenceStatus PartyQuestStatePersistence::SavePowerLossDurably(
    const std::filesystem::path& acPath,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestState& acState,
    PartyQuestStatePersistenceHooks aHooks)
{
    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
        return PartyQuestPersistenceStatus::InvalidData;

    const auto encoded = Encode(acCampaignId, acState);
    if (encoded.empty())
        return PartyQuestPersistenceStatus::InvalidData;

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestPersistenceStatus::IoError;

        const auto parentStatus = std::filesystem::symlink_status(path.parent_path(), ec);
        if (ec || std::filesystem::is_symlink(parentStatus) ||
            !std::filesystem::is_directory(parentStatus))
        {
            return PartyQuestPersistenceStatus::IoError;
        }
    }
    catch (...)
    {
        return PartyQuestPersistenceStatus::IoError;
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    if (!IsExistingRegularOrMissing(path) ||
        !IsExistingRegularOrMissing(temporary) ||
        !IsExistingRegularOrMissing(backup))
    {
        return PartyQuestPersistenceStatus::IoError;
    }

    auto stable = PartyQuestStableStorage::WriteFileDurably(
        temporary,
        encoded.data(),
        encoded.size());
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    std::vector<uint8_t> verifiedBytes;
    if (!ReadExactArchive(temporary, verifiedBytes))
        return PartyQuestPersistenceStatus::IoError;
    const auto verified = Decode(verifiedBytes);
    if (verified.Status != PartyQuestPersistenceStatus::Success ||
        verified.CampaignId != acCampaignId ||
        !verified.State ||
        Encode(acCampaignId, *verified.State) != encoded ||
        verifiedBytes != encoded)
    {
        return PartyQuestPersistenceStatus::InvalidData;
    }

    if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::TemporaryVerified) ==
        PartyQuestStatePersistenceDirective::FailClosed)
    {
        return PartyQuestPersistenceStatus::IoError;
    }

    std::error_code ec;
    const bool hadPrimary = std::filesystem::exists(path, ec);
    if (ec)
        return PartyQuestPersistenceStatus::IoError;
    if (hadPrimary)
    {
        stable = PartyQuestStableStorage::PublishFileRename(path, backup, true);
        if (stable != PartyQuestStableStorageStatus::Success)
            return MapStableStatus(stable);

        if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestStatePersistenceDirective::FailClosed)
        {
            return PartyQuestPersistenceStatus::IoError;
        }
    }

    stable = PartyQuestStableStorage::PublishFileRename(temporary, path, false);
    if (stable != PartyQuestStableStorageStatus::Success)
        return MapStableStatus(stable);

    if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::TemporaryPublished) ==
        PartyQuestStatePersistenceDirective::FailClosed)
    {
        return PartyQuestPersistenceStatus::IoError;
    }

    return PartyQuestPersistenceStatus::Success;
}
