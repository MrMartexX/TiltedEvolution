#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
bool IsMissingWindowsPathError(DWORD aError) noexcept
{
    return aError == ERROR_FILE_NOT_FOUND ||
        aError == ERROR_PATH_NOT_FOUND;
}

bool IsWindowsNtfsHandle(HANDLE aDirectory) noexcept
{
    wchar_t fileSystemName[MAX_PATH + 1]{};
    if (!::GetVolumeInformationByHandleW(
            aDirectory,
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

PartyQuestStableStorageStatus PartyQuestStableStorage::RemoveEmptyDirectoryDurably(
    const std::filesystem::path& acDirectory) noexcept
{
    if (acDirectory.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
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

        const auto parent = directory.parent_path();
        const DWORD parentAttributes = ::GetFileAttributesW(parent.c_str());
        if (parentAttributes == INVALID_FILE_ATTRIBUTES)
            return PartyQuestStableStorageStatus::OpenFailed;
        if ((parentAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (parentAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        const auto parentPromotion = EnsureDirectoryTreeDurably(parent);
        if (parentPromotion != PartyQuestStableStorageStatus::Success)
            return parentPromotion;

        const HANDLE handle = ::CreateFileW(
            directory.c_str(),
            DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return PartyQuestStableStorageStatus::OpenFailed;

        BY_HANDLE_FILE_INFORMATION information{};
        if (!::GetFileInformationByHandle(handle, &information) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            ::CloseHandle(handle);
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }
        if (!IsWindowsNtfsHandle(handle))
        {
            ::CloseHandle(handle);
            return PartyQuestStableStorageStatus::Unsupported;
        }

        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;
        if (!::SetFileInformationByHandle(
                handle,
                FileDispositionInfo,
                &disposition,
                sizeof(disposition)))
        {
            // A non-empty directory is expected to fail here. Do not fall back
            // to recursive deletion; this primitive has authority over one
            // already-empty final directory only.
            ::CloseHandle(handle);
            return PartyQuestStableStorageStatus::RemoveFailed;
        }
        if (!::CloseHandle(handle))
            return PartyQuestStableStorageStatus::CloseFailed;

        if (::GetFileAttributesW(directory.c_str()) != INVALID_FILE_ATTRIBUTES)
            return PartyQuestStableStorageStatus::RemoveFailed;
        const DWORD removeError = ::GetLastError();
        if (!IsMissingWindowsPathError(removeError))
            return PartyQuestStableStorageStatus::RemoveFailed;

        return EnsureDirectoryTreeDurably(parent);
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
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
