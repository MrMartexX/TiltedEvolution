#include <Structs/Skyrim/PartyQuestStatePersistence.h>
#include <Structs/Skyrim/PartyQuestDurableResourcePolicy.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace
{
constexpr std::array<uint8_t, 8> kMagic{'T', 'P', 'Q', 'S', 'T', 'A', 'T', 'E'};
constexpr uint16_t kLegacyUnboundFormatVersion = 1;
constexpr uint16_t kFormatVersion = 2;
constexpr uint32_t kMaxQuestCount = 100000;
constexpr uint64_t kMaxJournalEntries =
    PartyQuestDurableResourcePolicy::MaxCanonicalJournalRecords;
constexpr uint32_t kMaxCollectionEntries = 1000000;
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

template <class T, bool = std::is_enum_v<T>>
struct StorageType
{
    using Type = T;
};

template <class T>
struct StorageType<T, true>
{
    using Type = std::underlying_type_t<T>;
};

class Writer final
{
public:
    template <class T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void Write(T aValue)
    {
        using ValueType = typename StorageType<T>::Type;
        using UnsignedType = std::make_unsigned_t<ValueType>;
        const auto value = static_cast<UnsignedType>(aValue);

        for (size_t i = 0; i < sizeof(UnsignedType); ++i)
            m_bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }

    void WriteBytes(const uint8_t* apBytes, size_t aSize)
    {
        m_bytes.insert(m_bytes.end(), apBytes, apBytes + aSize);
    }

    void WriteBytes(const std::vector<uint8_t>& acBytes)
    {
        WriteBytes(acBytes.data(), acBytes.size());
    }

    [[nodiscard]] const std::vector<uint8_t>& GetBytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::vector<uint8_t> TakeBytes() noexcept { return std::move(m_bytes); }

private:
    std::vector<uint8_t> m_bytes;
};

class Reader final
{
public:
    Reader(const uint8_t* apData, size_t aSize) noexcept
        : m_pData(apData)
        , m_size(aSize)
    {
    }

    template <class T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    bool Read(T& aValue) noexcept
    {
        using ValueType = typename StorageType<T>::Type;
        using UnsignedType = std::make_unsigned_t<ValueType>;

        if (Remaining() < sizeof(UnsignedType))
            return false;

        UnsignedType value{};
        for (size_t i = 0; i < sizeof(UnsignedType); ++i)
            value |= static_cast<UnsignedType>(m_pData[m_offset + i]) << (i * 8);

        m_offset += sizeof(UnsignedType);
        aValue = static_cast<T>(value);
        return true;
    }

    bool ReadBytes(uint8_t* apDestination, size_t aSize) noexcept
    {
        if (Remaining() < aSize)
            return false;

        std::copy_n(m_pData + m_offset, aSize, apDestination);
        m_offset += aSize;
        return true;
    }

    bool Skip(size_t aSize) noexcept
    {
        if (Remaining() < aSize)
            return false;

        m_offset += aSize;
        return true;
    }

    [[nodiscard]] const uint8_t* Current() const noexcept { return m_pData + m_offset; }
    [[nodiscard]] size_t Remaining() const noexcept { return m_size - m_offset; }

private:
    const uint8_t* m_pData{};
    size_t m_size{};
    size_t m_offset{};
};

enum class ParseError : uint8_t
{
    None,
    Truncated,
    UnsupportedVersion,
    InvalidData
};

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

bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;

    return acLeft.BaseId < acRight.BaseId;
}

void WriteGameId(Writer& aWriter, const GameId& acId)
{
    aWriter.Write(acId.ModId);
    aWriter.Write(acId.BaseId);
}

bool ReadGameId(Reader& aReader, GameId& aId) noexcept
{
    return aReader.Read(aId.ModId) && aReader.Read(aId.BaseId);
}

void WriteOptionalGameId(Writer& aWriter, const std::optional<GameId>& acId)
{
    aWriter.Write<uint8_t>(acId.has_value() ? 1 : 0);
    if (acId)
        WriteGameId(aWriter, *acId);
}

