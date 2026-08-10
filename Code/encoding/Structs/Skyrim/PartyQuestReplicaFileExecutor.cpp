#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace
{
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kIoBufferSize = 64 * 1024;

struct ExecutionDeadline
{
    uint64_t StartedAt{};
    uint64_t ExpiresAt{};
};

bool BuildDeadline(
    const PartyQuestReplicaExecutionHooks& acHooks,
    ExecutionDeadline& aDeadline) noexcept
{
    const uint64_t now = acHooks.NowTicks();
    if (now == 0 ||
        now > std::numeric_limits<uint64_t>::max() -
            PartyQuestReplicaResourcePolicy::MaxExecutionNanoseconds)
    {
        return false;
    }
    aDeadline.StartedAt = now;
    aDeadline.ExpiresAt =
        now + PartyQuestReplicaResourcePolicy::MaxExecutionNanoseconds;
    return true;
}

bool DeadlineExceeded(
    const PartyQuestReplicaExecutionHooks& acHooks,
    const ExecutionDeadline& acDeadline) noexcept
{
    const uint64_t now = acHooks.NowTicks();
    return now == 0 ||
        now < acDeadline.StartedAt ||
        now >= acDeadline.ExpiresAt;
}

struct NormalizedOperation
{
    PartyQuestReplicaCopyOperation Operation;
    std::filesystem::path Source;
    std::filesystem::path Destination;
};

uint64_t NextCopyNonce() noexcept
{
    static std::atomic<uint64_t> sequence{1};
    const uint64_t counter = sequence.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t value = now ^ (counter * 0x9E3779B97F4A7C15ull);
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value != 0 ? value : counter;
}

bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

std::string LowerExtension(const std::filesystem::path& acPath)
{
    std::string extension = acPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char aCharacter)
    {
        return static_cast<char>(std::tolower(aCharacter));
    });
    return extension;
}

bool HasExpectedExtension(
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acPath)
{
    switch (aKind)
    {
    case PartyQuestReplicaFileKind::SkyrimSave:
        return LowerExtension(acPath) == ".ess";
    case PartyQuestReplicaFileKind::SkseCosave:
        return LowerExtension(acPath) == ".skse";
    case PartyQuestReplicaFileKind::ExternalSidecar:
        return true;
    }
    return false;
}

std::optional<std::filesystem::path> AbsoluteNormalized(
    const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty())
        return std::nullopt;

    std::error_code ec;
    const auto absolute = std::filesystem::absolute(acPath, ec);
    if (ec || absolute.empty())
        return std::nullopt;
    return absolute.lexically_normal();
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

PartyQuestReplicaExecutionStatus ObserveDetailed(
    const std::filesystem::path& acPath,
    PartyQuestReplicaFileObservation& aObservation) noexcept
{
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found || IsMissingError(ec))
            return PartyQuestReplicaExecutionStatus::SourceMissing;
        if (ec)
            return PartyQuestReplicaExecutionStatus::IoError;
        if (std::filesystem::is_symlink(status))
            return PartyQuestReplicaExecutionStatus::SourceSymlink;
        if (!std::filesystem::is_regular_file(status))
            return PartyQuestReplicaExecutionStatus::SourceNotRegularFile;

        std::ifstream file(acPath, std::ios::binary);
        if (!file.is_open())
            return PartyQuestReplicaExecutionStatus::IoError;

        uint64_t digest = kFnvOffsetBasis;
        uint64_t size{};
        std::array<char, kIoBufferSize> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            if (count <= 0)
                break;

            size += static_cast<uint64_t>(count);
            for (std::streamsize i = 0; i < count; ++i)
            {
                digest ^= static_cast<uint8_t>(buffer[static_cast<size_t>(i)]);
                digest *= kFnvPrime;
            }
        }

        if (file.bad())
            return PartyQuestReplicaExecutionStatus::IoError;
        if (digest == 0)
            digest = 1;

        aObservation.Size = size;
        aObservation.Digest = digest;
        return PartyQuestReplicaExecutionStatus::Success;
    }
    catch (...)
    {
        return PartyQuestReplicaExecutionStatus::IoError;
    }
}

bool MatchesExpected(
    const PartyQuestReplicaFileObservation& acObservation,
    const PartyQuestReplicaCopyOperation& acOperation) noexcept
{
    return acObservation.Size == acOperation.ExpectedSize &&
        acObservation.Digest == acOperation.ExpectedDigest;
}

