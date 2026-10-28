#include <Messages/PartyQuestMessages.h>

#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

namespace
{
void WriteProtocolHeader(TiltedPhoques::Buffer::Writer& aWriter) noexcept
{
    Serialization::WriteVarInt(aWriter, PartyQuestWireCodec::ProtocolVersion);
}

bool ReadProtocolHeader(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    return Serialization::ReadVarInt(aReader) == PartyQuestWireCodec::ProtocolVersion;
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

void WriteCampaignId(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestCampaignId& acCampaignId) noexcept
{
    Serialization::WriteVarInt(aWriter, acCampaignId.High);
    Serialization::WriteVarInt(aWriter, acCampaignId.Low);
}

void ReadCampaignId(TiltedPhoques::Buffer::Reader& aReader, PartyQuestCampaignId& aCampaignId) noexcept
{
    aCampaignId.High = Serialization::ReadVarInt(aReader);
    aCampaignId.Low = Serialization::ReadVarInt(aReader);
}

void WriteSyncFacts(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestSyncFacts& acFacts) noexcept
{
    Serialization::WriteVarInt(aWriter, acFacts.QuestType);
    WriteBool(aWriter, acFacts.HasStages);
    WriteBool(aWriter, acFacts.IsDisplayedInHud);
    WriteBool(aWriter, acFacts.HasDisplayName);
}

bool ReadSyncFacts(TiltedPhoques::Buffer::Reader& aReader, PartyQuestSyncFacts& aFacts) noexcept
{
    const uint64_t questType = Serialization::ReadVarInt(aReader);
    if (questType > 0xFF)
        return false;

    PartyQuestSyncFacts facts;
    facts.QuestType = static_cast<uint8_t>(questType);
    if (!ReadBool(aReader, facts.HasStages) ||
        !ReadBool(aReader, facts.IsDisplayedInHud) ||
        !ReadBool(aReader, facts.HasDisplayName))
    {
        return false;
    }

    aFacts = facts;
    return true;
}

bool SyncFactsEqual(const PartyQuestSyncFacts& acLeft, const PartyQuestSyncFacts& acRight) noexcept
{
    return acLeft.QuestType == acRight.QuestType &&
        acLeft.HasStages == acRight.HasStages &&
        acLeft.IsDisplayedInHud == acRight.IsDisplayedInHud &&
        acLeft.HasDisplayName == acRight.HasDisplayName;
}
} // namespace

void RequestPartyQuestTransaction::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, RequestId);
    PartyQuestWireCodec::SerializeTransaction(aWriter, Transaction);
    WriteSyncFacts(aWriter, SyncFacts);
}

void RequestPartyQuestTransaction::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    RequestId = Serialization::ReadVarInt(aReader);
    IsValid = IsValid && RequestId != 0 &&
        PartyQuestWireCodec::DeserializeTransaction(aReader, Transaction) &&
        ReadSyncFacts(aReader, SyncFacts);
}

bool RequestPartyQuestTransaction::operator==(const RequestPartyQuestTransaction& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId &&
           Transaction == acRhs.Transaction && SyncFactsEqual(SyncFacts, acRhs.SyncFacts) &&
           IsValid == acRhs.IsValid;
}

void RequestPartyQuestReplicaReport::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, ReportId);
    WriteCampaignId(aWriter, CampaignId);
    WriteBool(aWriter, IsReconnect);
    PartyQuestWireCodec::SerializeReplicaReport(aWriter, Report);
}

void RequestPartyQuestReplicaReport::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    ReportId = Serialization::ReadVarInt(aReader);
    ReadCampaignId(aReader, CampaignId);
    bool reconnect{};
    IsValid = IsValid && ReportId != 0 && ReadBool(aReader, reconnect) &&
              PartyQuestWireCodec::DeserializeReplicaReport(aReader, Report);
    IsReconnect = reconnect;
}

bool RequestPartyQuestReplicaReport::operator==(const RequestPartyQuestReplicaReport& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && ReportId == acRhs.ReportId &&
           CampaignId == acRhs.CampaignId && IsReconnect == acRhs.IsReconnect &&
           Report == acRhs.Report && IsValid == acRhs.IsValid;
}

void RequestPartyQuestRepairAck::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, PlanId);
    Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(ApplyStatus));
    PartyQuestWireCodec::SerializeReplicaReport(aWriter, PostApplyReport);
}