bool ReadOptionalGameId(Reader& aReader, std::optional<GameId>& aId, ParseError& aError) noexcept
{
    uint8_t present{};
    if (!aReader.Read(present))
    {
        aError = ParseError::Truncated;
        return false;
    }

    if (present > 1)
    {
        aError = ParseError::InvalidData;
        return false;
    }

    if (present == 0)
    {
        aId.reset();
        return true;
    }

    GameId id;
    if (!ReadGameId(aReader, id))
    {
        aError = ParseError::Truncated;
        return false;
    }

    aId = id;
    return true;
}

void WriteSnapshot(Writer& aWriter, const QuestSnapshot& acSnapshot)
{
    QuestSnapshot snapshot = acSnapshot;
    snapshot.Canonicalize();

    aWriter.Write<uint16_t>(QuestSnapshot::SchemaVersion);
    WriteGameId(aWriter, snapshot.QuestId);
    aWriter.Write(snapshot.Status);
    aWriter.Write(snapshot.CurrentStage);
    aWriter.Write(snapshot.Revision);
    aWriter.Write(snapshot.InitiatorPlayerId);

    aWriter.Write<uint8_t>(snapshot.SceneParticipantPlayerId.has_value() ? 1 : 0);
    if (snapshot.SceneParticipantPlayerId)
        aWriter.Write(*snapshot.SceneParticipantPlayerId);

    aWriter.Write<uint32_t>(static_cast<uint32_t>(snapshot.CompletedStages.size()));
    for (uint16_t stage : snapshot.CompletedStages)
        aWriter.Write(stage);

    aWriter.Write<uint32_t>(static_cast<uint32_t>(snapshot.Objectives.size()));
    for (const auto& objective : snapshot.Objectives)
    {
        aWriter.Write(objective.Index);
        aWriter.Write(objective.State);
    }

    aWriter.Write<uint32_t>(static_cast<uint32_t>(snapshot.ReferenceAliases.size()));
    for (const auto& alias : snapshot.ReferenceAliases)
    {
        aWriter.Write(alias.AliasId);
        WriteOptionalGameId(aWriter, alias.ReferenceId);
        aWriter.Write<uint8_t>(alias.IsQuestObject ? 1 : 0);
    }

    aWriter.Write<uint32_t>(static_cast<uint32_t>(snapshot.LocationAliases.size()));
    for (const auto& alias : snapshot.LocationAliases)
    {
        aWriter.Write(alias.AliasId);
        WriteOptionalGameId(aWriter, alias.LocationId);
    }

    aWriter.Write<uint32_t>(static_cast<uint32_t>(snapshot.CreatedReferences.size()));
    for (const auto& reference : snapshot.CreatedReferences)
        WriteGameId(aWriter, reference);
}

bool ReadCount(Reader& aReader, uint32_t& aCount, ParseError& aError) noexcept
{
    if (!aReader.Read(aCount))
    {
        aError = ParseError::Truncated;
        return false;
    }

    if (aCount > kMaxCollectionEntries)
    {
        aError = ParseError::InvalidData;
        return false;
    }

    return true;
}

