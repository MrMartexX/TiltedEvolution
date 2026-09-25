#include <Structs/Skyrim/PartyQuestReplicaRestoreJournal.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <type_traits>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'R', 'S', 'T', 'R', 'J'};
constexpr uint16_t kLegacyFormatVersion = 1;
constexpr uint16_t kRolledBackFormatVersion = 2;
constexpr uint16_t kProcessCrashFormatVersion = 3;
constexpr uint16_t kPowerLossFormatVersion = 4;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint32_t kMaxOperations =
    static_cast<uint32_t>(PartyQuestReplicaResourcePolicy::MaxFiles);
constexpr uint32_t kMaxPathBytes =
    PartyQuestDurableResourcePolicy::MaxSerializedPathBytes;

bool IsMissingError(const std::error_code& acError) noexcept
{
    return acError == std::errc::no_such_file_or_directory ||
        acError == std::errc::not_a_directory;
}

bool IsKnownCheckpointKind(PartyQuestCheckpointKind aKind) noexcept
{
    switch (aKind)
    {
    case PartyQuestCheckpointKind::PreJoin:
    case PartyQuestCheckpointKind::PreMigration:
    case PartyQuestCheckpointKind::PreRepair:
    case PartyQuestCheckpointKind::SessionStart:
    case PartyQuestCheckpointKind::LastKnownGood:
        return true;
    }
    return false;
}

bool IsKnownPhase(PartyQuestReplicaRestoreJournalPhase aPhase) noexcept
{
    return static_cast<uint8_t>(aPhase) <=
        static_cast<uint8_t>(PartyQuestReplicaRestoreJournalPhase::RolledBack);
}

bool IsSupportedFormatVersion(uint16_t aVersion) noexcept
{
    return aVersion == kLegacyFormatVersion ||
        aVersion == kRolledBackFormatVersion ||
        aVersion == kProcessCrashFormatVersion ||
        aVersion == kPowerLossFormatVersion;
}

PartyQuestReplicaRestoreJournalArchiveDurability GetArchiveDurability(
    uint16_t aVersion) noexcept
{
    if (aVersion == kProcessCrashFormatVersion)
        return PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient;
    if (aVersion == kPowerLossFormatVersion)
        return PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable;
    return PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding;
}

std::optional<uint16_t> GetFormatVersion(
    PartyQuestReplicaRestoreJournalArchiveDurability aDurability) noexcept
{
    switch (aDurability)
    {
    case PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient:
        return kProcessCrashFormatVersion;
    case PartyQuestReplicaRestoreJournalArchiveDurability::PowerLossDurable:
        return kPowerLossFormatVersion;
    case PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding:
        return std::nullopt;
    }
    return std::nullopt;
}

bool IsInside(
    const std::filesystem::path& acRoot,
    const std::filesystem::path& acPath) noexcept
{
    try
    {
        return PartyQuestReplicaFilePlanner::IsContainedBy(
            acRoot.lexically_normal(), acPath.lexically_normal());
    }
    catch (...)
    {
        return false;
    }
}

bool HasBoundedPath(const std::filesystem::path& acPath) noexcept
{
    try
    {
        const auto value = acPath.generic_string();
        return !value.empty() && value.size() <= kMaxPathBytes;
    }
    catch (...)
    {
        return false;
    }
}

std::string FormatRestoreId(uint64_t aRestoreId)
{
    std::ostringstream stream;
    stream << "Transaction_" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << aRestoreId;
    return stream.str();
}

bool IsRegularNonSymlink(
    const std::filesystem::path& acPath,
    bool& aExists) noexcept
{
    aExists = false;
    try
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found || IsMissingError(ec))
        {
            aExists = false;
            return true;
        }
        if (ec)
            return false;

        aExists = true;
        return !std::filesystem::is_symlink(status) &&
            std::filesystem::is_regular_file(status);
    }
    catch (...)
    {
        return false;
    }
}