void RequestPartyQuestRepairAck::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    PlanId = Serialization::ReadVarInt(aReader);

    const uint64_t status = Serialization::ReadVarInt(aReader);
    if (status > static_cast<uint8_t>(PartyQuestReplicaApplyStatus::ResourceLimitExceeded))
    {
        IsValid = false;
        return;
    }

    ApplyStatus = static_cast<PartyQuestReplicaApplyStatus>(status);
    IsValid = IsValid && PlanId != 0 && PartyQuestWireCodec::DeserializeReplicaReport(aReader, PostApplyReport);
}

bool RequestPartyQuestRepairAck::operator==(const RequestPartyQuestRepairAck& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && PlanId == acRhs.PlanId &&
           ApplyStatus == acRhs.ApplyStatus && PostApplyReport == acRhs.PostApplyReport &&
           IsValid == acRhs.IsValid;
}

void NotifyPartyQuestTransactionResult::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, RequestId);
    PartyQuestWireCodec::SerializeApplyResult(aWriter, Result);
    WriteBool(aWriter, CanonicalSnapshot.has_value());
    if (CanonicalSnapshot)
        PartyQuestWireCodec::SerializeQuestSnapshot(aWriter, *CanonicalSnapshot);
}

void NotifyPartyQuestTransactionResult::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    RequestId = Serialization::ReadVarInt(aReader);
    IsValid = IsValid && RequestId != 0 && PartyQuestWireCodec::DeserializeApplyResult(aReader, Result);

    bool hasSnapshot{};
    IsValid = IsValid && ReadBool(aReader, hasSnapshot);
    if (hasSnapshot)
    {
        QuestSnapshot snapshot;
        IsValid = IsValid && PartyQuestWireCodec::DeserializeQuestSnapshot(aReader, snapshot);
        if (IsValid)
            CanonicalSnapshot = std::move(snapshot);
    }
    else
    {
        CanonicalSnapshot.reset();
    }
}

bool NotifyPartyQuestTransactionResult::operator==(const NotifyPartyQuestTransactionResult& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId &&
           Result == acRhs.Result && CanonicalSnapshot == acRhs.CanonicalSnapshot &&
           IsValid == acRhs.IsValid;
}

void NotifyPartyQuestRepairPlan::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, ReportId);
    Serialization::WriteVarInt(aWriter, PlanId);
    WriteCampaignId(aWriter, CampaignId);
    PartyQuestWireCodec::SerializeRepairPlan(aWriter, Plan);
}

void NotifyPartyQuestRepairPlan::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    ReportId = Serialization::ReadVarInt(aReader);
    PlanId = Serialization::ReadVarInt(aReader);
    ReadCampaignId(aReader, CampaignId);
    IsValid = IsValid && ReportId != 0 && PlanId != 0 && CampaignId.IsValid() &&
              PartyQuestWireCodec::DeserializeRepairPlan(aReader, Plan);
}

bool NotifyPartyQuestRepairPlan::operator==(const NotifyPartyQuestRepairPlan& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && ReportId == acRhs.ReportId &&
           PlanId == acRhs.PlanId && CampaignId == acRhs.CampaignId &&
           Plan == acRhs.Plan && IsValid == acRhs.IsValid;
}

void NotifyPartyQuestCanonicalUpdate::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, TransactionId);
    Serialization::WriteVarInt(aWriter, WorldRevision);
    Serialization::WriteVarInt(aWriter, InitiatorPlayerId);
    PartyQuestWireCodec::SerializeQuestSnapshot(aWriter, CanonicalSnapshot);
}

void NotifyPartyQuestCanonicalUpdate::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    TransactionId = Serialization::ReadVarInt(aReader);
    WorldRevision = Serialization::ReadVarInt(aReader);
    InitiatorPlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    IsValid = IsValid && TransactionId != 0 && WorldRevision != 0 && InitiatorPlayerId != 0 &&
              PartyQuestWireCodec::DeserializeQuestSnapshot(aReader, CanonicalSnapshot) &&
              CanonicalSnapshot.QuestId && CanonicalSnapshot.Revision != 0 &&
              CanonicalSnapshot.InitiatorPlayerId == InitiatorPlayerId;
}

bool NotifyPartyQuestCanonicalUpdate::operator==(const NotifyPartyQuestCanonicalUpdate& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && TransactionId == acRhs.TransactionId &&
           WorldRevision == acRhs.WorldRevision && InitiatorPlayerId == acRhs.InitiatorPlayerId &&
           CanonicalSnapshot == acRhs.CanonicalSnapshot && IsValid == acRhs.IsValid;
}
