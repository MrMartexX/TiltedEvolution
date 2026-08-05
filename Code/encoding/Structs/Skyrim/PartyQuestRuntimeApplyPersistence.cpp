#include <Structs/Skyrim/PartyQuestRuntimeApplyPersistence.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'R', 'A', 'P', 'P', 'L'};
constexpr uint16_t kFormatVersion = 3;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint64_t kMaxCommittedEntries = 1000000;
constexpr uint32_t kKnownActionMask =
    static_cast<uint32_t>(PartyQuestApplyAction::StageTransition) |
    static_cast<uint32_t>(PartyQuestApplyAction::VerifyObjectives) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForWorldTargets) |
    static_cast<uint32_t>(PartyQuestApplyAction::WaitForPapyrusQuiescence) |
    static_cast<uint32_t>(PartyQuestApplyAction::ResnapshotAndVerify) |
    static_cast<uint32_t>(PartyQuestApplyAction::AdapterManaged);

template <class T>
void WriteInteger(std::vector<uint8_t>& aBytes, T aValue)
{
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    const auto value = static_cast<UnsignedType>(aValue);
    for (size_t i = 0; i < sizeof(UnsignedType); ++i)
        aBytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}

template <class T>
bool ReadInteger(const std::vector<uint8_t>& acBytes, size_t& aOffset, size_t aEnd, T& aValue) noexcept
{
    static_assert(std::is_integral_v<T>);
    using UnsignedType = std::make_unsigned_t<T>;
    if (aOffset > aEnd || aEnd - aOffset < sizeof(UnsignedType) || aEnd > acBytes.size())
        return false;

    UnsignedType value{};
    for (size_t i = 0; i < sizeof(UnsignedType); ++i)
        value |= static_cast<UnsignedType>(acBytes[aOffset + i]) << (i * 8);

    aOffset += sizeof(UnsignedType);
    aValue = static_cast<T>(value);
    return true;
}

void WriteBool(std::vector<uint8_t>& aBytes, bool aValue)
{
    WriteInteger<uint8_t>(aBytes, aValue ? 1 : 0);
}

bool ReadBool(const std::vector<uint8_t>& acBytes, size_t& aOffset, size_t aEnd, bool& aValue) noexcept
{
    uint8_t value{};
    if (!ReadInteger(acBytes, aOffset, aEnd, value) || value > 1)
        return false;
    aValue = value != 0;
    return true;
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

bool IsValidActions(uint32_t aActions) noexcept
{
    return aActions != 0 && (aActions & ~kKnownActionMask) == 0;
}

void WriteFingerprint(
    std::vector<uint8_t>& aBytes,
    uint64_t aTransactionId,
    uint64_t aTargetWorldRevision,
    const GameId& acQuestId,
    uint64_t aCanonicalDigest,
    uint64_t aSidecarManifestFingerprint,
    PartyQuestApplyAction aActions)
{
    WriteInteger(aBytes, aTransactionId);
    WriteInteger(aBytes, aTargetWorldRevision);
    WriteInteger(aBytes, acQuestId.ModId);
    WriteInteger(aBytes, acQuestId.BaseId);
    WriteInteger(aBytes, aCanonicalDigest);
    WriteInteger(aBytes, aSidecarManifestFingerprint);
    WriteInteger(aBytes, static_cast<uint32_t>(aActions));
}

bool ReadFingerprint(
    const std::vector<uint8_t>& acBytes,
    size_t& aOffset,
    size_t aEnd,
    uint64_t& aTransactionId,
    uint64_t& aTargetWorldRevision,
    GameId& aQuestId,
    uint64_t& aCanonicalDigest,
    uint64_t& aSidecarManifestFingerprint,
    PartyQuestApplyAction& aActions) noexcept
{
    uint32_t modId{};
    uint32_t baseId{};
    uint32_t actions{};
    if (!ReadInteger(acBytes, aOffset, aEnd, aTransactionId) ||
        !ReadInteger(acBytes, aOffset, aEnd, aTargetWorldRevision) ||
        !ReadInteger(acBytes, aOffset, aEnd, modId) ||
        !ReadInteger(acBytes, aOffset, aEnd, baseId) ||
        !ReadInteger(acBytes, aOffset, aEnd, aCanonicalDigest) ||
        !ReadInteger(acBytes, aOffset, aEnd, aSidecarManifestFingerprint) ||
        !ReadInteger(acBytes, aOffset, aEnd, actions))
    {
        return false;
    }

    aQuestId = GameId(modId, baseId);
    if (aTransactionId == 0 ||
        aTargetWorldRevision == 0 ||
        !aQuestId ||
        aCanonicalDigest == 0 ||
        aSidecarManifestFingerprint == 0 ||
        !IsValidActions(actions))
    {
        return false;
    }

    aActions = static_cast<PartyQuestApplyAction>(actions);
    return true;
}

PartyQuestRuntimeApplyPersistenceStatus ReadFile(
    const std::filesystem::path& acPath,
    std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return std::filesystem::exists(acPath)
            ? PartyQuestRuntimeApplyPersistenceStatus::IoError
            : PartyQuestRuntimeApplyPersistenceStatus::FileNotFound;
    }

    const std::streampos end = file.tellg();
    if (end < 0)
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;

    const auto size = static_cast<uint64_t>(end);
    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return PartyQuestRuntimeApplyPersistenceStatus::InvalidData;

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty() &&
        !file.read(reinterpret_cast<char*>(aBytes.data()), static_cast<std::streamsize>(aBytes.size())))
    {
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    return PartyQuestRuntimeApplyPersistenceStatus::Success;
}

