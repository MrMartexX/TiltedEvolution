#include <Structs/Skyrim/PartyQuestReplicaFileExecutor.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kIoBufferSize = 64 * 1024;

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
    std::filesystem::path absolute = std::filesystem::absolute(acPath, ec);
    if (ec || absolute.empty())
        return std::nullopt;
    return absolute.lexically_normal();
}

PartyQuestReplicaExecutionStatus ObserveDetailed(
    const std::filesystem::path& acPath,
    PartyQuestReplicaFileObservation& aObservation) noexcept
{
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(acPath, ec);
    if (ec)
        return PartyQuestReplicaExecutionStatus::IoError;
    if (status.type() == std::filesystem::file_type::not_found)
        return PartyQuestReplicaExecutionStatus::SourceMissing;
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

bool MatchesExpected(
    const PartyQuestReplicaFileObservation& acObservation,
    const PartyQuestReplicaCopyOperation& acOperation) noexcept
{
    return acObservation.Size == acOperation.ExpectedSize &&
        acObservation.Digest == acOperation.ExpectedDigest;
}

bool PathExistsOrIsLink(const std::filesystem::path& acPath, std::error_code& aEc) noexcept
{
    aEc.clear();
    const std::filesystem::file_status status = std::filesystem::symlink_status(acPath, aEc);
    if (aEc)
        return false;
    return status.type() != std::filesystem::file_type::not_found;
}

bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acCandidate) noexcept
{
    return PartyQuestReplicaFilePlanner::IsContainedBy(
        acRoot.lexically_normal(),
        acCandidate.lexically_normal());
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

    const std::filesystem::path checkpointRoot = aRevisionScoped
        ? PartyQuestCoopSaveLayout::GetCheckpointRevisionDirectory(
              acPaths,
              aCheckpointKind,
              aCampaignWorldRevision)
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
        const PartyQuestReplicaCopyOperation& operation = acPlan.Operations[i];
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
        if (!destination || !expectedRoot ||
            !IsInside(*playerRoot, *destination) ||
            !IsInside(*expectedRoot, *destination) ||
            !HasExpectedExtension(operation.Kind, *destination))
        {
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidDestination, i, operation.DestinationPath);
        }

        if (*source == *destination)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidDestination, i, *destination);
        if (!sources.emplace(*source).second || !destinations.emplace(*destination).second)
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidPlan, i, *destination);

        if (!aCheckpoint)
        {
            if (IsInside(*playerRoot, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::ImportSourceInsidePlayerRoot,
                    i,
                    *source);
            }
        }
        else
        {
            if (!IsInside(*playerRoot, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::CheckpointSourceOutsidePlayerRoot,
                    i,
                    *source);
            }
            if (IsInside(*checkpointTree, *source))
            {
                return MakeFailure(
                    PartyQuestReplicaExecutionStatus::CheckpointSourceInsideCheckpointTree,
                    i,
                    *source);
            }
        }

        PartyQuestReplicaFileObservation observation;
        const PartyQuestReplicaExecutionStatus observationStatus = ObserveDetailed(*source, observation);
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
    for (const std::filesystem::path& path : acPaths)
    {
        std::error_code ec;
        const bool removed = std::filesystem::remove(path, ec);
        if (ec)
            success = false;
        else if (!removed)
        {
            std::error_code statusError;
            const auto status = std::filesystem::symlink_status(path, statusError);
            if (statusError || status.type() != std::filesystem::file_type::not_found)
                success = false;
        }
    }
    return success;
}

