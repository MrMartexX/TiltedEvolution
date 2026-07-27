#include <Structs/Skyrim/PartyQuestWireCodec.h>

#include <TiltedCore/Serialization.hpp>

#include <algorithm>
#include <unordered_set>

using TiltedPhoques::Serialization;

namespace
{
constexpr uint64_t kMaxQuestEntries = 100000;
constexpr uint64_t kMaxCollectionEntries = 1000000;

bool GameIdLess(const GameId& acLeft, const GameId& acRight) noexcept
{
    if (acLeft.ModId != acRight.ModId)
        return acLeft.ModId < acRight.ModId;

    return acLeft.BaseId < acRight.BaseId;
}

void WriteBool(TiltedPhoques::Buffer::Writer& aWriter, bool aValue) noexcept
{
    Serialization::WriteVarInt(aWriter, aValue ? 1 : 0);
}

bool ReadBool(TiltedPhoques::Buffer::Reader& aReader, bool& aValue) noexcept
{
    const uint64_t value = Serialization::ReadVarInt(aReader);
    if (value > 1)
        return false;

    aValue = value == 1;
    return true;
}

bool ReadCount(TiltedPhoques::Buffer::Reader& aReader, uint64_t aMaximum, size_t& aCount) noexcept
{
    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > aMaximum)
        return false;

    aCount = static_cast<size_t>(count);
    return true;
}

void WriteOptionalGameId(TiltedPhoques::Buffer::Writer& aWriter, const std::optional<GameId>& acId) noexcept
{
    WriteBool(aWriter, acId.has_value());
    if (acId)
        acId->Serialize(aWriter);
}

bool ReadOptionalGameId(TiltedPhoques::Buffer::Reader& aReader, std::optional<GameId>& aId) noexcept
{
    bool present{};
    if (!ReadBool(aReader, present))
        return false;

    if (!present)
    {
        aId.reset();
        return true;
    }

    GameId id;
    id.Deserialize(aReader);
    aId = id;
    return true;
}
} // namespace

namespace PartyQuestWireCodec
{
void SerializeQuestSnapshot(TiltedPhoques::Buffer::Writer& aWriter, const QuestSnapshot& acSnapshot) noexcept
{
    QuestSnapshot snapshot = acSnapshot;
    snapshot.Canonicalize();

    Serialization::WriteVarInt(aWriter, QuestSnapshot::SchemaVersion);
    snapshot.QuestId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(snapshot.Status));
    Serialization::WriteVarInt(aWriter, snapshot.CurrentStage);
    Serialization::WriteVarInt(aWriter, snapshot.Revision);
    Serialization::WriteVarInt(aWriter, snapshot.InitiatorPlayerId);

    WriteBool(aWriter, snapshot.SceneParticipantPlayerId.has_value());
    if (snapshot.SceneParticipantPlayerId)
        Serialization::WriteVarInt(aWriter, *snapshot.SceneParticipantPlayerId);

    Serialization::WriteVarInt(aWriter, snapshot.CompletedStages.size());
    for (uint16_t stage : snapshot.CompletedStages)
        Serialization::WriteVarInt(aWriter, stage);

    Serialization::WriteVarInt(aWriter, snapshot.Objectives.size());
    for (const auto& objective : snapshot.Objectives)
    {
        Serialization::WriteVarInt(aWriter, objective.Index);
        Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(objective.State));
    }

    Serialization::WriteVarInt(aWriter, snapshot.ReferenceAliases.size());
    for (const auto& alias : snapshot.ReferenceAliases)
    {
        Serialization::WriteVarInt(aWriter, alias.AliasId);
        WriteOptionalGameId(aWriter, alias.ReferenceId);
        WriteBool(aWriter, alias.IsQuestObject);
    }

    Serialization::WriteVarInt(aWriter, snapshot.LocationAliases.size());
    for (const auto& alias : snapshot.LocationAliases)
    {
        Serialization::WriteVarInt(aWriter, alias.AliasId);
        WriteOptionalGameId(aWriter, alias.LocationId);
    }

    Serialization::WriteVarInt(aWriter, snapshot.CreatedReferences.size());
    for (const auto& reference : snapshot.CreatedReferences)
        reference.Serialize(aWriter);
}