bool PathExistsOrIsLink(const std::filesystem::path& acPath, std::error_code& aError) noexcept
{
    aError.clear();
    const auto status = std::filesystem::symlink_status(acPath, aError);
    if (status.type() == std::filesystem::file_type::not_found || IsMissingError(aError))
    {
        aError.clear();
        return false;
    }
    if (aError)
        return false;
    return true;
}

std::optional<std::filesystem::path> ExpectedDestinationRoot(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestReplicaFileKind aKind,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision) noexcept
{
    if (!aCheckpoint)
    {
        if (aKind == PartyQuestReplicaFileKind::ExternalSidecar)
            return acPaths.SidecarsDirectory / "external";
        return acPaths.SavesDirectory;
    }

    if (aRevisionScoped && aCampaignWorldRevision == 0)
        return std::nullopt;

    const auto checkpointRoot = aRevisionScoped
        ? PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
              acPaths, aCheckpointKind, aCampaignWorldRevision)
        : PartyQuestCoopSaveLayout::GetCheckpointDirectory(acPaths, aCheckpointKind);
    if (checkpointRoot.empty())
        return std::nullopt;

    if (aKind == PartyQuestReplicaFileKind::ExternalSidecar)
        return checkpointRoot / "sidecars" / "external";
    return checkpointRoot / "saves";
}

PartyQuestReplicaExecutionReport MakeFailure(
    PartyQuestReplicaExecutionStatus aStatus,
    size_t aIndex,
    const std::filesystem::path& acPath = {}) noexcept
{
    PartyQuestReplicaExecutionReport report;
    report.Status = aStatus;
    report.FailedOperation = aIndex;
    report.FailedPath = acPath;
    return report;
}

PartyQuestReplicaExecutionReport ValidatePlanAndSources(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision,
    std::vector<NormalizedOperation>& aOperations) noexcept
{
    if (!acPlan.IsReady() || acPlan.Operations.empty())
        return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidPlan, 0);

    const auto playerRoot = AbsoluteNormalized(acPaths.PlayerDirectory);
    const auto checkpointTree = AbsoluteNormalized(acPaths.CheckpointsDirectory);
    if (!playerRoot || !checkpointTree)
        return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidLayout, 0);

    std::set<std::filesystem::path> sources;
    std::set<std::filesystem::path> destinations;
    aOperations.clear();
    aOperations.reserve(acPlan.Operations.size());

    for (size_t i = 0; i < acPlan.Operations.size(); ++i)
    {
        const auto& operation = acPlan.Operations[i];
        if (operation.SourcePath.empty() || operation.DestinationPath.empty() || operation.ExpectedDigest == 0)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidPlan, i);

        const auto source = AbsoluteNormalized(operation.SourcePath);
        const auto destination = AbsoluteNormalized(operation.DestinationPath);
        const auto expectedRootRaw = ExpectedDestinationRoot(
            acPaths,
            operation.Kind,
            aCheckpoint,
            aCheckpointKind,
            aRevisionScoped,
            aCampaignWorldRevision);
        const auto expectedRoot = expectedRootRaw ? AbsoluteNormalized(*expectedRootRaw) : std::nullopt;

        if (!source)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidSourcePath, i, operation.SourcePath);
        if (!PartyQuestReplicaResourcePolicy::IsPathWithinBudget(*source))
        {
            return MakeFailure(
                PartyQuestReplicaExecutionStatus::ResourceLimitExceeded,
                i,
                *source);
        }
        if (!destination || !expectedRoot ||
            !IsInside(*playerRoot, *destination) ||
            !IsInside(*expectedRoot, *destination) ||
            !HasExpectedExtension(operation.Kind, *destination))
        {
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidDestination, i, operation.DestinationPath);
        }
        if (!PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(*destination))
        {
            return MakeFailure(
                PartyQuestReplicaExecutionStatus::ResourceLimitExceeded,
                i,
                *destination);
        }
        if (*source == *destination ||
            !sources.emplace(*source).second ||
            !destinations.emplace(*destination).second)
        {
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidPlan, i, *destination);
        }

        if (!aCheckpoint)
        {
            if (IsInside(*playerRoot, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::ImportSourceInsidePlayerRoot, i, *source);
            }
        }
        else
        {
            if (!IsInside(*playerRoot, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::CheckpointSourceOutsidePlayerRoot, i, *source);
            }
            if (IsInside(*checkpointTree, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::CheckpointSourceInsideCheckpointTree, i, *source);
            }
        }

        PartyQuestReplicaFileObservation observation;
        const auto observationStatus = ObserveDetailed(*source, observation);
        if (observationStatus != PartyQuestReplicaExecutionStatus::Success)
            return MakeFailure(observationStatus, i, *source);
        if (!MatchesExpected(observation, operation))
            return MakeFailure(PartyQuestReplicaExecutionStatus::SourceChanged, i, *source);

        std::error_code ec;
        const bool destinationExists = PathExistsOrIsLink(*destination, ec);
        if (ec)
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, *destination);
        if (destinationExists)
            return MakeFailure(PartyQuestReplicaExecutionStatus::DestinationExists, i, *destination);

        aOperations.push_back({operation, *source, *destination});
    }

    PartyQuestReplicaExecutionReport report;
    report.Status = PartyQuestReplicaExecutionStatus::Success;
    return report;
}

