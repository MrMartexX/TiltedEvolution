#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifndef _WIN32
PartyQuestStableStorageStatus FlushPosixPath(
    const std::filesystem::path& acPath,
    bool aDirectory) noexcept
{
    if (acPath.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

    int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
    if (aDirectory)
        flags |= O_DIRECTORY;

    const int descriptor = ::open(acPath.c_str(), flags);
    if (descriptor < 0)
        return PartyQuestStableStorageStatus::OpenFailed;

    struct stat status{};
    if (::fstat(descriptor, &status) != 0 ||
        (aDirectory ? !S_ISDIR(status.st_mode) : !S_ISREG(status.st_mode)))
    {
        ::close(descriptor);
        return PartyQuestStableStorageStatus::NodeValidationFailed;
    }

    if (::fsync(descriptor) != 0)
    {
        ::close(descriptor);
        return PartyQuestStableStorageStatus::FlushFailed;
    }

    if (::close(descriptor) != 0)
        return PartyQuestStableStorageStatus::CloseFailed;

    return PartyQuestStableStorageStatus::Success;
}
#endif
} // namespace

PartyQuestStableStorageStatus PartyQuestStableStorage::FlushFile(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    const HANDLE file = ::CreateFileW(
        acPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return PartyQuestStableStorageStatus::OpenFailed;

    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        ::CloseHandle(file);
        return PartyQuestStableStorageStatus::NodeValidationFailed;
    }

    if (!::FlushFileBuffers(file))
    {
        ::CloseHandle(file);
        return PartyQuestStableStorageStatus::FlushFailed;
    }

    if (!::CloseHandle(file))
        return PartyQuestStableStorageStatus::CloseFailed;

    return PartyQuestStableStorageStatus::Success;
#else
    return FlushPosixPath(acPath, false);
#endif
}

PartyQuestStableStorageStatus PartyQuestStableStorage::FlushDirectory(
    const std::filesystem::path& acDirectory) noexcept
{
    if (acDirectory.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    // No documented non-admin Win32 equivalent to POSIX directory fsync is
    // accepted as a P0-H proof primitive here. Keep the boundary explicit.
    return PartyQuestStableStorageStatus::Unsupported;
#else
    return FlushPosixPath(acDirectory, true);
#endif
}

PartyQuestStableStorageStatus PartyQuestStableStorage::FlushParentDirectory(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

    try
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(acPath, ec);
        if (ec || absolute.empty() || absolute.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        return FlushDirectory(absolute.parent_path());
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
}

PartyQuestStableStorageStatus PartyQuestStableStorage::PublishFileRename(
    const std::filesystem::path& acSource,
    const std::filesystem::path& acDestination) noexcept
{
    if (acSource.empty() || acDestination.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    // P0-H deliberately has no Windows publication claim yet. File-level
    // FlushFileBuffers alone is not treated as proof that the destination
    // directory entry is durably published.
    return PartyQuestStableStorageStatus::Unsupported;
#else
    try
    {
        std::error_code ec;
        const auto source = std::filesystem::absolute(acSource, ec).lexically_normal();
        if (ec || source.empty() || source.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        ec.clear();
        const auto destination =
            std::filesystem::absolute(acDestination, ec).lexically_normal();
        if (ec || destination.empty() || destination.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        if (source == destination || source.parent_path() != destination.parent_path())
            return PartyQuestStableStorageStatus::CrossDirectoryRename;

        const auto fileFlush = FlushFile(source);
        if (fileFlush != PartyQuestStableStorageStatus::Success)
            return fileFlush;

        ec.clear();
        std::filesystem::rename(source, destination, ec);
        if (ec)
            return PartyQuestStableStorageStatus::RenameFailed;

        // Once rename has succeeded a failed directory fsync is an uncertain
        // publication, not a reason to pretend the old namespace is restored.
        // The caller must keep transaction/recovery authority and fail closed.
        return FlushDirectory(destination.parent_path());
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
#endif
}