bool ValidateState(const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!acState.CampaignId.IsValid() ||
        !acState.PlayerProfileId.IsValid() ||
        acState.RestoreId == 0 ||
        acState.CampaignWorldRevision == 0 ||
        !IsKnownCheckpointKind(acState.CheckpointKind) ||
        !IsKnownPhase(acState.Phase) ||
        !HasBoundedPath(acState.TransactionDirectory) ||
        acState.Operations.empty() ||
        acState.Operations.size() > kMaxOperations)
    {
        return false;
    }

    std::set<std::filesystem::path> destinations;
    std::set<std::filesystem::path> rollbackPaths;
    for (const auto& operation : acState.Operations)
    {
        if (!HasBoundedPath(operation.CheckpointSourcePath) ||
            !HasBoundedPath(operation.ReplicaDestinationPath) ||
            !HasBoundedPath(operation.RollbackPath) ||
            operation.ExpectedRestoredDigest == 0 ||
            (operation.DestinationExisted && operation.OriginalDigest == 0) ||
            operation.CheckpointSourcePath == operation.ReplicaDestinationPath ||
            !IsInside(acState.TransactionDirectory / "rollback", operation.RollbackPath) ||
            !destinations.emplace(operation.ReplicaDestinationPath.lexically_normal()).second ||
            !rollbackPaths.emplace(operation.RollbackPath.lexically_normal()).second)
        {
            return false;
        }
    }

    return true;
}

template <class T>
void WriteInteger(std::vector<uint8_t>& aBytes, T aValue)
{
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    const auto value = static_cast<Unsigned>(aValue);
    for (size_t i = 0; i < sizeof(Unsigned); ++i)
        aBytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}

template <class T>
bool ReadInteger(
    const std::vector<uint8_t>& acBytes,
    size_t& aOffset,
    size_t aEnd,
    T& aValue) noexcept
{
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    if (aEnd > acBytes.size() || aOffset > aEnd ||
        aEnd - aOffset < sizeof(Unsigned))
    {
        return false;
    }

    Unsigned value{};
    for (size_t i = 0; i < sizeof(Unsigned); ++i)
        value |= static_cast<Unsigned>(acBytes[aOffset + i]) << (i * 8);
    aOffset += sizeof(Unsigned);
    aValue = static_cast<T>(value);
    return true;
}

void WritePath(
    std::vector<uint8_t>& aBytes,
    const std::filesystem::path& acPath)
{
    const std::string value = acPath.generic_string();
    WriteInteger<uint32_t>(aBytes, static_cast<uint32_t>(value.size()));
    aBytes.insert(aBytes.end(), value.begin(), value.end());
}

bool ReadPath(
    const std::vector<uint8_t>& acBytes,
    size_t& aOffset,
    size_t aEnd,
    std::filesystem::path& aPath) noexcept
{
    uint32_t size{};
    if (!ReadInteger(acBytes, aOffset, aEnd, size) ||
        size == 0 || size > kMaxPathBytes ||
        aOffset > aEnd || aEnd - aOffset < size)
    {
        return false;
    }

    try
    {
        const std::string value(
            reinterpret_cast<const char*>(acBytes.data() + aOffset), size);
        aOffset += size;
        aPath = std::filesystem::path(value).lexically_normal();
        return !aPath.empty();
    }
    catch (...)
    {
        return false;
    }
}

uint64_t ComputeChecksum(const uint8_t* apData, size_t aSize) noexcept
{
    uint64_t checksum = kFnvOffsetBasis;
    for (size_t i = 0; i < aSize; ++i)
    {
        checksum ^= apData[i];
        checksum *= kFnvPrime;
    }
    return checksum;
}

PartyQuestReplicaRestoreJournalPersistenceStatus ReadFile(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(acPath, ec);
        if (status.type() == std::filesystem::file_type::not_found || IsMissingError(ec))
            return PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound;
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }

    const std::streampos end = file.tellg();
    if (end < 0)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

    const auto size = static_cast<uint64_t>(end);
    if (size > PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes ||
        size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
    }

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty() &&
        !file.read(
            reinterpret_cast<char*>(aBytes.data()),
            static_cast<std::streamsize>(aBytes.size())))
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }
    return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
}

bool WriteFile(
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
    return file.good();
}