bool CleanupPaths(const std::vector<std::filesystem::path>& acPaths) noexcept
{
    bool success = true;
    for (const auto& path : acPaths)
    {
        std::error_code ec;
        const bool removed = std::filesystem::remove(path, ec);
        if (IsMissingError(ec))
        {
            ec.clear();
            continue;
        }
        if (ec)
        {
            success = false;
            continue;
        }
        if (removed)
            continue;

        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(path, statusError);
        if (status.type() == std::filesystem::file_type::not_found || IsMissingError(statusError))
            continue;
        if (statusError || status.type() != std::filesystem::file_type::not_found)
            success = false;
    }
    return success;
}

bool PrepareDestinationParents(
    const std::filesystem::path& acCanonicalPlayerRoot,
    const std::vector<NormalizedOperation>& acOperations,
    PartyQuestReplicaExecutionReport& aFailure) noexcept
{
    for (size_t i = 0; i < acOperations.size(); ++i)
    {
        const auto parent = acOperations[i].Destination.parent_path();
        std::error_code ec;
        const auto weakParent = std::filesystem::weakly_canonical(parent, ec);
        if (ec || weakParent.empty())
        {
            aFailure = MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, parent);
            return false;
        }
        if (!IsInside(acCanonicalPlayerRoot, weakParent))
        {
            aFailure = MakeFailure(
                PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape, i, parent);
            return false;
        }

        ec.clear();
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            aFailure = MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, parent);
            return false;
        }

        ec.clear();
        const auto canonicalParent = std::filesystem::weakly_canonical(parent, ec);
        if (ec || canonicalParent.empty() || !IsInside(acCanonicalPlayerRoot, canonicalParent))
        {
            aFailure = MakeFailure(
                PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape, i, parent);
            return false;
        }
    }
    return true;
}

bool DestinationParentStillSafe(
    const std::filesystem::path& acCanonicalPlayerRoot,
    const std::filesystem::path& acDestination) noexcept
{
    std::error_code ec;
    const auto canonicalParent = std::filesystem::weakly_canonical(
        acDestination.parent_path(), ec);
    return !ec && !canonicalParent.empty() && IsInside(acCanonicalPlayerRoot, canonicalParent);
}

