#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>

#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>
#include <Structs/Skyrim/PartyQuestStableStorage.h>

#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace
{
PartyQuestRuntimeApplyPersistenceStatus MapStableStorageStatus(
    PartyQuestStableStorageStatus aStatus) noexcept
{
    if (aStatus == PartyQuestStableStorageStatus::Success)
        return PartyQuestRuntimeApplyPersistenceStatus::Success;
    if (aStatus == PartyQuestStableStorageStatus::Unsupported)
        return PartyQuestRuntimeApplyPersistenceStatus::PowerLossDurabilityUnsupported;
    return PartyQuestRuntimeApplyPersistenceStatus::IoError;
}

bool WriteExactArchive(
    const std::filesystem::path& acPath,
    const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    if (!acBytes.empty())
    {
        file.write(
            reinterpret_cast<const char*>(acBytes.data()),
            static_cast<std::streamsize>(acBytes.size()));
    }

    file.flush();
    if (!file.good())
        return false;

    file.close();
    return file.good();
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
    if (size > PartyQuestDurableResourcePolicy::MaxRuntimeApplyArchiveBytes ||
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

bool IsExistingRegularOrMissing(
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (ec == std::errc::no_such_file_or_directory)
            return true;
        if (ec)
            return false;
        return status.type() == std::filesystem::file_type::not_found ||
            status.type() == std::filesystem::file_type::regular;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

PartyQuestRuntimeApplyPersistenceStatus
PartyQuestRuntimeApplyPersistence::SavePowerLossDurably(
    const std::filesystem::path& acPath,
    const PartyQuestRuntimeRecoveryState& acState,
    PartyQuestRuntimeApplyPersistenceHooks aHooks)
{
    if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
        return PartyQuestRuntimeApplyPersistenceStatus::ResourceLimitExceeded;

    const std::vector<uint8_t> encoded = Encode(acState);
    if (encoded.empty())
        return PartyQuestRuntimeApplyPersistenceStatus::InvalidData;

    std::filesystem::path path;
    try
    {
        std::error_code ec;
        path = std::filesystem::absolute(acPath, ec).lexically_normal();
        if (ec || path.empty() || path.parent_path().empty())
            return PartyQuestRuntimeApplyPersistenceStatus::IoError;

        const auto parentStatus = std::filesystem::symlink_status(path.parent_path(), ec);
        if (ec || parentStatus.type() != std::filesystem::file_type::directory)
            return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }
    catch (...)
    {
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    auto temporaryPath = path;
    temporaryPath += ".tmp";
    auto backupPath = path;
    backupPath += ".bak";

    // Never follow a pre-existing temp/backup final symlink. The enclosing
    // replica confinement remains the primary path authorization boundary.
    if (!IsExistingRegularOrMissing(temporaryPath) ||
        !IsExistingRegularOrMissing(backupPath) ||
        !IsExistingRegularOrMissing(path))
    {
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    if (!WriteExactArchive(temporaryPath, encoded))
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;

    // Flush before verification so the verified bytes are also the bytes that
    // have crossed the file-data durability barrier. PublishFileRename repeats
    // the exact-file flush immediately before namespace publication.
    auto stableStatus = PartyQuestStableStorage::FlushFile(temporaryPath);
    if (stableStatus != PartyQuestStableStorageStatus::Success)
        return MapStableStorageStatus(stableStatus);

    std::vector<uint8_t> verifiedBytes;
    if (!ReadExactArchive(temporaryPath, verifiedBytes))
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;

    const auto verified = Decode(verifiedBytes);
    if (verified.Status != PartyQuestRuntimeApplyPersistenceStatus::Success ||
        !verified.State || *verified.State != acState)
    {
        return PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
    }

    if (aHooks.Invoke(PartyQuestRuntimeApplyPersistenceBoundary::TemporaryVerified) ==
        PartyQuestRuntimeApplyPersistenceDirective::FailClosed)
    {
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    std::error_code ec;
    const bool hadPrimary = std::filesystem::exists(path, ec);
    if (ec)
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;

    if (hadPrimary)
    {
        stableStatus = PartyQuestStableStorage::PublishFileRename(
            path,
            backupPath,
            true);
        if (stableStatus != PartyQuestStableStorageStatus::Success)
            return MapStableStorageStatus(stableStatus);

        if (aHooks.Invoke(
                PartyQuestRuntimeApplyPersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestRuntimeApplyPersistenceDirective::FailClosed)
        {
            // The old primary is now durably reachable as .bak. Preserve both
            // backup and verified .tmp; an unproven rollback would reduce rather
            // than improve recovery authority.
            return PartyQuestRuntimeApplyPersistenceStatus::IoError;
        }
    }

    stableStatus = PartyQuestStableStorage::PublishFileRename(
        temporaryPath,
        path,
        false);
    if (stableStatus != PartyQuestStableStorageStatus::Success)
    {
        // If primary was already moved, .bak remains the old durable authority
        // and .tmp remains the verified candidate. Load/recovery decides which
        // state is safe; this method never fabricates a successful rollback.
        return MapStableStorageStatus(stableStatus);
    }

    if (aHooks.Invoke(PartyQuestRuntimeApplyPersistenceBoundary::TemporaryPublished) ==
        PartyQuestRuntimeApplyPersistenceDirective::FailClosed)
    {
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    return PartyQuestRuntimeApplyPersistenceStatus::Success;
}