PartyQuestReplicaRestoreJournalPersistenceResult DecodeFile(
    const std::filesystem::path& acPath)
{
    std::vector<uint8_t> bytes;
    PartyQuestReplicaRestoreJournalPersistenceResult result;
    result.Status = ReadFile(acPath, bytes);
    if (result.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return result;
    return PartyQuestReplicaRestoreJournalPersistence::Decode(bytes);
}

PartyQuestReplicaRestoreJournalPersistenceResult RequireArchiveDurability(
    PartyQuestReplicaRestoreJournalPersistenceResult aResult,
    PartyQuestReplicaRestoreJournalArchiveDurability aExpected) noexcept
{
    if (aResult.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return aResult;
    if (!aResult.State)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return aResult;
    }
    if (aResult.ArchiveDurability ==
        PartyQuestReplicaRestoreJournalArchiveDurability::AmbiguousLegacyEncoding)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityAmbiguous;
        return aResult;
    }
    if (aResult.ArchiveDurability != aExpected)
    {
        aResult.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::DurabilityMismatch;
        return aResult;
    }
    return aResult;
}

PartyQuestReplicaRestoreJournalPersistenceStatus ValidateExistingEvidenceDomain(
    const std::filesystem::path& acPath,
    PartyQuestReplicaRestoreJournalArchiveDurability aExpected) noexcept
{
    const auto decoded = DecodeFile(acPath);
    if (decoded.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::FileNotFound)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    if (decoded.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    return RequireArchiveDurability(decoded, aExpected).Status;
}
} // namespace

PartyQuestReplicaRestoreJournalResult PartyQuestReplicaRestoreJournal::Prepare(
    const PartyQuestCoopSavePaths& acPaths,
    const PartyQuestReplicaRestorePlan& acPlan,
    uint64_t aRestoreId) noexcept
{
    PartyQuestReplicaRestoreJournalResult result;
    if (!acPlan.IsReady())
    {
        result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPlan;
        return result;
    }
    if (!acPlan.CampaignId.IsValid() || !acPlan.PlayerProfileId.IsValid())
    {
        result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidIdentity;
        return result;
    }
    if (aRestoreId == 0)
    {
        result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidRestoreId;
        return result;
    }
    if (acPlan.CampaignWorldRevision == 0)
    {
        result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidWorldRevision;
        return result;
    }

    try
    {
        PartyQuestReplicaRestoreJournalState state;
        state.CampaignId = acPlan.CampaignId;
        state.PlayerProfileId = acPlan.PlayerProfileId;
        state.RestoreId = aRestoreId;
        state.CheckpointKind = acPlan.CheckpointKind;
        state.CampaignWorldRevision = acPlan.CampaignWorldRevision;
        state.Phase = PartyQuestReplicaRestoreJournalPhase::Prepared;
        state.TransactionDirectory =
            (acPaths.MetadataDirectory / "restore" / FormatRestoreId(aRestoreId)).lexically_normal();

        const auto playerRoot = acPaths.PlayerDirectory.lexically_normal();
        const auto rollbackRoot = (state.TransactionDirectory / "rollback").lexically_normal();
        if (!PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(
                state.TransactionDirectory) ||
            !IsInside(playerRoot, state.TransactionDirectory) ||
            !IsInside(acPaths.MetadataDirectory, state.TransactionDirectory))
        {
            result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
            return result;
        }

        state.Operations.reserve(acPlan.Operations.size());
        for (const auto& operation : acPlan.Operations)
        {
            const auto destination = operation.ReplicaDestinationPath.lexically_normal();
            if (!PartyQuestReplicaResourcePolicy::IsPathWithinBudget(
                    operation.CheckpointSourcePath) ||
                !PartyQuestReplicaResourcePolicy::IsMutablePathWithinBudget(destination) ||
                !IsInside(playerRoot, destination) ||
                IsInside(acPaths.CheckpointsDirectory, destination) ||
                IsInside(acPaths.MetadataDirectory, destination) ||
                destination == acPaths.RuntimeApplySidecar.lexically_normal())
            {
                result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
                return result;
            }

            const auto relative = destination.lexically_relative(playerRoot).lexically_normal();
            if (!PartyQuestReplicaFilePlanner::IsSafeRelativePath(relative))
            {
                result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
                return result;
            }

            PartyQuestReplicaRestoreJournalOperation journalOperation;
            journalOperation.Kind = operation.Kind;
            journalOperation.CheckpointSourcePath = operation.CheckpointSourcePath.lexically_normal();
            journalOperation.ReplicaDestinationPath = destination;
            journalOperation.RollbackPath = (rollbackRoot / relative).lexically_normal();
            journalOperation.ExpectedRestoredSize = operation.ExpectedSize;
            journalOperation.ExpectedRestoredDigest = operation.ExpectedDigest;

            auto rollbackTemporary = journalOperation.RollbackPath;
            rollbackTemporary += ".tmp";
            if (!PartyQuestReplicaResourcePolicy::IsPathWithinBudget(
                    journalOperation.RollbackPath) ||
                !PartyQuestReplicaResourcePolicy::IsPathWithinBudget(rollbackTemporary))
            {
                result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
                return result;
            }

            bool destinationExists{};
            if (!IsRegularNonSymlink(destination, destinationExists))
            {
                result.Status = PartyQuestReplicaRestoreJournalStatus::DestinationUnsafe;
                return result;
            }
            journalOperation.DestinationExisted = destinationExists;

            if (destinationExists)
            {
                const auto observation =
                    PartyQuestReplicaFileExecutor::ObserveRegularFile(destination);
                if (!observation)
                {
                    result.Status = PartyQuestReplicaRestoreJournalStatus::DestinationUnsafe;
                    return result;
                }
                journalOperation.OriginalSize = observation->Size;
                journalOperation.OriginalDigest = observation->Digest;
            }

            state.Operations.push_back(std::move(journalOperation));
        }

        if (!ValidateState(state))
        {
            result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
            return result;
        }

        result.Status = PartyQuestReplicaRestoreJournalStatus::Ready;
        result.State = std::move(state);
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestReplicaRestoreJournalStatus::InvalidPath;
        return result;
    }
}

