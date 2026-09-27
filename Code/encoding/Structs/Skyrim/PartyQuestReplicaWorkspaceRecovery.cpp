#include <Structs/Skyrim/PartyQuestReplicaWorkspaceRecovery.h>
#include <Structs/Skyrim/PartyQuestReplicaFiles.h>

#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
using Status = PartyQuestReplicaWorkspaceRecoveryStatus;
using Result = PartyQuestReplicaWorkspaceRecoveryResult;

struct RecoveryDeadline
{
    uint64_t StartedAt{};
    uint64_t ExpiresAt{};
};

struct RecoveryCandidate
{
    std::filesystem::path Source;
    std::filesystem::path Destination;
    uint64_t Size{};
};

Result MakeResult(
    Status aStatus,
    size_t aInspected,
    size_t aQuarantined = 0,
    size_t aQuarantineFiles = 0,
    uint64_t aQuarantineBytes = 0) noexcept
{
    Result result;
    result.Status = aStatus;
    result.InspectedEntries = aInspected;
    result.QuarantinedFiles = aQuarantined;
    result.QuarantineFiles = aQuarantineFiles;
    result.QuarantineBytes = aQuarantineBytes;
    return result;
}

bool BuildDeadline(
    const PartyQuestReplicaWorkspaceRecoveryHooks& acHooks,
    RecoveryDeadline& aDeadline) noexcept
{
    const uint64_t now = acHooks.NowTicks();
    if (now == 0 ||
        now > std::numeric_limits<uint64_t>::max() -
            PartyQuestReplicaWorkspaceRecovery::MaxRecoveryNanoseconds)
    {
        return false;
    }
    aDeadline.StartedAt = now;
    aDeadline.ExpiresAt =
        now + PartyQuestReplicaWorkspaceRecovery::MaxRecoveryNanoseconds;
    return true;
}

bool DeadlineExceeded(
    const PartyQuestReplicaWorkspaceRecoveryHooks& acHooks,
    const RecoveryDeadline& acDeadline) noexcept
{
    const uint64_t now = acHooks.NowTicks();
    return now == 0 || now < acDeadline.StartedAt ||
        now >= acDeadline.ExpiresAt;
}

bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

bool ParseDecimal(std::string_view aText, uint64_t& aValue) noexcept
{
    if (aText.empty())
        return false;
    const auto parsed = std::from_chars(
        aText.data(), aText.data() + aText.size(), aValue);
    return parsed.ec == std::errc{} && parsed.ptr == aText.data() + aText.size();
}

bool IsExactCopyTemporaryName(const std::filesystem::path& acPath)
{
    constexpr std::string_view marker = ".tpqtmp-";
    const std::string name = acPath.filename().generic_string();
    const size_t markerPosition = name.rfind(marker);
    if (markerPosition == std::string::npos || markerPosition == 0)
        return false;

    const size_t nonceStart = markerPosition + marker.size();
    const size_t separator = name.find('-', nonceStart);
    if (separator == std::string::npos ||
        name.find('-', separator + 1) != std::string::npos)
    {
        return false;
    }

    uint64_t nonce{};
    uint64_t operationIndex{};
    return ParseDecimal(
               std::string_view(name).substr(nonceStart, separator - nonceStart),
               nonce) &&
        nonce != 0 &&
        ParseDecimal(std::string_view(name).substr(separator + 1), operationIndex);
}

bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acCandidate) noexcept
{
    try
    {
        return PartyQuestReplicaFilePlanner::IsContainedBy(
            acRoot.lexically_normal(), acCandidate.lexically_normal());
    }
    catch (...)
    {
        return false;
    }
}

bool IsSafeSingleLinkFile(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acPath) noexcept
{
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(acPath, ec);
    if (ec || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status))
    {
        return false;
    }

    ec.clear();
    if (std::filesystem::hard_link_count(acPath, ec) != 1 || ec)
        return false;

    ec.clear();
    const auto canonical = std::filesystem::weakly_canonical(acPath, ec);
    return !ec && !canonical.empty() && IsInside(acRoot, canonical);
}