bool DeserializeQuestSnapshot(TiltedPhoques::Buffer::Reader& aReader, QuestSnapshot& aSnapshot) noexcept
{
    if (Serialization::ReadVarInt(aReader) != QuestSnapshot::SchemaVersion)
        return false;

    QuestSnapshot snapshot;
    snapshot.QuestId.Deserialize(aReader);

    const uint64_t status = Serialization::ReadVarInt(aReader);
    if (status > static_cast<uint8_t>(QuestSnapshotStatus::Failed))
        return false;
    snapshot.Status = static_cast<QuestSnapshotStatus>(status);

    snapshot.CurrentStage = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    snapshot.Revision = Serialization::ReadVarInt(aReader);
    snapshot.InitiatorPlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);

    bool hasSceneParticipant{};
    if (!ReadBool(aReader, hasSceneParticipant))
        return false;
    if (hasSceneParticipant)
        snapshot.SceneParticipantPlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);

    size_t count{};
    if (!ReadCount(aReader, kMaxCollectionEntries, count))
        return false;
    snapshot.CompletedStages.reserve(count);
    for (size_t i = 0; i < count; ++i)
        snapshot.CompletedStages.push_back(static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF));

    if (!ReadCount(aReader, kMaxCollectionEntries, count))
        return false;
    snapshot.Objectives.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        QuestObjectiveSnapshot objective;
        objective.Index = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
        const uint64_t objectiveState = Serialization::ReadVarInt(aReader);
        if (objectiveState > static_cast<uint8_t>(QuestObjectiveState::Failed))
            return false;
        objective.State = static_cast<QuestObjectiveState>(objectiveState);
        snapshot.Objectives.push_back(objective);
    }

    if (!ReadCount(aReader, kMaxCollectionEntries, count))
        return false;
    snapshot.ReferenceAliases.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        QuestReferenceAliasSnapshot alias;
        alias.AliasId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
        if (!ReadOptionalGameId(aReader, alias.ReferenceId) || !ReadBool(aReader, alias.IsQuestObject))
            return false;
        snapshot.ReferenceAliases.push_back(alias);
    }

    if (!ReadCount(aReader, kMaxCollectionEntries, count))
        return false;
    snapshot.LocationAliases.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        QuestLocationAliasSnapshot alias;
        alias.AliasId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
        if (!ReadOptionalGameId(aReader, alias.LocationId))
            return false;
        snapshot.LocationAliases.push_back(alias);
    }

    if (!ReadCount(aReader, kMaxCollectionEntries, count))
        return false;
    snapshot.CreatedReferences.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        GameId reference;
        reference.Deserialize(aReader);
        snapshot.CreatedReferences.push_back(reference);
    }

    if (!snapshot.QuestId)
        return false;

    snapshot.Canonicalize();
    aSnapshot = std::move(snapshot);
    return true;
}

void SerializeTransaction(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestTransaction& acTransaction) noexcept
{
    Serialization::WriteVarInt(aWriter, acTransaction.TransactionId);
    Serialization::WriteVarInt(aWriter, acTransaction.InitiatorPlayerId);
    acTransaction.QuestId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, acTransaction.ExpectedQuestRevision);
    SerializeQuestSnapshot(aWriter, acTransaction.ProposedSnapshot);
}

bool DeserializeTransaction(TiltedPhoques::Buffer::Reader& aReader, PartyQuestTransaction& aTransaction) noexcept
{
    PartyQuestTransaction transaction;
    transaction.TransactionId = Serialization::ReadVarInt(aReader);
    transaction.InitiatorPlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
    transaction.QuestId.Deserialize(aReader);
    transaction.ExpectedQuestRevision = Serialization::ReadVarInt(aReader);

    if (transaction.TransactionId == 0 || !transaction.QuestId ||
        !DeserializeQuestSnapshot(aReader, transaction.ProposedSnapshot) ||
        transaction.ProposedSnapshot.QuestId != transaction.QuestId)
    {
        return false;
    }

    aTransaction = std::move(transaction);
    return true;
}

void SerializeApplyResult(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestApplyResult& acResult) noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(acResult.Status));
    Serialization::WriteVarInt(aWriter, acResult.WorldRevision);
    Serialization::WriteVarInt(aWriter, acResult.QuestRevision);
}