std::filesystem::path PartyQuestReplicaRestoreJournal::GetJournalPath(
    const PartyQuestReplicaRestoreJournalState& acState)
{
    return acState.TransactionDirectory / "journal.bin";
}

bool PartyQuestReplicaRestoreJournal::VerifyRollbackBackups(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!ValidateState(acState))
        return false;

    for (const auto& operation : acState.Operations)
    {
        bool backupExists{};
        if (!IsRegularNonSymlink(operation.RollbackPath, backupExists))
            return false;

        if (!operation.DestinationExisted)
        {
            if (backupExists)
                return false;
            continue;
        }

        if (!backupExists)
            return false;
        const auto observation =
            PartyQuestReplicaFileExecutor::ObserveRegularFile(operation.RollbackPath);
        if (!observation ||
            observation->Size != operation.OriginalSize ||
            observation->Digest != operation.OriginalDigest)
        {
            return false;
        }
    }
    return true;
}

bool PartyQuestReplicaRestoreJournal::VerifyRestoredTargets(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!ValidateState(acState))
        return false;

    for (const auto& operation : acState.Operations)
    {
        const auto observation =
            PartyQuestReplicaFileExecutor::ObserveRegularFile(
                operation.ReplicaDestinationPath);
        if (!observation ||
            observation->Size != operation.ExpectedRestoredSize ||
            observation->Digest != operation.ExpectedRestoredDigest)
        {
            return false;
        }
    }
    return true;
}

bool PartyQuestReplicaRestoreJournal::VerifyOriginalTargets(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!ValidateState(acState))
        return false;

    for (const auto& operation : acState.Operations)
    {
        bool destinationExists{};
        if (!IsRegularNonSymlink(operation.ReplicaDestinationPath, destinationExists))
            return false;

        if (!operation.DestinationExisted)
        {
            if (destinationExists)
                return false;
            continue;
        }

        if (!destinationExists)
            return false;
        const auto observation =
            PartyQuestReplicaFileExecutor::ObserveRegularFile(
                operation.ReplicaDestinationPath);
        if (!observation ||
            observation->Size != operation.OriginalSize ||
            observation->Digest != operation.OriginalDigest)
        {
            return false;
        }
    }
    return true;
}

PartyQuestReplicaRestoreJournalStatus PartyQuestReplicaRestoreJournal::MarkBackupsReady(
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    if (aState.Phase != PartyQuestReplicaRestoreJournalPhase::Prepared)
        return PartyQuestReplicaRestoreJournalStatus::InvalidTransition;
    if (!VerifyRollbackBackups(aState))
        return PartyQuestReplicaRestoreJournalStatus::BackupVerificationFailed;
    aState.Phase = PartyQuestReplicaRestoreJournalPhase::BackupsReady;
    return PartyQuestReplicaRestoreJournalStatus::Ready;
}