PartyQuestReplicaExecutionReport ExecuteInternal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision,
    PartyQuestReplicaExecutionHooks aHooks) noexcept
{
    try
    {
        ExecutionDeadline deadline;
        if (!BuildDeadline(aHooks, deadline))
            return MakeFailure(PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded, 0);

        const auto requiredFreeBytes = PartyQuestReplicaResourcePolicy::RequiredFreeBytes(acPlan);
        if (!requiredFreeBytes)
            return MakeFailure(PartyQuestReplicaExecutionStatus::ResourceLimitExceeded, 0);

        std::vector<NormalizedOperation> operations;
        auto validation = ValidatePlanAndSources(
            acPaths,
            acPlan,
            aCheckpoint,
            aCheckpointKind,
            aRevisionScoped,
            aCampaignWorldRevision,
            operations);
        if (!validation.IsSuccess())
            return validation;
        if (DeadlineExceeded(aHooks, deadline))
            return MakeFailure(PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded, 0);

        const auto playerRoot = AbsoluteNormalized(acPaths.PlayerDirectory);
        if (!playerRoot)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidLayout, 0);

        std::error_code ec;
        std::filesystem::create_directories(*playerRoot, ec);
        if (ec)
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0, *playerRoot);

        ec.clear();
        const auto canonicalPlayerRoot = std::filesystem::weakly_canonical(*playerRoot, ec);
        if (ec || canonicalPlayerRoot.empty())
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0, *playerRoot);

        ec.clear();
        const auto diskSpace = std::filesystem::space(canonicalPlayerRoot, ec);
        if (ec)
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0, canonicalPlayerRoot);
        if (diskSpace.available < *requiredFreeBytes)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InsufficientDiskSpace, 0, canonicalPlayerRoot);

        PartyQuestReplicaExecutionReport parentFailure;
        if (!PrepareDestinationParents(canonicalPlayerRoot, operations, parentFailure))
            return parentFailure;
        if (DeadlineExceeded(aHooks, deadline))
            return MakeFailure(PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded, 0);

        const uint64_t nonce = NextCopyNonce();
        std::vector<std::filesystem::path> staged;
        std::vector<std::filesystem::path> published;
        staged.reserve(operations.size());
        published.reserve(operations.size());

        for (size_t i = 0; i < operations.size(); ++i)
        {
            const auto& operation = operations[i];
            if (DeadlineExceeded(aHooks, deadline))
            {
                const bool rolledBack = CleanupPaths(staged);
                return MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operation.Source);
            }
            std::filesystem::path temporary = operation.Destination;
            temporary += ".tpqtmp-" + std::to_string(nonce) + "-" + std::to_string(i);

            std::error_code existsError;
            if (PathExistsOrIsLink(temporary, existsError))
            {
                CleanupPaths(staged);
                return MakeFailure(PartyQuestReplicaExecutionStatus::DestinationExists, i, temporary);
            }
            if (existsError)
            {
                CleanupPaths(staged);
                return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, temporary);
            }
            if (!DestinationParentStillSafe(canonicalPlayerRoot, operation.Destination))
            {
                CleanupPaths(staged);
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape,
                    i,
                    operation.Destination.parent_path());
            }

            ec.clear();
            if (!std::filesystem::copy_file(
                    operation.Source,
                    temporary,
                    std::filesystem::copy_options::none,
                    ec) || ec)
            {
                CleanupPaths(staged);
                return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, operation.Source);
            }
            staged.push_back(temporary);

            PartyQuestReplicaFileObservation stagedObservation;
            const auto stagedStatus = ObserveDetailed(temporary, stagedObservation);
            if (stagedStatus != PartyQuestReplicaExecutionStatus::Success ||
                !MatchesExpected(stagedObservation, operation.Operation))
            {
                CleanupPaths(staged);
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::VerificationFailed, i, temporary);
            }

            PartyQuestReplicaFileObservation sourceObservation;
            if (ObserveDetailed(operation.Source, sourceObservation) !=
                    PartyQuestReplicaExecutionStatus::Success ||
                !MatchesExpected(sourceObservation, operation.Operation))
            {
                CleanupPaths(staged);
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::SourceChanged, i, operation.Source);
            }
            if (DeadlineExceeded(aHooks, deadline))
            {
                const bool rolledBack = CleanupPaths(staged);
                return MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operation.Source);
            }
        }

        for (size_t i = 0; i < operations.size(); ++i)
        {
            if (DeadlineExceeded(aHooks, deadline))
            {
                const bool rolledBack = CleanupPaths(staged) && CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }
            if (!DestinationParentStillSafe(canonicalPlayerRoot, operations[i].Destination))
            {
                const bool rolledBack = CleanupPaths(staged) && CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination.parent_path());
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }

            std::error_code existsError;
            if (PathExistsOrIsLink(operations[i].Destination, existsError) || existsError)
            {
                const bool rolledBack = CleanupPaths(staged) && CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::DestinationExists
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }

            ec.clear();
            std::filesystem::rename(staged[i], operations[i].Destination, ec);
            if (ec)
            {
                const bool rolledBack = CleanupPaths(staged) && CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::IoError
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }
            published.push_back(operations[i].Destination);
        }

        for (size_t i = 0; i < operations.size(); ++i)
        {
            if (DeadlineExceeded(aHooks, deadline))
            {
                const bool rolledBack = CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }
            PartyQuestReplicaFileObservation finalObservation;
            const auto finalStatus = ObserveDetailed(operations[i].Destination, finalObservation);
            if (finalStatus != PartyQuestReplicaExecutionStatus::Success ||
                !MatchesExpected(finalObservation, operations[i].Operation))
            {
                const bool rolledBack = CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::VerificationFailed
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }
            if (DeadlineExceeded(aHooks, deadline))
            {
                const bool rolledBack = CleanupPaths(published);
                auto report = MakeFailure(
                    rolledBack ? PartyQuestReplicaExecutionStatus::OperationDeadlineExceeded
                               : PartyQuestReplicaExecutionStatus::RollbackFailed,
                    i,
                    operations[i].Destination);
                report.CompletedOperations = rolledBack ? 0 : published.size();
                return report;
            }
        }

        PartyQuestReplicaExecutionReport report;
        report.Status = PartyQuestReplicaExecutionStatus::Success;
        report.CompletedOperations = operations.size();
        return report;
    }
    catch (...)
    {
        return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0);
    }
}

