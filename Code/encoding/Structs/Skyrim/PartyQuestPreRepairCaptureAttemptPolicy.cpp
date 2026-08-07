#include <Structs/Skyrim/PartyQuestPreRepairCaptureAttemptPolicy.h>

#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <system_error>

namespace
{
PartyQuestPreRepairCaptureAttemptDecision Fail(
    PartyQuestPreRepairCaptureAttemptStatus aStatus,
    size_t aAttemptCount = 0) noexcept
{
    PartyQuestPreRepairCaptureAttemptDecision result;
    result.Status = aStatus;
    result.ExistingAttemptCount = aAttemptCount;
    return result;
}

std::string FormatAttemptPrefix(
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision) noexcept
{
    std::array<char, 64> buffer{};
    const int written = std::snprintf(
        buffer.data(),
        buffer.size(),
        "STR_PreRepair_T%016llX_R%016llX_A",
        static_cast<unsigned long long>(aTransactionId),
        static_cast<unsigned long long>(aTargetWorldRevision));
    return written > 0 && static_cast<size_t>(written) < buffer.size()
        ? std::string(buffer.data(), static_cast<size_t>(written))
        : std::string{};
}

bool IsHexNonce(const std::string& acValue) noexcept
{
    if (acValue.size() != 16)
        return false;
    for (const char value : acValue)
    {
        if (!((value >= '0' && value <= '9') ||
                (value >= 'A' && value <= 'F')))
        {
            return false;
        }
    }
    return true;
}
} // namespace

PartyQuestPreRepairCaptureAttemptDecision
PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
    const PartyQuestCoopSavePaths& acPaths,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision) noexcept
{
    try
    {
        if (aTransactionId == 0 || aTargetWorldRevision == 0)
            return Fail(PartyQuestPreRepairCaptureAttemptStatus::InvalidContext);

        if (acPaths.PlayerDirectory.empty() ||
            acPaths.SavesDirectory.empty() ||
            !acPaths.PlayerDirectory.is_absolute() ||
            !acPaths.SavesDirectory.is_absolute() ||
            !PartyQuestReplicaFilePlanner::IsContainedBy(
                acPaths.PlayerDirectory,
                acPaths.SavesDirectory))
        {
            return Fail(PartyQuestPreRepairCaptureAttemptStatus::InvalidLayout);
        }

        std::error_code ec;
        const auto status = std::filesystem::symlink_status(
            acPaths.SavesDirectory,
            ec);
        if (ec || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_directory(status))
        {
            return Fail(
                PartyQuestPreRepairCaptureAttemptStatus::SaveDirectoryUnavailable);
        }

        const std::string prefix = FormatAttemptPrefix(
            aTransactionId,
            aTargetWorldRevision);
        if (prefix.empty())
            return Fail(PartyQuestPreRepairCaptureAttemptStatus::InvalidContext);

        std::set<std::string> attempts;
        size_t inspected{};
        std::filesystem::directory_iterator iterator(acPaths.SavesDirectory, ec);
        const std::filesystem::directory_iterator end;
        if (ec)
        {
            return Fail(
                PartyQuestPreRepairCaptureAttemptStatus::DirectoryInspectionFailed);
        }

        for (; iterator != end; iterator.increment(ec))
        {
            if (ec)
            {
                return Fail(
                    PartyQuestPreRepairCaptureAttemptStatus::DirectoryInspectionFailed,
                    attempts.size());
            }
            if (++inspected > MaxInspectedDirectoryEntries)
            {
                return Fail(
                    PartyQuestPreRepairCaptureAttemptStatus::DirectoryEntryLimitExceeded,
                    attempts.size());
            }

            const auto extension = iterator->path().extension().generic_string();
            if (extension != ".ess" && extension != ".skse")
                continue;

            const auto stem = iterator->path().stem().generic_string();
            if (stem.size() != prefix.size() + 16 ||
                stem.compare(0, prefix.size(), prefix) != 0 ||
                !IsHexNonce(stem.substr(prefix.size())))
            {
                continue;
            }

            attempts.emplace(stem);
            if (attempts.size() >= MaxAttemptsPerTransactionRevision)
            {
                return Fail(
                    PartyQuestPreRepairCaptureAttemptStatus::AttemptLimitExceeded,
                    attempts.size());
            }
        }

        if (ec)
        {
            return Fail(
                PartyQuestPreRepairCaptureAttemptStatus::DirectoryInspectionFailed,
                attempts.size());
        }

        PartyQuestPreRepairCaptureAttemptDecision result;
        result.Status = PartyQuestPreRepairCaptureAttemptStatus::Ready;
        result.ExistingAttemptCount = attempts.size();
        return result;
    }
    catch (...)
    {
        return Fail(
            PartyQuestPreRepairCaptureAttemptStatus::DirectoryInspectionFailed);
    }
}