PartyQuestReplicaRestoreJournalStatus PartyQuestReplicaRestoreJournal::MarkMutationStarted(
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    if (aState.Phase != PartyQuestReplicaRestoreJournalPhase::BackupsReady)
        return PartyQuestReplicaRestoreJournalStatus::InvalidTransition;
    aState.Phase = PartyQuestReplicaRestoreJournalPhase::MutationStarted;
    return PartyQuestReplicaRestoreJournalStatus::Ready;
}

PartyQuestReplicaRestoreJournalStatus PartyQuestReplicaRestoreJournal::MarkRestored(
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    if (aState.Phase != PartyQuestReplicaRestoreJournalPhase::MutationStarted)
        return PartyQuestReplicaRestoreJournalStatus::InvalidTransition;
    if (!VerifyRestoredTargets(aState))
        return PartyQuestReplicaRestoreJournalStatus::RestoredVerificationFailed;
    aState.Phase = PartyQuestReplicaRestoreJournalPhase::Restored;
    return PartyQuestReplicaRestoreJournalStatus::Ready;
}

PartyQuestReplicaRestoreJournalStatus PartyQuestReplicaRestoreJournal::MarkCommitted(
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    if (aState.Phase != PartyQuestReplicaRestoreJournalPhase::Restored)
        return PartyQuestReplicaRestoreJournalStatus::InvalidTransition;
    if (!VerifyRestoredTargets(aState))
        return PartyQuestReplicaRestoreJournalStatus::RestoredVerificationFailed;
    aState.Phase = PartyQuestReplicaRestoreJournalPhase::Committed;
    return PartyQuestReplicaRestoreJournalStatus::Ready;
}

PartyQuestReplicaRestoreJournalStatus PartyQuestReplicaRestoreJournal::MarkRolledBack(
    PartyQuestReplicaRestoreJournalState& aState) noexcept
{
    if (aState.Phase != PartyQuestReplicaRestoreJournalPhase::MutationStarted &&
        aState.Phase != PartyQuestReplicaRestoreJournalPhase::Restored)
    {
        return PartyQuestReplicaRestoreJournalStatus::InvalidTransition;
    }
    if (!VerifyOriginalTargets(aState))
        return PartyQuestReplicaRestoreJournalStatus::OriginalVerificationFailed;
    aState.Phase = PartyQuestReplicaRestoreJournalPhase::RolledBack;
    return PartyQuestReplicaRestoreJournalStatus::Ready;
}

PartyQuestReplicaRestoreRecoveryDisposition
PartyQuestReplicaRestoreJournal::GetRecoveryDisposition(
    const PartyQuestReplicaRestoreJournalState& acState) noexcept
{
    if (!ValidateState(acState))
        return PartyQuestReplicaRestoreRecoveryDisposition::InvalidState;

    switch (acState.Phase)
    {
    case PartyQuestReplicaRestoreJournalPhase::Prepared:
    case PartyQuestReplicaRestoreJournalPhase::BackupsReady:
        return PartyQuestReplicaRestoreRecoveryDisposition::ResumeBeforeMutation;
    case PartyQuestReplicaRestoreJournalPhase::MutationStarted:
        return PartyQuestReplicaRestoreRecoveryDisposition::RollbackRequired;
    case PartyQuestReplicaRestoreJournalPhase::Restored:
        return PartyQuestReplicaRestoreRecoveryDisposition::VerifyThenCommit;
    case PartyQuestReplicaRestoreJournalPhase::Committed:
        return PartyQuestReplicaRestoreRecoveryDisposition::Clean;
    case PartyQuestReplicaRestoreJournalPhase::RolledBack:
        return PartyQuestReplicaRestoreRecoveryDisposition::RolledBackClean;
    }
    return PartyQuestReplicaRestoreRecoveryDisposition::InvalidState;
}