std::optional<uint64_t> ObserveFileSize(
    const std::filesystem::path& acPath) noexcept
{
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(acPath, ec);
    if (ec || size > std::numeric_limits<uint64_t>::max())
        return std::nullopt;
    return static_cast<uint64_t>(size);
}

bool IsAllowedQuarantineRelative(
    const std::filesystem::path& acRelative) noexcept
{
    try
    {
        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(acRelative))
            return false;
        const auto first = acRelative.begin();
        if (first == acRelative.end())
            return false;
        return *first == "saves" || *first == "sidecars" ||
            *first == "checkpoints";
    }
    catch (...)
    {
        return false;
    }
}

std::optional<std::filesystem::path> PrepareDirectory(
    const std::filesystem::path& acCanonicalPlayer,
    const std::filesystem::path& acRelative) noexcept
{
    try
    {
        if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(acRelative))
            return std::nullopt;

        std::error_code ec;
        auto current = acCanonicalPlayer;
        for (const auto& component : acRelative)
        {
            current /= component;
            auto status = std::filesystem::symlink_status(current, ec);
            if (status.type() == std::filesystem::file_type::not_found ||
                IsMissingError(ec))
            {
                ec.clear();
                if (!std::filesystem::create_directory(current, ec) || ec)
                    return std::nullopt;
                status = std::filesystem::symlink_status(current, ec);
            }
            if (ec || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
            {
                return std::nullopt;
            }

            ec.clear();
            const auto canonical = std::filesystem::weakly_canonical(current, ec);
            if (ec || canonical.empty() || canonical != current.lexically_normal())
                return std::nullopt;
        }
        return current.lexically_normal();
    }
    catch (...)
    {
        return std::nullopt;
    }
}
} // namespace

uint64_t PartyQuestReplicaWorkspaceRecoveryHooks::NowTicks() const noexcept
{
    if (MonotonicNow)
        return MonotonicNow(Context);

    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    return now > 0 ? static_cast<uint64_t>(now) : 1;
}