PartyQuestReplicaExecutionReport ExecuteInternal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision) noexcept
{
    std::vector<NormalizedOperation> operations;
    PartyQuestReplicaExecutionReport validation = ValidatePlanAndSources(
        acPaths,
        acPlan,
        aCheckpoint,
        aCheckpointKind,
        aRevisionScoped,
        aCampaignWorldRevision,
        operations);
    if (!validation.IsSuccess())
        return validation;

    const auto playerRoot = AbsoluteNormalized(acPaths.PlayerDirectory);
    if (!playerRoot)
        return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidLayout, 0);

    std::error_code ec;
    std::filesystem::create_directories(*playerRoot, ec);
    if (ec)
        return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0, *playerRoot);

    const std::filesystem::path canonicalPlayerRoot = std::filesystem::weakly_canonical(*playerRoot, ec);
    if (ec || canonicalPlayerRoot.empty())
        return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, 0, *playerRoot);

    for (size_t i = 0; i < operations.size(); ++i)
    {
        const std::filesystem::path parent = operations[i].Destination.parent_path();
        const std::filesystem::path weakParent = std::filesystem::weakly_canonical(parent, ec);
        if (ec || weakParent.empty())
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, parent);
        if (!IsInside(canonicalPlayerRoot, weakParent))
            return MakeFailure(PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape, i, parent);

        std::filesystem::create_directories(parent, ec);
        if (ec)
            return MakeFailure(PartyQuestReplicaExecutionStatus::IoError, i, parent);

        const std::filesystem::path canonicalParent = std::filesystem::weakly_canonical(parent, ec);
        if (ec || !IsInside(canonicalPlayerRoot, canonicalParent))
            return MakeFailure(PartyQuestReplicaExecutionStatus::DestinationSymlinkEscape, i, parent);
    }

    const uint64_t nonce = NextCopyNonce();
    std::vector<std::filesystem::path> staged;
    std::vector<std::filesystem::path> published;
    staged.reserve(operations.size());
    published.reserve(operations.size());

    for (size_t i = 0; i < operations.size(); ++i)
    {
        const NormalizedOperation& operation = operations[i];
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
        const PartyQuestReplicaExecutionStatus stagedStatus = ObserveDetailed(temporary, stagedObservation);
        if (stagedStatus != PartyQuestReplicaExecutionStatus::Success ||
            !MatchesExpected(stagedObservation, operation.Operation))
        {
            CleanupPaths(staged);
            return MakeFailure(PartyQuestReplicaExecutionStatus::VerificationFailed, i, temporary);
        }
    }

    for (size_t i = 0; i < operations.size(); ++i)
    {
        std::error_code existsError;
        if (PathExistsOrIsLink(operations[i].Destination, existsError) || existsError)
        {
            const bool rolledBack = CleanupPaths(staged) && CleanupPaths(published);
            PartyQuestReplicaExecutionReport report = MakeFailure(
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
            std::vector<std::filesystem::path> remainingStaged(
                staged.begin() + static_cast<std::ptrdiff_t>(i),
                staged.end());
            const bool rolledBack = CleanupPaths(remainingStaged) && CleanupPaths(published);
            PartyQuestReplicaExecutionReport report = MakeFailure(
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
        PartyQuestReplicaFileObservation finalObservation;
        const PartyQuestReplicaExecutionStatus finalStatus =
            ObserveDetailed(operations[i].Destination, finalObservation);
        if (finalStatus != PartyQuestReplicaExecutionStatus::Success ||
            !MatchesExpected(finalObservation, operations[i].Operation))
        {
            const bool rolledBack = CleanupPaths(published);
            PartyQuestReplicaExecutionReport report = MakeFailure(
                rolledBack ? PartyQuestReplicaExecutionStatus::VerificationFailed
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

PartyQuestReplicaExecutionReport VerifyInternal(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan,
    bool aCheckpoint,
    PartyQuestCheckpointKind aCheckpointKind,
    bool aRevisionScoped,
    uint64_t aCampaignWorldRevision) noexcept
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
            return MakeFailure(PartyQuestReplicaExecutionStatus::InvalidDestination, i, operation.DestinationPath);
        }

        PartyQuestReplicaFileObservation observation;
        const PartyQuestReplicaExecutionStatus status = ObserveDetailed(*destination, observation);
        if (status != PartyQuestReplicaExecutionStatus::Success || !MatchesExpected(observation, operation))
            return MakeFailure(PartyQuestReplicaExecutionStatus::VerificationFailed, i, *destination);
    }

    PartyQuestReplicaExecutionReport report;
    report.Status = PartyQuestReplicaExecutionStatus::Success;
    report.CompletedOperations = acPlan.Operations.size();
    return report;
}
} // namespace

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
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return ExecuteInternal(acPaths, acPlan, false, PartyQuestCheckpointKind::PreJoin, false, 0);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::ExecuteCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return ExecuteInternal(acPaths, acPlan, true, aKind, false, 0);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::ExecuteRevisionCheckpoint(
    const PartyQuestCoopSavePaths& acPaths,
    PartyQuestCheckpointKind aKind,
    uint64_t aCampaignWorldRevision,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return ExecuteInternal(
        acPaths,
        acPlan,
        true,
        aKind,
        true,
        aCampaignWorldRevision);
}

PartyQuestReplicaExecutionReport PartyQuestReplicaFileExecutor::VerifyImport(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaCopyPlan& acPlan) noexcept
{
    return VerifyInternal(acPaths, acPlan, false, PartyQuestCheckpointKind::PreJoin, false, 0);
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