std::vector<uint8_t> PartyQuestReplicaRestoreJournalPersistence::Encode(
    const PartyQuestReplicaRestoreJournalState& acState)
{
    return EncodeForArchiveDurability(
        acState,
        PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
}

std::vector<uint8_t> PartyQuestReplicaRestoreJournalPersistence::EncodeForArchiveDurability(
    const PartyQuestReplicaRestoreJournalState& acState,
    PartyQuestReplicaRestoreJournalArchiveDurability aDurability)
{
    if (!ValidateState(acState))
        return {};

    const auto version = GetFormatVersion(aDurability);
    if (!version)
        return {};

    std::vector<uint8_t> payload;
    WriteInteger(payload, acState.CampaignId.High);
    WriteInteger(payload, acState.CampaignId.Low);
    WriteInteger(payload, acState.PlayerProfileId.High);
    WriteInteger(payload, acState.PlayerProfileId.Low);
    WriteInteger(payload, acState.RestoreId);
    WriteInteger<uint8_t>(payload, static_cast<uint8_t>(acState.CheckpointKind));
    WriteInteger(payload, acState.CampaignWorldRevision);
    WriteInteger<uint8_t>(payload, static_cast<uint8_t>(acState.Phase));
    WritePath(payload, acState.TransactionDirectory);
    WriteInteger<uint32_t>(payload, static_cast<uint32_t>(acState.Operations.size()));

    for (const auto& operation : acState.Operations)
    {
        WriteInteger<uint8_t>(payload, static_cast<uint8_t>(operation.Kind));
        WritePath(payload, operation.CheckpointSourcePath);
        WritePath(payload, operation.ReplicaDestinationPath);
        WritePath(payload, operation.RollbackPath);
        WriteInteger(payload, operation.ExpectedRestoredSize);
        WriteInteger(payload, operation.ExpectedRestoredDigest);
        WriteInteger<uint8_t>(payload, operation.DestinationExisted ? 1 : 0);
        WriteInteger(payload, operation.OriginalSize);
        WriteInteger(payload, operation.OriginalDigest);
    }

    std::vector<uint8_t> archive;
    archive.insert(archive.end(), kMagic.begin(), kMagic.end());
    WriteInteger<uint16_t>(archive, *version);
    WriteInteger<uint64_t>(archive, payload.size());
    archive.insert(archive.end(), payload.begin(), payload.end());
    WriteInteger<uint64_t>(
        archive,
        ComputeChecksum(payload.data(), payload.size()));
    return archive;
}

PartyQuestReplicaRestoreJournalPersistenceResult
PartyQuestReplicaRestoreJournalPersistence::Decode(
    const std::vector<uint8_t>& acBytes)
{
    PartyQuestReplicaRestoreJournalPersistenceResult result;
    if (acBytes.size() >
        PartyQuestDurableResourcePolicy::MaxReplicaMetadataArchiveBytes)
    {
        result.Status =
            PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        return result;
    }
    const size_t headerSize = kMagic.size() + sizeof(uint16_t) + sizeof(uint64_t);
    if (acBytes.size() < headerSize + sizeof(uint64_t))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::Truncated;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidMagic;
        return result;
    }

    size_t offset = kMagic.size();
    uint16_t version{};
    uint64_t payloadSize{};
    if (!ReadInteger(acBytes, offset, acBytes.size(), version) ||
        !ReadInteger(acBytes, offset, acBytes.size(), payloadSize))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::Truncated;
        return result;
    }
    if (!IsSupportedFormatVersion(version))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::UnsupportedVersion;
        return result;
    }
    result.ArchiveDurability = GetArchiveDurability(version);

    if (payloadSize > acBytes.size() ||
        offset > acBytes.size() ||
        acBytes.size() - offset < payloadSize + sizeof(uint64_t))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::Truncated;
        return result;
    }

    const size_t payloadStart = offset;
    const size_t payloadEnd = payloadStart + static_cast<size_t>(payloadSize);
    if (payloadEnd + sizeof(uint64_t) != acBytes.size())
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return result;
    }

    size_t checksumOffset = payloadEnd;
    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, checksumOffset, acBytes.size(), storedChecksum))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::Truncated;
        return result;
    }
    if (storedChecksum != ComputeChecksum(
            acBytes.data() + payloadStart,
            static_cast<size_t>(payloadSize)))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ChecksumMismatch;
        return result;
    }

    PartyQuestReplicaRestoreJournalState state;
    uint8_t checkpointKind{};
    uint8_t phase{};
    uint32_t operationCount{};
    offset = payloadStart;
    if (!ReadInteger(acBytes, offset, payloadEnd, state.CampaignId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.PlayerProfileId.Low) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.RestoreId) ||
        !ReadInteger(acBytes, offset, payloadEnd, checkpointKind) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.CampaignWorldRevision) ||
        !ReadInteger(acBytes, offset, payloadEnd, phase) ||
        !ReadPath(acBytes, offset, payloadEnd, state.TransactionDirectory) ||
        !ReadInteger(acBytes, offset, payloadEnd, operationCount) ||
        operationCount == 0 || operationCount > kMaxOperations)
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return result;
    }

    if ((version == kLegacyFormatVersion &&
         phase > static_cast<uint8_t>(PartyQuestReplicaRestoreJournalPhase::Committed)) ||
        (version != kLegacyFormatVersion &&
         phase > static_cast<uint8_t>(PartyQuestReplicaRestoreJournalPhase::RolledBack)))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return result;
    }

    state.CheckpointKind = static_cast<PartyQuestCheckpointKind>(checkpointKind);
    state.Phase = static_cast<PartyQuestReplicaRestoreJournalPhase>(phase);
    state.Operations.reserve(operationCount);
    for (uint32_t i = 0; i < operationCount; ++i)
    {
        PartyQuestReplicaRestoreJournalOperation operation;
        uint8_t kind{};
        uint8_t existed{};
        if (!ReadInteger(acBytes, offset, payloadEnd, kind) ||
            !ReadPath(acBytes, offset, payloadEnd, operation.CheckpointSourcePath) ||
            !ReadPath(acBytes, offset, payloadEnd, operation.ReplicaDestinationPath) ||
            !ReadPath(acBytes, offset, payloadEnd, operation.RollbackPath) ||
            !ReadInteger(acBytes, offset, payloadEnd, operation.ExpectedRestoredSize) ||
            !ReadInteger(acBytes, offset, payloadEnd, operation.ExpectedRestoredDigest) ||
            !ReadInteger(acBytes, offset, payloadEnd, existed) ||
            existed > 1 ||
            !ReadInteger(acBytes, offset, payloadEnd, operation.OriginalSize) ||
            !ReadInteger(acBytes, offset, payloadEnd, operation.OriginalDigest))
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
            return result;
        }
        if (kind > static_cast<uint8_t>(PartyQuestReplicaFileKind::ExternalSidecar))
        {
            result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
            return result;
        }
        operation.Kind = static_cast<PartyQuestReplicaFileKind>(kind);
        operation.DestinationExisted = existed != 0;
        state.Operations.push_back(std::move(operation));
    }

    if (offset != payloadEnd || !ValidateState(state))
    {
        result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        return result;
    }

    result.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    result.State = std::move(state);
    return result;
}