bool WriteFile(const std::filesystem::path& acPath, const std::vector<uint8_t>& acBytes)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    if (!acBytes.empty())
        file.write(reinterpret_cast<const char*>(acBytes.data()), static_cast<std::streamsize>(acBytes.size()));

    file.flush();
    return file.good();
}

PartyQuestRuntimeApplyPersistenceResult DecodeFile(const std::filesystem::path& acPath)
{
    std::vector<uint8_t> bytes;
    PartyQuestRuntimeApplyPersistenceResult result;
    result.Status = ReadFile(acPath, bytes);
    if (result.Status != PartyQuestRuntimeApplyPersistenceStatus::Success)
        return result;

    return PartyQuestRuntimeApplyPersistence::Decode(bytes);
}
} // namespace

std::vector<uint8_t> PartyQuestRuntimeApplyPersistence::Encode(
    const PartyQuestRuntimeRecoveryState& acState)
{
    if (!acState.CampaignId.IsValid() || !acState.PlayerProfileId.IsValid())
        return {};

    std::vector<PartyQuestRuntimeCommittedRecord> committed = acState.Committed;
    std::sort(committed.begin(), committed.end(), [](const auto& acLeft, const auto& acRight)
    {
        return acLeft.TransactionId < acRight.TransactionId;
    });

    std::unordered_set<uint64_t> transactionIds;
    transactionIds.reserve(committed.size() + (acState.Active ? 1 : 0));

    std::vector<uint8_t> payload;
    WriteInteger(payload, acState.CampaignId.High);
    WriteInteger(payload, acState.CampaignId.Low);
    WriteInteger(payload, acState.PlayerProfileId.High);
    WriteInteger(payload, acState.PlayerProfileId.Low);
    WriteInteger<uint64_t>(payload, committed.size());
    for (const PartyQuestRuntimeCommittedRecord& record : committed)
    {
        if (record.TransactionId == 0 ||
            record.TargetWorldRevision == 0 ||
            !record.QuestId ||
            record.CanonicalDigest == 0 ||
            record.SidecarManifestFingerprint == 0 ||
            !IsValidActions(static_cast<uint32_t>(record.Actions)) ||
            !transactionIds.emplace(record.TransactionId).second)
        {
            return {};
        }

        WriteFingerprint(
            payload,
            record.TransactionId,
            record.TargetWorldRevision,
            record.QuestId,
            record.CanonicalDigest,
            record.SidecarManifestFingerprint,
            record.Actions);
    }

    WriteBool(payload, acState.Active.has_value());
    if (acState.Active)
    {
        const PartyQuestRuntimeApplyEntry& active = *acState.Active;
        if (active.TransactionId == 0 ||
            active.TargetWorldRevision == 0 ||
            !active.QuestId ||
            active.CanonicalDigest == 0 ||
            active.SidecarManifestFingerprint == 0 ||
            !IsValidActions(static_cast<uint32_t>(active.Actions)) ||
            !transactionIds.emplace(active.TransactionId).second)
        {
            return {};
        }

        WriteFingerprint(
            payload,
            active.TransactionId,
            active.TargetWorldRevision,
            active.QuestId,
            active.CanonicalDigest,
            active.SidecarManifestFingerprint,
            active.Actions);
        WriteInteger<uint8_t>(payload, static_cast<uint8_t>(active.State));
        WriteBool(payload, active.SaveGuardActive);
        WriteBool(payload, active.CheckpointCreated);
        WriteBool(payload, active.RuntimeMutationMayHaveOccurred);
        WriteInteger(payload, active.LastObservedDigest);
        WriteInteger(payload, active.StableCanonicalSamples);
    }

    std::vector<uint8_t> archive;
    archive.reserve(kMagic.size() + sizeof(uint16_t) + sizeof(uint64_t) + payload.size() + sizeof(uint64_t));
    archive.insert(archive.end(), kMagic.begin(), kMagic.end());
    WriteInteger(archive, kFormatVersion);
    WriteInteger<uint64_t>(archive, payload.size());
    archive.insert(archive.end(), payload.begin(), payload.end());
    WriteInteger(archive, ComputeChecksum(payload.data(), payload.size()));
    return archive;
}

