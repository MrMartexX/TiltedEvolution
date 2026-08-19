#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <system_error>
#include <vector>

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
#else
PartyQuestStableStorageStatus ValidateWindowsRegularHandle(HANDLE aFile) noexcept
{
    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(aFile, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        return PartyQuestStableStorageStatus::NodeValidationFailed;
    }

    return PartyQuestStableStorageStatus::Success;
}

bool IsWindowsNtfsHandle(HANDLE aFile) noexcept
{
    wchar_t fileSystemName[MAX_PATH + 1]{};
    if (!::GetVolumeInformationByHandleW(
            aFile,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            fileSystemName,
            MAX_PATH + 1))
    {
        return false;
    }

    return ::CompareStringOrdinal(
               fileSystemName,
               -1,
               L"NTFS",
               -1,
               TRUE) == CSTR_EQUAL;
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

    const auto validation = ValidateWindowsRegularHandle(file);
    if (validation != PartyQuestStableStorageStatus::Success)
    {
        ::CloseHandle(file);
        return validation;
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
    // No generic documented non-admin Win32 equivalent to POSIX directory fsync
    // is accepted as a P0-H proof primitive. NTFS rename durability uses the
    // narrower FILE_FLAG_WRITE_THROUGH path below instead.
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
    const std::filesystem::path& acDestination,
    bool aReplaceExisting) noexcept
{
    if (acSource.empty() || acDestination.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

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

#ifdef _WIN32
        // Use one exact source handle for validation, data flush and rename. This
        // avoids proving durability for one path node and then renaming a later
        // replacement of that node.
        const HANDLE file = ::CreateFileW(
            source.c_str(),
            GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
                FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return PartyQuestStableStorageStatus::OpenFailed;

        const auto validation = ValidateWindowsRegularHandle(file);
        if (validation != PartyQuestStableStorageStatus::Success)
        {
            ::CloseHandle(file);
            return validation;
        }

        // The accepted Windows proof is intentionally NTFS-specific. Microsoft
        // documents write-through metadata flushing for NTFS rename operations;
        // no equivalent claim is made here for ReFS/FAT/network filesystems.
        if (!IsWindowsNtfsHandle(file))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::Unsupported;
        }

        if (!::FlushFileBuffers(file))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::FlushFailed;
        }

        const auto& destinationName = destination.native();
        const size_t destinationBytes =
            destinationName.size() * sizeof(std::filesystem::path::value_type);
        if (destinationName.empty() ||
            destinationBytes > static_cast<size_t>(std::numeric_limits<DWORD>::max()))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::InvalidPath;
        }

        const size_t renameInfoSize =
            offsetof(FILE_RENAME_INFO, FileName) + destinationBytes;
        if (renameInfoSize > static_cast<size_t>(std::numeric_limits<DWORD>::max()))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::InvalidPath;
        }

        const size_t alignedUnits =
            (renameInfoSize + sizeof(std::max_align_t) - 1) /
            sizeof(std::max_align_t);
        std::vector<std::max_align_t> renameStorage(alignedUnits);
        std::memset(renameStorage.data(), 0, alignedUnits * sizeof(std::max_align_t));
        auto* pRename = reinterpret_cast<FILE_RENAME_INFO*>(renameStorage.data());
        pRename->ReplaceIfExists = aReplaceExisting ? TRUE : FALSE;
        pRename->RootDirectory = nullptr;
        pRename->FileNameLength = static_cast<DWORD>(destinationBytes);
        std::memcpy(pRename->FileName, destinationName.data(), destinationBytes);

        if (!::SetFileInformationByHandle(
                file,
                FileRenameInfo,
                pRename,
                static_cast<DWORD>(renameInfoSize)))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::RenameFailed;
        }

        // FILE_FLAG_WRITE_THROUGH is the authority for NTFS rename metadata.
        // FlushFileBuffers after the rename is an additional exact-handle barrier;
        // failure means publication is uncertain even though the name may exist.
        if (!::FlushFileBuffers(file))
        {
            ::CloseHandle(file);
            return PartyQuestStableStorageStatus::FlushFailed;
        }

        if (!::CloseHandle(file))
            return PartyQuestStableStorageStatus::CloseFailed;

        return PartyQuestStableStorageStatus::Success;
#else
        ec.clear();
        const auto destinationStatus = std::filesystem::symlink_status(destination, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
            return PartyQuestStableStorageStatus::InvalidPath;
        ec.clear();

        if (!aReplaceExisting && std::filesystem::exists(destinationStatus))
            return PartyQuestStableStorageStatus::RenameFailed;

        const auto fileFlush = FlushFile(source);
        if (fileFlush != PartyQuestStableStorageStatus::Success)
            return fileFlush;

        std::filesystem::rename(source, destination, ec);
        if (ec)
            return PartyQuestStableStorageStatus::RenameFailed;

        // Once rename has succeeded a failed directory fsync is an uncertain
        // publication, not a reason to pretend the old namespace is restored.
        // The caller must keep transaction/recovery authority and fail closed.
        return FlushDirectory(destination.parent_path());
#endif
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
}

PartyQuestStableStorageStatus PartyQuestStableStorage::RemoveFileDurably(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

    try
    {
        std::error_code ec;
        const auto path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
        // Delete disposition commonly becomes a namespace removal on handle
        // close. Until that close-time metadata durability is documented to the
        // same standard as NTFS write-through rename, fail closed.
        return PartyQuestStableStorageStatus::Unsupported;
#else
        struct stat status{};
        if (::lstat(path.c_str(), &status) != 0)
            return PartyQuestStableStorageStatus::OpenFailed;
        if (!S_ISREG(status.st_mode))
            return PartyQuestStableStorageStatus::NodeValidationFailed;

        if (::unlink(path.c_str()) != 0)
            return PartyQuestStableStorageStatus::RemoveFailed;

        // A failed parent fsync after unlink means deletion durability is
        // uncertain. Do not report success merely because the name disappeared
        // from the current process view.
        return FlushDirectory(path.parent_path());
#endif
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
}
