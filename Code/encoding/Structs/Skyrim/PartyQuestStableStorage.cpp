#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
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

    int flags = O_RDONLY | O_CLOEXEC;
    if (aDirectory)
        flags |= O_DIRECTORY;

    const int descriptor = ::open(acPath.c_str(), flags);
    if (descriptor < 0)
        return PartyQuestStableStorageStatus::OpenFailed;

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
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return PartyQuestStableStorageStatus::OpenFailed;

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