PartyQuestReplicaWorkspaceRecoveryResult
PartyQuestReplicaWorkspaceRecovery::QuarantineOrphanCopyTemporaries(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId,
    const PartyQuestReplicaWorkspaceLease& acLease,
    PartyQuestReplicaWorkspaceRecoveryHooks aHooks) noexcept
{
    try
    {
        if (!PartyQuestCoopSaveLayout::Matches(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return MakeResult(Status::InvalidLayout, 0);
        }

        RecoveryDeadline deadline;
        if (!BuildDeadline(aHooks, deadline))
            return MakeResult(Status::DeadlineExceeded, 0);

        if (!acLease.Protects(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return MakeResult(Status::InvalidLease, 0);
        }
        if (DeadlineExceeded(aHooks, deadline))
            return MakeResult(Status::DeadlineExceeded, 0);

        std::error_code ec;
        const auto canonicalPlayer = std::filesystem::weakly_canonical(
            acPaths.PlayerDirectory, ec);
        if (ec || canonicalPlayer.empty())
            return MakeResult(Status::InvalidNamespace, 0);

        const std::array<std::filesystem::path, 3> scanRoots{
            canonicalPlayer / "saves",
            canonicalPlayer / "sidecars",
            canonicalPlayer / "checkpoints"};
        std::vector<std::filesystem::path> candidates;
        size_t inspected{};

        for (const auto& root : scanRoots)
        {
            if (DeadlineExceeded(aHooks, deadline))
                return MakeResult(Status::DeadlineExceeded, inspected);
            ec.clear();
            const auto rootStatus = std::filesystem::symlink_status(root, ec);
            if (rootStatus.type() == std::filesystem::file_type::not_found ||
                IsMissingError(ec))
            {
                continue;
            }
            if (ec || std::filesystem::is_symlink(rootStatus) ||
                !std::filesystem::is_directory(rootStatus))
            {
                return MakeResult(Status::InvalidNamespace, inspected);
            }

            std::filesystem::recursive_directory_iterator iterator(root, ec);
            const std::filesystem::recursive_directory_iterator end;
            if (ec)
                return MakeResult(Status::IoError, inspected);

            for (; iterator != end; iterator.increment(ec))
            {
                if (ec)
                    return MakeResult(Status::IoError, inspected);
                if (++inspected > MaxInspectedEntries)
                    return MakeResult(Status::EntryLimitExceeded, inspected);
                if (DeadlineExceeded(aHooks, deadline))
                    return MakeResult(Status::DeadlineExceeded, inspected);

                const auto status = iterator->symlink_status(ec);
                if (ec)
                    return MakeResult(Status::IoError, inspected);
                if (std::filesystem::is_symlink(status))
                    continue;
                if (!std::filesystem::is_regular_file(status) ||
                    !IsExactCopyTemporaryName(iterator->path()))
                {
                    continue;
                }
                if (candidates.size() >= MaxCandidates)
                {
                    return MakeResult(
                        Status::QuarantineQuotaExceeded,
                        inspected);
                }
                candidates.push_back(iterator->path().lexically_normal());
            }
            if (ec)
                return MakeResult(Status::IoError, inspected);
        }

        const std::filesystem::path quarantinePath = canonicalPlayer /
            "metadata" / "orphan_copy_quarantine";
        size_t quarantineFiles{};
        uint64_t quarantineBytes{};
        ec.clear();
        const auto quarantineStatus = std::filesystem::symlink_status(
            quarantinePath, ec);
        if (quarantineStatus.type() != std::filesystem::file_type::not_found &&
            !IsMissingError(ec))
        {
            if (ec || std::filesystem::is_symlink(quarantineStatus) ||
                !std::filesystem::is_directory(quarantineStatus))
            {
                return MakeResult(Status::QuarantineInvalid, inspected);
            }

            std::filesystem::recursive_directory_iterator iterator(
                quarantinePath, ec);
            const std::filesystem::recursive_directory_iterator end;
            if (ec)
                return MakeResult(Status::IoError, inspected);
            for (; iterator != end; iterator.increment(ec))
            {
                if (ec)
                    return MakeResult(Status::IoError, inspected);
                if (++inspected > MaxInspectedEntries)
                    return MakeResult(Status::EntryLimitExceeded, inspected);
                if (DeadlineExceeded(aHooks, deadline))
                {
                    return MakeResult(
                        Status::DeadlineExceeded,
                        inspected,
                        0,
                        quarantineFiles,
                        quarantineBytes);
                }

                const auto relative = iterator->path().lexically_relative(
                    quarantinePath);
                const auto status = iterator->symlink_status(ec);
                if (ec || std::filesystem::is_symlink(status) ||
                    !IsAllowedQuarantineRelative(relative))
                {
                    return MakeResult(
                        Status::QuarantineInvalid,
                        inspected,
                        0,
                        quarantineFiles,
                        quarantineBytes);
                }
                if (std::filesystem::is_directory(status))
                    continue;
                if (!std::filesystem::is_regular_file(status) ||
                    !IsExactCopyTemporaryName(iterator->path()) ||
                    !IsSafeSingleLinkFile(quarantinePath, iterator->path()))
                {
                    return MakeResult(
                        Status::QuarantineInvalid,
                        inspected,
                        0,
                        quarantineFiles,
                        quarantineBytes);
                }

                const auto size = ObserveFileSize(iterator->path());
                if (!size)
                    return MakeResult(Status::IoError, inspected);
                if (*size > PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes ||
                    quarantineFiles >= MaxQuarantineFiles ||
                    *size > MaxQuarantineBytes - quarantineBytes)
                {
                    return MakeResult(
                        Status::QuarantineQuotaExceeded,
                        inspected,
                        0,
                        quarantineFiles,
                        quarantineBytes);
                }
                ++quarantineFiles;
                quarantineBytes += *size;
            }
            if (ec)
                return MakeResult(Status::IoError, inspected);
        }
        else if (ec && !IsMissingError(ec))
        {
            return MakeResult(Status::IoError, inspected);
        }

        if (candidates.empty())
        {
            return MakeResult(
                Status::Clean,
                inspected,
                0,
                quarantineFiles,
                quarantineBytes);
        }

        std::vector<RecoveryCandidate> admitted;
        admitted.reserve(candidates.size());
        size_t projectedFiles = quarantineFiles;
        uint64_t projectedBytes = quarantineBytes;
        for (const auto& candidate : candidates)
        {
            if (DeadlineExceeded(aHooks, deadline))
            {
                return MakeResult(
                    Status::DeadlineExceeded,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }
            if (!IsSafeSingleLinkFile(canonicalPlayer, candidate))
            {
                return MakeResult(
                    Status::CandidateUnsafe,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }

            const auto relative = candidate.lexically_relative(canonicalPlayer);
            if (!IsAllowedQuarantineRelative(relative))
            {
                return MakeResult(
                    Status::CandidateUnsafe,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }

            const auto size = ObserveFileSize(candidate);
            if (!size)
            {
                return MakeResult(
                    Status::IoError,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }
            if (*size > PartyQuestReplicaResourcePolicy::MaxIndividualFileBytes ||
                projectedFiles >= MaxQuarantineFiles ||
                *size > MaxQuarantineBytes - projectedBytes)
            {
                return MakeResult(
                    Status::QuarantineQuotaExceeded,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }

            const auto destination = quarantinePath / relative;
            ec.clear();
            const auto destinationStatus = std::filesystem::symlink_status(
                destination, ec);
            if (destinationStatus.type() != std::filesystem::file_type::not_found ||
                (!IsMissingError(ec) && ec))
            {
                return MakeResult(
                    Status::DestinationConflict,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }
            admitted.push_back({candidate, destination, *size});
            ++projectedFiles;
            projectedBytes += *size;
        }

        const auto quarantineRoot = PrepareDirectory(
            canonicalPlayer,
            std::filesystem::path("metadata") / "orphan_copy_quarantine");
        if (!quarantineRoot)
        {
            return MakeResult(
                Status::InvalidNamespace,
                inspected,
                0,
                quarantineFiles,
                quarantineBytes);
        }

        for (const auto& candidate : admitted)
        {
            const auto relative = candidate.Destination.parent_path()
                                      .lexically_relative(canonicalPlayer);
            const auto parent = PrepareDirectory(canonicalPlayer, relative);
            if (!parent)
            {
                return MakeResult(
                    Status::InvalidNamespace,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }
            ec.clear();
            const auto status = std::filesystem::symlink_status(
                candidate.Destination, ec);
            if (status.type() != std::filesystem::file_type::not_found ||
                (!IsMissingError(ec) && ec))
            {
                return MakeResult(
                    Status::DestinationConflict,
                    inspected,
                    0,
                    quarantineFiles,
                    quarantineBytes);
            }
        }

        size_t quarantined{};
        for (const auto& candidate : admitted)
        {
            if (DeadlineExceeded(aHooks, deadline))
            {
                return MakeResult(
                    Status::DeadlineExceeded,
                    inspected,
                    quarantined,
                    quarantineFiles,
                    quarantineBytes);
            }
            const auto size = ObserveFileSize(candidate.Source);
            if (!IsSafeSingleLinkFile(canonicalPlayer, candidate.Source) ||
                !size || *size != candidate.Size)
            {
                return MakeResult(
                    Status::CandidateUnsafe,
                    inspected,
                    quarantined,
                    quarantineFiles,
                    quarantineBytes);
            }
            ec.clear();
            std::filesystem::rename(
                candidate.Source,
                candidate.Destination,
                ec);
            if (ec)
            {
                return MakeResult(
                    Status::IoError,
                    inspected,
                    quarantined,
                    quarantineFiles,
                    quarantineBytes);
            }
            ++quarantined;
            ++quarantineFiles;
            quarantineBytes += candidate.Size;
        }

        return MakeResult(
            Status::Quarantined,
            inspected,
            quarantined,
            quarantineFiles,
            quarantineBytes);
    }
    catch (...)
    {
        return MakeResult(Status::IoError, 0);
    }
}
