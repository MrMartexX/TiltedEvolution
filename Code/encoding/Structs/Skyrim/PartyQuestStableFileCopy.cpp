#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <array>
#include <cerrno>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

PartyQuestStableStorageStatus PartyQuestStableStorage::CopyFileDurably(
    const std::filesystem::path& acSource,
    const std::filesystem::path& acDestination) noexcept
{
    if (acSource.empty() || acDestination.empty())
        return PartyQuestStableStorageStatus::InvalidPath;

#ifdef _WIN32
    // Full Windows destructive-restore durability is still blocked on durable
    // directory-tree and delete semantics. Keep rollback-copy authority aligned
    // with that platform boundary rather than implying restore readiness from a
    // file-only primitive.
    return PartyQuestStableStorageStatus::Unsupported;
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