PartyQuestRuntimeApplyPersistenceResult PartyQuestRuntimeApplyPersistence::Decode(
    const std::vector<uint8_t>& acBytes)
{
    PartyQuestRuntimeApplyPersistenceResult result;
    size_t offset{};

    if (acBytes.size() < kMagic.size())
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), acBytes.begin()))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidMagic;
        return result;
    }
    offset += kMagic.size();

    uint16_t version{};
    uint64_t payloadSize64{};
    if (!ReadInteger(acBytes, offset, acBytes.size(), version) ||
        !ReadInteger(acBytes, offset, acBytes.size(), payloadSize64))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (version != kFormatVersion)
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::UnsupportedVersion;
        return result;
    }
    if (payloadSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }
    if (offset > acBytes.size())
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }

    const size_t payloadSize = static_cast<size_t>(payloadSize64);
    const size_t remaining = acBytes.size() - offset;
    if (payloadSize > remaining)
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (remaining - payloadSize < sizeof(uint64_t))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (remaining - payloadSize != sizeof(uint64_t))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }

    const size_t payloadOffset = offset;
    const size_t payloadEnd = payloadOffset + payloadSize;
    size_t checksumOffset = payloadEnd;
    uint64_t storedChecksum{};
    if (!ReadInteger(acBytes, checksumOffset, acBytes.size(), storedChecksum) || checksumOffset != acBytes.size())
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }
    if (storedChecksum != ComputeChecksum(acBytes.data() + payloadOffset, payloadSize))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::ChecksumMismatch;
        return result;
    }

    PartyQuestRuntimeRecoveryState state;
    if (!ReadInteger(acBytes, offset, payloadEnd, state.CampaignId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.CampaignId.Low) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.PlayerProfileId.High) ||
        !ReadInteger(acBytes, offset, payloadEnd, state.PlayerProfileId.Low))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (!state.CampaignId.IsValid() || !state.PlayerProfileId.IsValid())
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }

    uint64_t committedCount{};
    if (!ReadInteger(acBytes, offset, payloadEnd, committedCount))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::Truncated;
        return result;
    }
    if (committedCount > kMaxCommittedEntries)
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }

    std::unordered_set<uint64_t> transactionIds;
    transactionIds.reserve(static_cast<size_t>(committedCount) + 1);
    state.Committed.reserve(static_cast<size_t>(committedCount));

    for (uint64_t i = 0; i < committedCount; ++i)
    {
        PartyQuestRuntimeCommittedRecord record;
        if (!ReadFingerprint(
                acBytes,
                offset,
                payloadEnd,
                record.TransactionId,
                record.TargetWorldRevision,
                record.QuestId,
                record.CanonicalDigest,
                record.SidecarManifestFingerprint,
                record.Actions))
        {
            result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
            return result;
        }
        if (!transactionIds.emplace(record.TransactionId).second)
        {
            result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
            return result;
        }
        state.Committed.push_back(record);
    }

    bool hasActive{};
    if (!ReadBool(acBytes, offset, payloadEnd, hasActive))
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }

    if (hasActive)
    {
        PartyQuestRuntimeApplyEntry active;
        if (!ReadFingerprint(
                acBytes,
                offset,
                payloadEnd,
                active.TransactionId,
                active.TargetWorldRevision,
                active.QuestId,
                active.CanonicalDigest,
                active.SidecarManifestFingerprint,
                active.Actions) ||
            !transactionIds.emplace(active.TransactionId).second)
        {
            result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
            return result;
        }

        uint8_t stateValue{};
        if (!ReadInteger(acBytes, offset, payloadEnd, stateValue) ||
            stateValue > static_cast<uint8_t>(PartyQuestRuntimeApplyState::ReadyToCommit) ||
            !ReadBool(acBytes, offset, payloadEnd, active.SaveGuardActive) ||
            !ReadBool(acBytes, offset, payloadEnd, active.CheckpointCreated) ||
            !ReadBool(acBytes, offset, payloadEnd, active.RuntimeMutationMayHaveOccurred) ||
            !ReadInteger(acBytes, offset, payloadEnd, active.LastObservedDigest) ||
            !ReadInteger(acBytes, offset, payloadEnd, active.StableCanonicalSamples))
        {
            result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
            return result;
        }
        active.State = static_cast<PartyQuestRuntimeApplyState>(stateValue);
        state.Active = active;
    }

    if (offset != payloadEnd)
    {
        result.Status = PartyQuestRuntimeApplyPersistenceStatus::InvalidData;
        return result;
    }

    result.Status = PartyQuestRuntimeApplyPersistenceStatus::Success;
    result.State = std::move(state);
    return result;
}