PartyQuestReplicaExecutionReport VerifyInternal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision) noexcept
{
    try
    {
        if (!acPlan.IsReady() || acPlan.Operations.empty())
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidPlan, 0);

        const auto playerRoot = AbsoluteNormalized(acPaths.PlayerDirectory);
        if (!playerRoot)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidLayout, 0);

        std::set<std::filesystem::path> destinations;
        for (size_t i = 0; i < acPlan.Operations.size(); ++i)
        {
            const auto& operation = acPlan.Operations[i];
            const auto destination = AbsoluteNormalized(operation.DestinationPath);
            const auto expectedRootRaw = ExpectedDestinationRoot(
                acPaths,
                operation.Kind,
                aCheckpoint,
                aCheckpointKind,
                aRevisionScoped,
                aCampaignWorldRevision);
            const auto expectedRoot = expectedRootRaw ? AbsoluteNormalized(*expectedRootRaw) : std::nullopt;
            if (!destination || !expectedRoot ||
                !IsInside(*playerRoot, *destination) ||
                !IsInside(*expectedRoot, *destination) ||
                !HasExpectedExtension(operation.Kind, *destination) ||
                !destinations.emplace(*destination).second)
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::InvalidDestination, i, operation.DestinationPath);
            }

            PartyQuestReplicaFileObservation observation;
            const auto status = ObserveDetailed(*destination, observation);
            if (status != PartyQuestReplicaExecutionStatus::Success ||
                !MatchesExpected(observation, operation))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::VerificationFailed, i, *destination);
            }
        }

        PartyQuestReplicaExecutionReport report;
        report.Status = PartyQuestReplicaExecutionStatus::Success;
        report.CompletedOperations = acPlan.Operations.size();
        return report;
    }
    catch (...)
    {
        return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0);
    }
}
} // namespace

uint64_t PartyQuestReplicaExecutionHooks::NowTicks() const noexcept
{
    if (MonotonicNow)
        return MonotonicNow(Context);
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now > 0 ? static_cast<uint64_t>(now) : 0;
}

std::optional<PartyQuestReplicaFileObservation>
PartyQuestReplicaFileExecutor::ObserveRegularFile(
    const std::filesystem::path& acPath) noexcept
{
    PartyQuestReplicaFileObservation observation;
    if (ObserveDetailed(acPath, observation) != PartyQuestReplicaExecutionStatus::Success)
        return std::nullopt;
    return observation;
}

std::optional<PartyQuestReplicaFileSpec> PartyQuestReplicaFileExecutor::InspectSource(
    PartyQuestReplicaFileKind aKind,
    const std::filesystem::path& acSourcePath,
    const std::filesystem::path& acRelativePath) noexcept
{
    if (acSourcePath.empty() ||
        !PartyQuestReplicaResourcePolicy::IsPathWithinBudget(acSourcePath) ||
        !PartyQuestReplicaResourcePolicy::IsPathWithinBudget(acRelativePath) ||
        !PartyQuestReplicaFilePlanner::IsSafeRelativePath(acRelativePath) ||
        !HasExpectedExtension(aKind, acRelativePath))
    {
        return std::nullopt;
    }

    const auto observation = ObserveRegularFile(acSourcePath);
    if (!observation)
        return std::nullopt;

    PartyQuestReplicaFileSpec spec;
    spec.Kind = aKind;
    spec.SourcePath = acSourcePath;
    spec.RelativePath = acRelativePath;
    spec.Size = observation->Size;
    spec.Digest = observation->Digest;
    return spec;
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::ExecuteImport(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    PartyQuestReplicaExecutionHooks aHooks) noexcept
{
    return ExecuteInternal(
        acPaths, acPlan, false, PartyQuestCheckpointKind::PreJoin, false, 0, aHooks);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    const PartyQuestReplicaCopyPlan& acPlan,
    PartyQuestReplicaExecutionHooks aHooks) noexcept
{
    return ExecuteInternal(acPaths, acPlan, true, aKind, false, 0, aHooks);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan,
    PartyQuestReplicaExecutionHooks aHooks) noexcept
{
    return ExecuteInternal(
        acPaths,
        acPlan,
        true,
        aKind,
        true,
        aCampaignWorldRevision,
        aHooks);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::VerifyImport(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return VerifyInternal(
        acPaths, acPlan, false, PartyQuestCheckpointKind::PreJoin, false, 0);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::VerifyCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return VerifyInternal(acPaths, acPlan, true, aKind, false, 0);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::VerifyRevisionCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return VerifyInternal(
        acPaths,
        acPlan,
        true,
        aKind,
        true,
        aCampaignWorldRevision);
}