bool DeserializeApplyResult(TiltedPhoques::Buffer::Reader& aReader, PartyQuestApplyResult& aResult) noexcept
{
    const uint64_t status = Serialization::ReadVarInt(aReader);
    if (status > static_cast<uint8_t>(PartyQuestApplyStatus::TransactionConflict))
        return false;

    aResult.Status = static_cast<PartyQuestApplyStatus>(status);
    aResult.WorldRevision = Serialization::ReadVarInt(aReader);
    aResult.QuestRevision = Serialization::ReadVarInt(aReader);
    return true;
}

void SerializeReplicaReport(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestReplicaReport& acReport) noexcept
{
    Serialization::WriteVarInt(aWriter, acReport.WorldRevision);

    std::vector<std::pair<GameId, PartyQuestReplicaEntry>> entries;
    entries.reserve(acReport.Quests.size());
    for (const auto& entry : acReport.Quests)
        entries.push_back(entry);

    std::sort(entries.begin(), entries.end(), [](const auto& acLeft, const auto& acRight)
    {
        return GameIdLess(acLeft.first, acRight.first);
    });

    Serialization::WriteVarInt(aWriter, entries.size());
    for (const auto& [questId, entry] : entries)
    {
        questId.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, entry.QuestRevision);
        Serialization::WriteVarInt(aWriter, entry.Digest);
    }
}

bool DeserializeReplicaReport(TiltedPhoques::Buffer::Reader& aReader, PartyQuestReplicaReport& aReport) noexcept
{
    PartyQuestReplicaReport report;
    report.WorldRevision = Serialization::ReadVarInt(aReader);

    size_t count{};
    if (!ReadCount(aReader, kMaxQuestEntries, count))
        return false;
    report.Quests.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        GameId questId;
        questId.Deserialize(aReader);
        if (!questId)
            return false;

        PartyQuestReplicaEntry entry;
        entry.QuestRevision = Serialization::ReadVarInt(aReader);
        entry.Digest = Serialization::ReadVarInt(aReader);

        if (!report.Quests.emplace(questId, entry).second)
            return false;
    }

    aReport = std::move(report);
    return true;
}

void SerializeRepairPlan(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestRepairPlan& acPlan) noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(acPlan.Status));
    Serialization::WriteVarInt(aWriter, acPlan.BaseClientWorldRevision);
    Serialization::WriteVarInt(aWriter, acPlan.TargetWorldRevision);
    Serialization::WriteVarInt(aWriter, acPlan.Items.size());

    for (const auto& item : acPlan.Items)
    {
        Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(item.Reason));
        SerializeQuestSnapshot(aWriter, item.CanonicalSnapshot);
    }
}

bool DeserializeRepairPlan(TiltedPhoques::Buffer::Reader& aReader, PartyQuestRepairPlan& aPlan) noexcept
{
    PartyQuestRepairPlan plan;

    const uint64_t status = Serialization::ReadVarInt(aReader);
    if (status > static_cast<uint8_t>(PartyQuestRepairPlanStatus::ClientAhead))
        return false;
    plan.Status = static_cast<PartyQuestRepairPlanStatus>(status);

    plan.BaseClientWorldRevision = Serialization::ReadVarInt(aReader);
    plan.TargetWorldRevision = Serialization::ReadVarInt(aReader);

    size_t count{};
    if (!ReadCount(aReader, kMaxQuestEntries, count))
        return false;
    plan.Items.reserve(count);

    std::unordered_set<GameId> seenQuestIds;
    seenQuestIds.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const uint64_t reason = Serialization::ReadVarInt(aReader);
        if (reason > static_cast<uint8_t>(PartyQuestRepairReason::DigestMismatch))
            return false;

        PartyQuestRepairItem item;
        item.Reason = static_cast<PartyQuestRepairReason>(reason);
        if (!DeserializeQuestSnapshot(aReader, item.CanonicalSnapshot) ||
            !seenQuestIds.emplace(item.CanonicalSnapshot.QuestId).second)
        {
            return false;
        }

        plan.Items.push_back(std::move(item));
    }

    if (plan.TargetWorldRevision < plan.BaseClientWorldRevision &&
        plan.Status != PartyQuestRepairPlanStatus::ClientAhead)
    {
        return false;
    }

    aPlan = std::move(plan);
    return true;
}
} // namespace PartyQuestWireCodec