PartyQuestReplicaRestoreJournalPersistenceStatus
PartyQuestReplicaRestoreJournalPersistence::SaveAtomically(
    const std::filesystem::path& acPath,
    const PartyQuestReplicaRestoreJournalState& acState,
    PartyQuestReplicaRestoreJournalPersistenceHooks aHooks)
{
    try
    {
        if (acPath.empty())
            return PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;
        if (!PartyQuestDurableResourcePolicy::IsMutableFilesystemPathWithinBudget(acPath))
            return PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;

        const auto bytes = Encode(acState);
        if (bytes.empty())
            return PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData;

        std::error_code ec;
        if (!acPath.parent_path().empty())
        {
            std::filesystem::create_directories(acPath.parent_path(), ec);
            if (ec)
                return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }

        std::filesystem::path temporary = acPath;
        temporary += ".tmp";
        std::filesystem::path backup = acPath;
        backup += ".bak";

        for (const auto& evidence : {acPath, temporary, backup})
        {
            const auto domainStatus = ValidateExistingEvidenceDomain(
                evidence,
                PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
            if (domainStatus != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
                return domainStatus;
        }

        std::filesystem::remove(temporary, ec);
        if (ec && !IsMissingError(ec))
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        ec.clear();

        if (!WriteFile(temporary, bytes))
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

        const auto verify = RequireArchiveDurability(
            DecodeFile(temporary),
            PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
        if (verify.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success ||
            !verify.State || *verify.State != acState)
        {
            std::filesystem::remove(temporary, ec);
            return verify.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success
                ? PartyQuestReplicaRestoreJournalPersistenceStatus::InvalidData
                : verify.Status;
        }

        if (aHooks.Invoke(
                PartyQuestReplicaRestoreJournalPersistenceBoundary::TemporaryVerified) ==
            PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
        {
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }

        ec.clear();
        const bool primaryExists = std::filesystem::exists(acPath, ec);
        if (ec && !IsMissingError(ec))
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        ec.clear();

        if (primaryExists)
        {
            std::filesystem::remove(backup, ec);
            if (ec && !IsMissingError(ec))
                return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            ec.clear();
            std::filesystem::rename(acPath, backup, ec);
            if (ec)
                return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;

            if (aHooks.Invoke(
                    PartyQuestReplicaRestoreJournalPersistenceBoundary::PrimaryMovedToBackup) ==
                PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
            {
                return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
            }
        }

        std::filesystem::rename(temporary, acPath, ec);
        if (ec)
        {
            std::error_code restoreEc;
            const bool primaryMissing = !std::filesystem::exists(acPath, restoreEc);
            if ((!restoreEc || IsMissingError(restoreEc)) && primaryMissing)
            {
                restoreEc.clear();
                if (std::filesystem::exists(backup, restoreEc) && !restoreEc)
                    std::filesystem::rename(backup, acPath, restoreEc);
            }
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }

        if (aHooks.Invoke(
                PartyQuestReplicaRestoreJournalPersistenceBoundary::TemporaryPublished) ==
            PartyQuestReplicaRestoreJournalPersistenceDirective::FailClosed)
        {
            return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        }

        return PartyQuestReplicaRestoreJournalPersistenceStatus::Success;
    }
    catch (...)
    {
        return PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
    }
}

PartyQuestReplicaRestoreJournalPersistenceResult
PartyQuestReplicaRestoreJournalPersistence::Load(
    const std::filesystem::path& acPath)
{
    PartyQuestReplicaRestoreJournalPersistenceResult primary;
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(acPath))
    {
        primary.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        return primary;
    }

    primary = DecodeFile(acPath);
    if (primary.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        return RequireArchiveDurability(
            std::move(primary),
            PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
    }

    std::filesystem::path temporary = acPath;
    temporary += ".tmp";
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(temporary))
    {
        primary.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        primary.State.reset();
        return primary;
    }

    auto temp = DecodeFile(temporary);
    if (temp.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success &&
        temp.State)
    {
        temp = RequireArchiveDurability(
            std::move(temp),
            PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
        if (temp.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            return temp;

        try
        {
            std::error_code ec;
            std::filesystem::remove(acPath, ec);
            if (ec && !IsMissingError(ec))
            {
                temp.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
                temp.State.reset();
                return temp;
            }
            ec.clear();
            std::filesystem::rename(temporary, acPath, ec);
            if (!ec)
            {
                temp.UsedTemporary = true;
                return temp;
            }
        }
        catch (...)
        {
        }
        temp.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::IoError;
        temp.State.reset();
        return temp;
    }

    std::filesystem::path backup = acPath;
    backup += ".bak";
    if (!PartyQuestDurableResourcePolicy::IsFilesystemPathWithinBudget(backup))
    {
        primary.Status = PartyQuestReplicaRestoreJournalPersistenceStatus::ResourceLimitExceeded;
        primary.State.reset();
        return primary;
    }

    auto staleBackup = DecodeFile(backup);
    if (staleBackup.Status == PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
    {
        staleBackup = RequireArchiveDurability(
            std::move(staleBackup),
            PartyQuestReplicaRestoreJournalArchiveDurability::ProcessCrashResilient);
        if (staleBackup.Status != PartyQuestReplicaRestoreJournalPersistenceStatus::Success)
            return staleBackup;
        staleBackup.Status =
            PartyQuestReplicaRestoreJournalPersistenceStatus::BackupRecoveryRequired;
        return staleBackup;
    }

    return primary;
}
