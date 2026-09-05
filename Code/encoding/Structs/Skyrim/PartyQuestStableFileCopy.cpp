#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <array>
#include <cerrno>
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
#ifdef _WIN32
bool IsRegularSingleLinkWindowsFile(
    HANDLE aFile,
    BY_HANDLE_FILE_INFORMATION& aInformation) noexcept
{
    return ::GetFileInformationByHandle(aFile, &aInformation) &&
        (aInformation.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
        aInformation.nNumberOfLinks == 1;
}

bool IsSameWindowsFile(
    const BY_HANDLE_FILE_INFORMATION& acLeft,
    const BY_HANDLE_FILE_INFORMATION& acRight) noexcept
{
    return acLeft.dwVolumeSerialNumber == acRight.dwVolumeSerialNumber &&
        acLeft.nFileIndexHigh == acRight.nFileIndexHigh &&
        acLeft.nFileIndexLow == acRight.nFileIndexLow;
}
#endif
} // namespace

PartyQuestStableStorageStatus PartyQuestStableStorage::CopyFileDurably(
    const std::filesystem::path& acSource,
    const std::filesystem::path& acDestination) noexcept
{
    if (acSource.empty() || acDestination.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    try
    {
        std::error_code ec;
        const auto source =
            std::filesystem::absolute(acSource, ec).lexically_normal();
        if (ec || source.empty() || source.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        ec.clear();
        const auto destination =
            std::filesystem::absolute(acDestination, ec).lexically_normal();
        if (ec || destination.empty() || destination.parent_path().empty() ||
            source == destination)
        {
            return PartyQuestStableStorageStatus::InvalidPath;
        }

        // Copy has no namespace-creation authority. The destination parent must
        // already exist; promoting it through the reviewed directory primitive
        // proves both its topology and NTFS metadata-flush support before the
        // destination can be created or truncated.
        const DWORD parentAttributes =
            ::GetFileAttributesW(destination.parent_path().c_str());
        if (parentAttributes == INVALID_FILE_ATTRIBUTES)
            return PartyQuestStableStorageStatus::OpenFailed;
        if ((parentAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (parentAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }
        const auto parentPromotion = EnsureDirectoryTreeDurably(
            destination.parent_path());
        if (parentPromotion != PartyQuestStableStorageStatus::Success)
            return parentPromotion;

        // Deny write/delete sharing for the source so the copied byte sequence
        // cannot change underneath the streaming operation.
        const HANDLE sourceFile = ::CreateFileW(
            source.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
                FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (sourceFile == INVALID_HANDLE_VALUE)
            return PartyQuestStableStorageStatus::OpenFailed;

        BY_HANDLE_FILE_INFORMATION sourceInformation{};
        if (!IsRegularSingleLinkWindowsFile(sourceFile, sourceInformation))
        {
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        // OPEN_ALWAYS intentionally avoids truncating an existing destination
        // before its exact file identity is compared with the already-open
        // source. A hard-link alias therefore cannot destroy source evidence.
        const HANDLE destinationFile = ::CreateFileW(
            destination.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL |
                FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN |
                FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (destinationFile == INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::OpenFailed;
        }

        BY_HANDLE_FILE_INFORMATION destinationInformation{};
        if (!IsRegularSingleLinkWindowsFile(
                destinationFile,
                destinationInformation) ||
            IsSameWindowsFile(sourceInformation, destinationInformation))
        {
            ::CloseHandle(destinationFile);
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        LARGE_INTEGER beginning{};
        if (!::SetFilePointerEx(
                destinationFile,
                beginning,
                nullptr,
                FILE_BEGIN) ||
            !::SetEndOfFile(destinationFile))
        {
            ::CloseHandle(destinationFile);
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::WriteFailed;
        }

        std::array<uint8_t, 64 * 1024> buffer{};
        for (;;)
        {
            DWORD readCount = 0;
            if (!::ReadFile(
                    sourceFile,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &readCount,
                    nullptr))
            {
                ::CloseHandle(destinationFile);
                ::CloseHandle(sourceFile);
                return PartyQuestStableStorageStatus::WriteFailed;
            }
            if (readCount == 0)
                break;

            DWORD offset = 0;
            while (offset < readCount)
            {
                DWORD written = 0;
                if (!::WriteFile(
                        destinationFile,
                        buffer.data() + offset,
                        readCount - offset,
                        &written,
                        nullptr) ||
                    written == 0)
                {
                    ::CloseHandle(destinationFile);
                    ::CloseHandle(sourceFile);
                    return PartyQuestStableStorageStatus::WriteFailed;
                }
                offset += written;
            }
        }

        if (!::FlushFileBuffers(destinationFile))
        {
            ::CloseHandle(destinationFile);
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::FlushFailed;
        }
        if (!::CloseHandle(destinationFile))
        {
            ::CloseHandle(sourceFile);
            return PartyQuestStableStorageStatus::CloseFailed;
        }
        if (!::CloseHandle(sourceFile))
            return PartyQuestStableStorageStatus::CloseFailed;

        // FILE_FLAG_WRITE_THROUGH + FlushFileBuffers establishes destination
        // data; the explicit directory promotion is the separate stable-name
        // barrier for a newly created destination entry.
        return EnsureDirectoryTreeDurably(destination.parent_path());
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
#else
    try
    {
        std::error_code ec;
        const auto source =
            std::filesystem::absolute(acSource, ec).lexically_normal();
        if (ec || source.empty() || source.parent_path().empty())
            return PartyQuestStableStorageStatus::InvalidPath;

        ec.clear();
        const auto destination =
            std::filesystem::absolute(acDestination, ec).lexically_normal();
        if (ec || destination.empty() || destination.parent_path().empty() ||
            source == destination)
        {
            return PartyQuestStableStorageStatus::InvalidPath;
        }

        const int sourceFd = ::open(
            source.c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (sourceFd < 0)
            return PartyQuestStableStorageStatus::OpenFailed;

        struct stat sourceStatus{};
        if (::fstat(sourceFd, &sourceStatus) != 0 ||
            !S_ISREG(sourceStatus.st_mode))
        {
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        // Open without O_TRUNC first. That lets us compare the exact destination
        // inode against the already-open source before any hard-link alias could
        // truncate the source evidence.
        const int destinationFd = ::open(
            destination.c_str(),
            O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
        if (destinationFd < 0)
        {
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::OpenFailed;
        }

        struct stat destinationStatus{};
        if (::fstat(destinationFd, &destinationStatus) != 0 ||
            !S_ISREG(destinationStatus.st_mode) ||
            (sourceStatus.st_dev == destinationStatus.st_dev &&
             sourceStatus.st_ino == destinationStatus.st_ino))
        {
            ::close(destinationFd);
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::NodeValidationFailed;
        }

        if (::ftruncate(destinationFd, 0) != 0)
        {
            ::close(destinationFd);
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::WriteFailed;
        }

        std::array<uint8_t, 64 * 1024> buffer{};
        for (;;)
        {
            ssize_t readCount = ::read(sourceFd, buffer.data(), buffer.size());
            if (readCount < 0 && errno == EINTR)
                continue;
            if (readCount < 0)
            {
                ::close(destinationFd);
                ::close(sourceFd);
                return PartyQuestStableStorageStatus::WriteFailed;
            }
            if (readCount == 0)
                break;

            size_t offset = 0;
            const size_t available = static_cast<size_t>(readCount);
            while (offset < available)
            {
                const ssize_t written = ::write(
                    destinationFd,
                    buffer.data() + offset,
                    available - offset);
                if (written < 0 && errno == EINTR)
                    continue;
                if (written <= 0)
                {
                    ::close(destinationFd);
                    ::close(sourceFd);
                    return PartyQuestStableStorageStatus::WriteFailed;
                }
                offset += static_cast<size_t>(written);
            }
        }

        if (::fsync(destinationFd) != 0)
        {
            ::close(destinationFd);
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::FlushFailed;
        }

        if (::close(destinationFd) != 0)
        {
            ::close(sourceFd);
            return PartyQuestStableStorageStatus::CloseFailed;
        }
        if (::close(sourceFd) != 0)
            return PartyQuestStableStorageStatus::CloseFailed;

        return FlushDirectory(destination.parent_path());
    }
    catch (...)
    {
        return PartyQuestStableStorageStatus::InvalidPath;
    }
#endif
}
