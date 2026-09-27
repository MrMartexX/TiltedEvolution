#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winternl.h>
#endif

namespace
{
#ifdef _WIN32
using NtFlushBuffersFileExFn = NTSTATUS(NTAPI*)(
    HANDLE,
    ULONG,
    PVOID,
    ULONG,
    PIO_STATUS_BLOCK);

bool IsMissingWindowsPathError(DWORD aError) noexcept
{
    return aError == ERROR_FILE_NOT_FOUND ||
        aError == ERROR_PATH_NOT_FOUND;
}

bool IsWindowsDirectoryPathWithoutReparse(
    const std::filesystem::path& acDirectory) noexcept
{
    const DWORD attributes = ::GetFileAttributesW(acDirectory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
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

PartyQuestStableStorageStatus FlushWindowsDirectoryDurably(
    const std::filesystem::path& acDirectory) noexcept
{
    const HANDLE directory = ::CreateFileW(
        acDirectory.c_str(),
        FILE_WRITE_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE)
        return PartyQuestStableStorageStatus::OpenFailed;

    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!::GetFileInformationByHandleEx(
            directory,
            FileAttributeTagInfo,
            &attributes,
            sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        ::CloseHandle(directory);
        return PartyQuestStableStorageStatus::NodeValidationFailed;
    }

    if (!IsWindowsNtfsHandle(directory))
    {
        ::CloseHandle(directory);
        return PartyQuestStableStorageStatus::Unsupported;
    }

    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        ::CloseHandle(directory);
        return PartyQuestStableStorageStatus::Unsupported;
    }

    const auto flush = reinterpret_cast<NtFlushBuffersFileExFn>(
        ::GetProcAddress(ntdll, "NtFlushBuffersFileEx"));
    if (!flush)
    {
        ::CloseHandle(directory);
        return PartyQuestStableStorageStatus::Unsupported;
    }

    // Microsoft documents Flags=0 as flushing cached file data + metadata and
    // synchronizing the underlying storage cache. The data-sync-only variant is
    // explicitly invalid for directory handles; the normal operation is the
    // reviewed directory-metadata barrier used here.
    IO_STATUS_BLOCK ioStatus{};
    const NTSTATUS status = flush(directory, 0, nullptr, 0, &ioStatus);
    if (status < 0 || ioStatus.Status < 0)
    {
        ::CloseHandle(directory);
        return PartyQuestStableStorageStatus::FlushFailed;
    }

    if (!::CloseHandle(directory))
        return PartyQuestStableStorageStatus::CloseFailed;

    return PartyQuestStableStorageStatus::Success;
}
#endif
} // namespace

PartyQuestStableStorageStatus PartyQuestStableStorage::EnsureDirectoryTreeDurably(
    const std::filesystem::path& acDirectory) noexcept
{
    if (acDirectory.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

    try
    {
        std::error_code ec;
        const auto directory =
            std::filesystem::absolute(acDirectory, ec).lexically_normal();
        if (ec || directory.empty() || !directory.is_absolute())
            return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
        if (directory == directory.root_path() || directory.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        std::filesystem::path current = directory.root_path();
        if (current.empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        for (const auto& component : directory.relative_path())
        {
            if (component.empty() || component == "." || component == "..")
                return PartyQuestStableStorageStatus::InvalidPath;

            const auto parent = current;
            current /= component;

            const DWORD attributes = ::GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const DWORD error = ::GetLastError();
                if (!IsMissingWindowsPathError(error))
                    return PartyQuestStableStorageStatus::OpenFailed;

                ec.clear();
                if (!std::filesystem::create_directory(current, ec) || ec)
                    return PartyQuestStableStorageStatus::CreateDirectoryFailed;

                if (!IsWindowsDirectoryPathWithoutReparse(current))
                    return PartyQuestStableStorageStatus::NodeValidationFailed;

                const auto parentFlush = FlushWindowsDirectoryDurably(parent);
                if (parentFlush != PartyQuestStableStorageStatus::Success)
                    return parentFlush;

                const auto childFlush = FlushWindowsDirectoryDurably(current);
                if (childFlush != PartyQuestStableStorageStatus::Success)
                    return childFlush;
            }
            else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                     (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return PartyQuestStableStorageStatus::NodeValidationFailed;
            }
        }

        // Promote an already-existing final directory too. This is essential for
        // workspaces that were created by an earlier process-crash-resilient
        // bootstrap: flushing the parent establishes the child name, and flushing
        // the child establishes the namespace that will contain durable sidecars.
        const auto parentFlush =
            FlushWindowsDirectoryDurably(directory.parent_path());
        if (parentFlush != PartyQuestStableStorageStatus::Success)
            return parentFlush;
        return FlushWindowsDirectoryDurably(directory);
#else
        std::filesystem::path current = directory.root_path();
        if (current.empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        auto rootStatus = std::filesystem::symlink_status(current, ec);
        if (ec || std::filesystem::is_symlink(rootStatus) ||
            !std::filesystem::is_directory(rootStatus))
        {
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        for (const auto& component : directory.relative_path())
        {
            if (component.empty() || component == "." || component == "..")
                return PartyQuestStableStorageStatus::InvalidPath;

            const auto parent = current;
            current /= component;

            ec.clear();
            auto status = std::filesystem::symlink_status(current, ec);
            const bool missing =
                status.type() == std::filesystem::file_type::not_found ||
                ec == std::errc::no_such_file_or_directory;
            if (missing)
            {
                ec.clear();
                if (!std::filesystem::create_directory(current, ec) || ec)
                    return PartyQuestStableStorageStatus::CreateDirectoryFailed;

                ec.clear();
                status = std::filesystem::symlink_status(current, ec);
            }

            if (ec || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
            {
                return PartyQuestStableStorageStatus::NodeValidationFailed;
            }

            // Persist the child name in its parent even when the directory was
            // created by an earlier crash-resilient path, then persist the child
            // directory's own metadata before descending into it.
            const auto parentFlush = FlushDirectory(parent);
            if (parentFlush != PartyQuestStableStorageStatus::Success)
                return parentFlush;

            const auto childFlush = FlushDirectory(current);
            if (childFlush != PartyQuestStableStorageStatus::Success)
                return childFlush;
        }

        return FlushDirectory(directory);
#endif
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
}