PartyQuestRuntimeApplyPersistenceStatus PartyQuestRuntimeApplyPersistence::SaveAtomically(
    const std::filesystem::path& acPath,
    const PartyQuestRuntimeRecoveryState& acState)
{
    const std::vector<uint8_t> encoded = Encode(acState);
    if (encoded.empty())
        return PartyQuestRuntimeApplyPersistenceStatus::InvalidData;

    std::error_code ec;
    if (!acPath.parent_path().empty())
    {
        std::filesystem::create_directories(acPath.parent_path(), ec);
        if (ec)
            return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    auto temporaryPath = acPath;
    temporaryPath += ".tmp";
    auto backupPath = acPath;
    backupPath += ".bak";

    std::filesystem::remove(temporaryPath, ec);
    ec.clear();

    if (!WriteFile(temporaryPath, encoded))
    {
        std::filesystem::remove(temporaryPath, ec);
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    const bool hadPrimary = std::filesystem::exists(acPath, ec) && !ec;
    if (hadPrimary)
    {
        std::filesystem::remove(backupPath, ec);
        ec.clear();
        std::filesystem::rename(acPath, backupPath, ec);
        if (ec)
        {
            std::filesystem::remove(temporaryPath, ec);
            return PartyQuestRuntimeApplyPersistenceStatus::IoError;
        }
    }

    std::filesystem::rename(temporaryPath, acPath, ec);
    if (ec)
    {
        std::error_code restoreError;
        if (hadPrimary && std::filesystem::exists(backupPath, restoreError))
            std::filesystem::rename(backupPath, acPath, restoreError);
        std::filesystem::remove(temporaryPath, restoreError);
        return PartyQuestRuntimeApplyPersistenceStatus::IoError;
    }

    return PartyQuestRuntimeApplyPersistenceStatus::Success;
}

PartyQuestRuntimeApplyPersistenceResult PartyQuestRuntimeApplyPersistence::Load(
    const std::filesystem::path& acPath)
{
    // Primary is authoritative whenever it is intact.
    PartyQuestRuntimeApplyPersistenceResult primaryResult = DecodeFile(acPath);
    if (primaryResult.Status == PartyQuestRuntimeApplyPersistenceStatus::Success)
        return primaryResult;

    // SaveAtomically writes and flushes the complete new archive to .tmp before
    // moving the old primary to .bak. If a process dies between those renames,
    // the valid .tmp is the newest complete recovery journal and is safer than
    // rolling back to the older backup.
    auto temporaryPath = acPath;
    temporaryPath += ".tmp";
    PartyQuestRuntimeApplyPersistenceResult temporaryResult = DecodeFile(temporaryPath);
    if (temporaryResult.Status == PartyQuestRuntimeApplyPersistenceStatus::Success)
    {
        temporaryResult.UsedTemporary = true;
        return temporaryResult;
    }

    // A backup is necessarily older than the failed/missing primary. Unlike a
    // canonical state snapshot, silently rolling this journal backward could
    // forget that a Skyrim mutation was armed/committed and allow duplicate
    // side effects. Expose the backup for explicit checkpoint recovery only.
    auto backupPath = acPath;
    backupPath += ".bak";
    PartyQuestRuntimeApplyPersistenceResult backupResult = DecodeFile(backupPath);
    if (backupResult.Status == PartyQuestRuntimeApplyPersistenceStatus::Success)
    {
        backupResult.Status = PartyQuestRuntimeApplyPersistenceStatus::BackupRecoveryRequired;
        backupResult.UsedBackup = true;
        return backupResult;
    }

    return primaryResult;
}