bool ReadSnapshot(Reader& aReader, QuestSnapshot& aSnapshot, ParseError& aError) noexcept
{
    uint16_t schemaVersion{};
    if (!aReader.Read(schemaVersion))
    {
        aError = ParseError::Truncated;
        return false;
    }

    if (schemaVersion != QuestSnapshot::SchemaVersion)
    {
        aError = ParseError::UnsupportedVersion;
        return false;
    }

    uint8_t status{};
    if (!ReadGameId(aReader, aSnapshot.QuestId) ||
        !aReader.Read(status) ||
        !aReader.Read(aSnapshot.CurrentStage) ||
        !aReader.Read(aSnapshot.Revision) ||
        !aReader.Read(aSnapshot.InitiatorPlayerId))
    {
        aError = ParseError::Truncated;
        return false;
    }

    if (status > static_cast<uint8_t>(QuestSnapshotStatus::Failed))
    {
        aError = ParseError::InvalidData;
        return false;
    }
    aSnapshot.Status = static_cast<QuestSnapshotStatus>(status);

    uint8_t hasSceneParticipant{};
    if (!aReader.Read(hasSceneParticipant))
    {
        aError = ParseError::Truncated;
        return false;
    }
    if (hasSceneParticipant > 1)
    {
        aError = ParseError::InvalidData;
        return false;
    }
    if (hasSceneParticipant == 1)
    {
        uint32_t playerId{};
        if (!aReader.Read(playerId))
        {
            aError = ParseError::Truncated;
            return false;
        }
        aSnapshot.SceneParticipantPlayerId = playerId;
    }

    uint32_t count{};
    if (!ReadCount(aReader, count, aError))
        return false;
    aSnapshot.CompletedStages.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint16_t stage{};
        if (!aReader.Read(stage))
        {
            aError = ParseError::Truncated;
            return false;
        }
        aSnapshot.CompletedStages.push_back(stage);
    }

    if (!ReadCount(aReader, count, aError))
        return false;
    aSnapshot.Objectives.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuestObjectiveSnapshot objective;
        uint8_t objectiveState{};
        if (!aReader.Read(objective.Index) || !aReader.Read(objectiveState))
        {
            aError = ParseError::Truncated;
            return false;
        }
        if (objectiveState > static_cast<uint8_t>(QuestObjectiveState::Failed))
        {
            aError = ParseError::InvalidData;
            return false;
        }
        objective.State = static_cast<QuestObjectiveState>(objectiveState);
        aSnapshot.Objectives.push_back(objective);
    }

    if (!ReadCount(aReader, count, aError))
        return false;
    aSnapshot.ReferenceAliases.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuestReferenceAliasSnapshot alias;
        uint8_t questObject{};
        if (!aReader.Read(alias.AliasId) ||
            !ReadOptionalGameId(aReader, alias.ReferenceId, aError) ||
            !aReader.Read(questObject))
        {
            if (aError == ParseError::None)
                aError = ParseError::Truncated;
            return false;
        }
        if (questObject > 1)
        {
            aError = ParseError::InvalidData;
            return false;
        }
        alias.IsQuestObject = questObject == 1;
        aSnapshot.ReferenceAliases.push_back(alias);
    }

    if (!ReadCount(aReader, count, aError))
        return false;
    aSnapshot.LocationAliases.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuestLocationAliasSnapshot alias;
        if (!aReader.Read(alias.AliasId) || !ReadOptionalGameId(aReader, alias.LocationId, aError))
        {
            if (aError == ParseError::None)
                aError = ParseError::Truncated;
            return false;
        }
        aSnapshot.LocationAliases.push_back(alias);
    }

    if (!ReadCount(aReader, count, aError))
        return false;
    aSnapshot.CreatedReferences.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        GameId reference;
        if (!ReadGameId(aReader, reference))
        {
            aError = ParseError::Truncated;
            return false;
        }
        aSnapshot.CreatedReferences.push_back(reference);
    }

    aSnapshot.Canonicalize();
    return true;
}

void WriteTransaction(Writer& aWriter, const PartyQuestTransaction& acTransaction)
{
    aWriter.Write(acTransaction.TransactionId);
    aWriter.Write(acTransaction.InitiatorPlayerId);
    WriteGameId(aWriter, acTransaction.QuestId);
    aWriter.Write(acTransaction.ExpectedQuestRevision);
    WriteSnapshot(aWriter, acTransaction.ProposedSnapshot);
}

bool ReadTransaction(Reader& aReader, PartyQuestTransaction& aTransaction, ParseError& aError) noexcept
{
    if (!aReader.Read(aTransaction.TransactionId) ||
        !aReader.Read(aTransaction.InitiatorPlayerId) ||
        !ReadGameId(aReader, aTransaction.QuestId) ||
        !aReader.Read(aTransaction.ExpectedQuestRevision))
    {
        aError = ParseError::Truncated;
        return false;
    }

    return ReadSnapshot(aReader, aTransaction.ProposedSnapshot, aError);
}

