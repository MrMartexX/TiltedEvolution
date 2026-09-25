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

    // INV-DURABILITY-001: the new recovery candidate, including its namespace
    // entry, must already survive power loss before the old primary authority is
    // moved aside. File-data flush alone is insufficient for a newly created
    // POSIX directory entry; WriteFileDurably includes that publication barrier.
    auto stableStatus = PartyQuestStableStorage::WriteFileDurably(
        temporaryPath,
        encoded.data(),
        encoded.size());
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
            // The old primary is now durably reachable as .bak and the verified
            // candidate is already durably reachable as .tmp. Preserve both;
            // an unproven rollback would reduce rather than improve recovery
            // authority.
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
        // The new primary has crossed the platform publication barrier already.
        // Reporting failure keeps callers fail-closed while recovery can inspect
        // the exact durable primary rather than guessing that publication failed.
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    return PartyQuestRuntimeApplyPersistenceStatus::Success;
}
