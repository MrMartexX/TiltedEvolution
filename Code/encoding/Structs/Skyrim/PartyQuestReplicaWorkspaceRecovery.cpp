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

Result Fail(Status aStatus, size_t aInspected, size_t aQuarantined = 0) noexcept
{
    Result result;
    result.Status = aStatus;
    result.InspectedEntries = aInspected;
    result.QuarantinedFiles = aQuarantined;
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
            return Fail(Status::InvalidLayout, 0);
        }

        RecoveryDeadline deadline;
        if (!BuildDeadline(aHooks, deadline))
            return Fail(Status::DeadlineExceeded, 0);

        if (!acLease.Protects(
                acPaths,
                acCampaignId,
                acPlayerProfileId))
        {
            return Fail(Status::InvalidLease, 0);
        }
        if (DeadlineExceeded(aHooks, deadline))
            return Fail(Status::DeadlineExceeded, 0);

        std::error_code ec;
        const auto canonicalPlayer = std::filesystem::weakly_canonical(
            acPaths.PlayerDirectory, ec);
        if (ec || canonicalPlayer.empty())
            return Fail(Status::InvalidNamespace, 0);

        const std::array<std::filesystem::path, 3> scanRoots{
            canonicalPlayer / "saves",
            canonicalPlayer / "sidecars",
            canonicalPlayer / "checkpoints"};
        std::vector<std::filesystem::path> candidates;
        size_t inspected{};

        for (const auto& root : scanRoots)
        {
            if (DeadlineExceeded(aHooks, deadline))
                return Fail(Status::DeadlineExceeded, inspected);
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
                return Fail(Status::InvalidNamespace, inspected);
            }

            std::filesystem::recursive_directory_iterator iterator(root, ec);
            const std::filesystem::recursive_directory_iterator end;
            if (ec)
                return Fail(Status::IoError, inspected);

            for (; iterator != end; iterator.increment(ec))
            {
                if (ec)
                    return Fail(Status::IoError, inspected);
                if (++inspected > MaxInspectedEntries)
                    return Fail(Status::EntryLimitExceeded, inspected);
                if (DeadlineExceeded(aHooks, deadline))
                    return Fail(Status::DeadlineExceeded, inspected);

                const auto status = iterator->symlink_status(ec);
                if (ec)
                    return Fail(Status::IoError, inspected);
                if (std::filesystem::is_symlink(status))
                    continue;
                if (!std::filesystem::is_regular_file(status) ||
                    !IsExactCopyTemporaryName(iterator->path()))
                {
                    continue;
                }
                if (candidates.size() >= MaxCandidates)
                    return Fail(Status::EntryLimitExceeded, inspected);
                candidates.push_back(iterator->path().lexically_normal());
            }
            if (ec)
                return Fail(Status::IoError, inspected);
        }

        if (candidates.empty())
            return Fail(Status::Clean, inspected);

        if (DeadlineExceeded(aHooks, deadline))
            return Fail(Status::DeadlineExceeded, inspected);

        const auto quarantineRoot = PrepareDirectory(
            canonicalPlayer,
            std::filesystem::path("metadata") / "orphan_copy_quarantine");
        if (!quarantineRoot)
            return Fail(Status::InvalidNamespace, inspected);

        std::vector<std::filesystem::path> destinations;
        destinations.reserve(candidates.size());
        for (const auto& candidate : candidates)
        {
            if (DeadlineExceeded(aHooks, deadline))
                return Fail(Status::DeadlineExceeded, inspected);
            if (!IsSafeSingleLinkFile(canonicalPlayer, candidate))
                return Fail(Status::CandidateUnsafe, inspected);

            const auto relative = candidate.lexically_relative(canonicalPlayer);
            if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
                return Fail(Status::CandidateUnsafe, inspected);

            const auto parent = PrepareDirectory(
                canonicalPlayer,
                (std::filesystem::path("metadata") /
                    "orphan_copy_quarantine" / relative.parent_path())
                    .lexically_normal());
            if (!parent)
                return Fail(Status::InvalidNamespace, inspected);

            const auto destination = *parent / relative.filename();
            ec.clear();
            const auto destinationStatus = std::filesystem::symlink_status(
                destination, ec);
            if (destinationStatus.type() != std::filesystem::file_type::not_found ||
                (!IsMissingError(ec) && ec))
            {
                return Fail(Status::DestinationConflict, inspected);
            }
            destinations.push_back(destination);
        }

        size_t quarantined{};
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (DeadlineExceeded(aHooks, deadline))
                return Fail(Status::DeadlineExceeded, inspected, quarantined);
            if (!IsSafeSingleLinkFile(canonicalPlayer, candidates[i]))
                return Fail(Status::CandidateUnsafe, inspected, quarantined);
            ec.clear();
            std::filesystem::rename(candidates[i], destinations[i], ec);
            if (ec)
                return Fail(Status::IoError, inspected, quarantined);
            ++quarantined;
        }

        return Fail(Status::Quarantined, inspected, quarantined);
    }
    catch (...)
    {
        return Fail(Status::IoError, 0);
    }
}
