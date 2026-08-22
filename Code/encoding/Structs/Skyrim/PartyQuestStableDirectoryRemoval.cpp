#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

PartyQuestStableStorageStatus PartyQuestStableStorage::RemoveEmptyDirectoryDurably(
    const std::filesystem::path& acDirectory) noexcept
{
    if (acDirectory.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    // File copy/removal and directory-tree promotion now have reviewed NTFS
    // contracts, but terminal restore-directory compaction is a separate
    // recovery authority surface. Keep directory deletion unavailable until the
    // restore state machine and its Windows fault matrix are promoted together.
    return PartyQuestStableStorageStatus::Unsupported;
#else
    try
    {
        std::error_code ec;
        const auto directory =
            std::filesystem::absolute(acDirectory, ec).lexically_normal();
        if (ec || directory.empty() || directory.parent_path().empty() ||
            directory == directory.root_path())
        {
            return PartyQuestStableStorageStatus::InvalidPath;
        }

        struct stat status{};
        if (::lstat(directory.c_str(), &status) != 0)
            return PartyQuestStableStorageStatus::OpenFailed;
        if (!S_ISDIR(status.st_mode))
            return PartyQuestStableStorageStatus::NodeValidationFailed;

        if (::rmdir(directory.c_str()) != 0)
            return PartyQuestStableStorageStatus::RemoveFailed;

        return FlushDirectory(directory.parent_path());
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
#endif
}