PartyQuestPersistenceStatus ConvertParseError(ParseError aError) noexcept
{
    switch (aError)
    {
    case ParseError::Truncated: return PartyQuestPersistenceStatus::Truncated;
    case ParseError::UnsupportedVersion: return PartyQuestPersistenceStatus::UnsupportedVersion;
    case ParseError::InvalidData: return PartyQuestPersistenceStatus::InvalidData;
    default: return PartyQuestPersistenceStatus::Success;
    }
}

PartyQuestPersistenceStatus ReadFile(const std::filesystem::path& acPath, std::vector<uint8_t>& aBytes)
{
    std::ifstream file(acPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return std::filesystem::exists(acPath) ? PartyQuestPersistenceStatus::IoError : PartyQuestPersistenceStatus::FileNotFound;

    const std::streampos end = file.tellg();
    if (end < 0)
        return PartyQuestPersistenceStatus::IoError;

    const auto size = static_cast<uint64_t>(end);
    if (size > PartyQuestDurableResourcePolicy::MaxCanonicalStateArchiveBytes ||
        size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return PartyQuestPersistenceStatus::InvalidData;

    aBytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!aBytes.empty() && !file.read(reinterpret_cast<char*>(aBytes.data()), static_cast<std::streamsize>(aBytes.size())))
        return PartyQuestPersistenceStatus::IoError;

    return PartyQuestPersistenceStatus::Success;
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
} // namespace

std::vector<uint8_t> PartyQuestStatePersistence::Encode(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestState& acState)
{
    if (!acCampaignId.IsValid())
        return {};

    Writer payload;
    payload.Write(acCampaignId.High);
    payload.Write(acCampaignId.Low);
    payload.Write(acState.GetWorldRevision());

    std::vector<const QuestSnapshot*> quests;
    quests.reserve(acState.GetQuestCount());
    for (const auto& [questId, snapshot] : acState.GetQuests())
    {
        (void)questId;
        quests.push_back(&snapshot);
    }
    std::sort(quests.begin(), quests.end(), [](const QuestSnapshot* apLeft, const QuestSnapshot* apRight)
    {
        return GameIdLess(apLeft->QuestId, apRight->QuestId);
    });

    payload.Write<uint32_t>(static_cast<uint32_t>(quests.size()));
    for (const QuestSnapshot* pQuest : quests)
        WriteSnapshot(payload, *pQuest);

    payload.Write<uint64_t>(static_cast<uint64_t>(acState.GetJournal().size()));
    for (const auto& entry : acState.GetJournal())
    {
        payload.Write(entry.WorldRevision);
        payload.Write(entry.QuestRevision);
        WriteTransaction(payload, entry.Transaction);
    }

    const auto& payloadBytes = payload.GetBytes();

    Writer archive;
    archive.WriteBytes(kMagic.data(), kMagic.size());
    archive.Write(kFormatVersion);
    archive.Write<uint64_t>(static_cast<uint64_t>(payloadBytes.size()));
    archive.WriteBytes(payloadBytes);
    archive.Write(ComputeChecksum(payloadBytes.data(), payloadBytes.size()));
    return archive.TakeBytes();
}

PartyQuestPersistenceResult PartyQuestStatePersistence::Decode(const std::vector<uint8_t>& acBytes)
{
    PartyQuestPersistenceResult result;
    if (acBytes.size() >
        PartyQuestDurableResourcePolicy::MaxCanonicalStateArchiveBytes)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }
    Reader archive(acBytes.data(), acBytes.size());

    std::array<uint8_t, kMagic.size()> magic{};
    if (!archive.ReadBytes(magic.data(), magic.size()))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }
    if (magic != kMagic)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidMagic;
        return result;
    }

    uint16_t formatVersion{};
    uint64_t payloadSize64{};
    if (!archive.Read(formatVersion) || !archive.Read(payloadSize64))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }
    if (formatVersion != kLegacyUnboundFormatVersion && formatVersion != kFormatVersion)
    {
        result.Status = PartyQuestPersistenceStatus::UnsupportedVersion;
        return result;
    }
    if (payloadSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }

    const size_t payloadSize = static_cast<size_t>(payloadSize64);
    if (archive.Remaining() < payloadSize + sizeof(uint64_t))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }

    const uint8_t* pPayload = archive.Current();
    if (!archive.Skip(payloadSize))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }

    uint64_t storedChecksum{};
    if (!archive.Read(storedChecksum))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }
    if (archive.Remaining() != 0)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }
    if (storedChecksum != ComputeChecksum(pPayload, payloadSize))
    {
        result.Status = PartyQuestPersistenceStatus::ChecksumMismatch;
        return result;
    }

    Reader payload(pPayload, payloadSize);
    if (formatVersion == kFormatVersion)
    {
        PartyQuestCampaignId campaignId;
        if (!payload.Read(campaignId.High) || !payload.Read(campaignId.Low))
        {
            result.Status = PartyQuestPersistenceStatus::Truncated;
            return result;
        }
        if (!campaignId.IsValid())
        {
            result.Status = PartyQuestPersistenceStatus::InvalidData;
            return result;
        }
        result.CampaignId = campaignId;
    }

    uint64_t checkpointWorldRevision{};
    uint32_t checkpointQuestCount{};
    if (!payload.Read(checkpointWorldRevision) || !payload.Read(checkpointQuestCount))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }
    if (checkpointQuestCount > kMaxQuestCount)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }

    std::unordered_map<GameId, QuestSnapshot> checkpointQuests;
    checkpointQuests.reserve(checkpointQuestCount);
    ParseError parseError = ParseError::None;
    for (uint32_t i = 0; i < checkpointQuestCount; ++i)
    {
        QuestSnapshot snapshot;
        if (!ReadSnapshot(payload, snapshot, parseError))
        {
            result.Status = ConvertParseError(parseError);
            return result;
        }
        if (!checkpointQuests.emplace(snapshot.QuestId, snapshot).second)
        {
            result.Status = PartyQuestPersistenceStatus::InvalidData;
            return result;
        }
    }

    uint64_t journalCount{};
    if (!payload.Read(journalCount))
    {
        result.Status = PartyQuestPersistenceStatus::Truncated;
        return result;
    }
    if (journalCount > kMaxJournalEntries || journalCount != checkpointWorldRevision)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }

    PartyQuestState replayed;
    for (uint64_t i = 0; i < journalCount; ++i)
    {
        PartyQuestJournalEntry entry;
        if (!payload.Read(entry.WorldRevision) || !payload.Read(entry.QuestRevision) ||
            !ReadTransaction(payload, entry.Transaction, parseError))
        {
            result.Status = parseError == ParseError::None
                ? PartyQuestPersistenceStatus::Truncated
                : ConvertParseError(parseError);
            return result;
        }

        const auto applyResult = replayed.Apply(entry.Transaction);
        if (applyResult.Status != PartyQuestApplyStatus::Accepted ||
            applyResult.WorldRevision != entry.WorldRevision ||
            applyResult.QuestRevision != entry.QuestRevision)
        {
            result.Status = PartyQuestPersistenceStatus::ReplayMismatch;
            return result;
        }
    }

    if (payload.Remaining() != 0)
    {
        result.Status = PartyQuestPersistenceStatus::InvalidData;
        return result;
    }
    if (replayed.GetWorldRevision() != checkpointWorldRevision ||
        replayed.GetQuestCount() != checkpointQuests.size())
    {
        result.Status = PartyQuestPersistenceStatus::ReplayMismatch;
        return result;
    }

    for (const auto& [questId, checkpointSnapshot] : checkpointQuests)
    {
        const QuestSnapshot* pReplayedSnapshot = replayed.FindQuest(questId);
        if (!pReplayedSnapshot || *pReplayedSnapshot != checkpointSnapshot)
        {
            result.Status = PartyQuestPersistenceStatus::ReplayMismatch;
            return result;
        }
    }

    result.Status = PartyQuestPersistenceStatus::Success;
    result.State = std::move(replayed);
    return result;
}

PartyQuestPersistenceStatus PartyQuestStatePersistence::SaveAtomically(
    const std::filesystem::path& acPath,
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestState& acState,
    PartyQuestStatePersistenceHooks aHooks)
{
    const auto encoded = Encode(acCampaignId, acState);
    if (encoded.empty())
        return PartyQuestPersistenceStatus::InvalidData;

    std::error_code ec;
    if (!acPath.parent_path().empty())
    {
        std::filesystem::create_directories(acPath.parent_path(), ec);
        if (ec)
            return PartyQuestPersistenceStatus::IoError;
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
        return PartyQuestPersistenceStatus::IoError;
    }

    std::vector<uint8_t> temporaryBytes;
    const auto temporaryRead = ReadFile(temporaryPath, temporaryBytes);
    PartyQuestPersistenceResult temporaryState;
    temporaryState.Status = temporaryRead;
    if (temporaryRead == PartyQuestPersistenceStatus::Success)
        temporaryState = Decode(temporaryBytes);
    if (temporaryState.Status != PartyQuestPersistenceStatus::Success ||
        temporaryState.CampaignId != acCampaignId ||
        !temporaryState.State || Encode(acCampaignId, *temporaryState.State) != encoded)
    {
        std::filesystem::remove(temporaryPath, ec);
        return temporaryState.Status == PartyQuestPersistenceStatus::Success
            ? PartyQuestPersistenceStatus::InvalidData
            : temporaryState.Status;
    }
    if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::TemporaryVerified) ==
        PartyQuestStatePersistenceDirective::FailClosed)
    {
        return PartyQuestPersistenceStatus::IoError;
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
            return PartyQuestPersistenceStatus::IoError;
        }
        if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::PrimaryMovedToBackup) ==
            PartyQuestStatePersistenceDirective::FailClosed)
        {
            return PartyQuestPersistenceStatus::IoError;
        }
    }

    std::filesystem::rename(temporaryPath, acPath, ec);
    if (ec)
    {
        std::error_code restoreError;
        if (hadPrimary && std::filesystem::exists(backupPath, restoreError))
            std::filesystem::rename(backupPath, acPath, restoreError);
        std::filesystem::remove(temporaryPath, restoreError);
        return PartyQuestPersistenceStatus::IoError;
    }

    if (aHooks.Invoke(PartyQuestStatePersistenceBoundary::TemporaryPublished) ==
        PartyQuestStatePersistenceDirective::FailClosed)
    {
        return PartyQuestPersistenceStatus::IoError;
    }

    return PartyQuestPersistenceStatus::Success;
}

PartyQuestPersistenceResult PartyQuestStatePersistence::Load(const std::filesystem::path& acPath)
{
    std::vector<uint8_t> bytes;
    const PartyQuestPersistenceStatus primaryReadStatus = ReadFile(acPath, bytes);

    PartyQuestPersistenceResult primaryResult;
    primaryResult.Status = primaryReadStatus;
    if (primaryReadStatus == PartyQuestPersistenceStatus::Success)
    {
        primaryResult = Decode(bytes);
        if (primaryResult.Status == PartyQuestPersistenceStatus::Success)
            return primaryResult;
    }

    auto temporaryPath = acPath;
    temporaryPath += ".tmp";
    bytes.clear();
    const PartyQuestPersistenceStatus temporaryReadStatus = ReadFile(temporaryPath, bytes);
    if (temporaryReadStatus == PartyQuestPersistenceStatus::Success)
    {
        auto temporaryResult = Decode(bytes);
        if (temporaryResult.Status == PartyQuestPersistenceStatus::Success)
        {
            temporaryResult.UsedTemporary = true;
            return temporaryResult;
        }
    }

    auto backupPath = acPath;
    backupPath += ".bak";
    bytes.clear();
    const PartyQuestPersistenceStatus backupReadStatus = ReadFile(backupPath, bytes);
    if (backupReadStatus == PartyQuestPersistenceStatus::Success)
    {
        auto backupResult = Decode(bytes);
        if (backupResult.Status == PartyQuestPersistenceStatus::Success)
        {
            backupResult.Status = PartyQuestPersistenceStatus::BackupRecoveryRequired;
            backupResult.UsedBackup = true;
            return backupResult;
        }
    }

    return primaryResult;
}
