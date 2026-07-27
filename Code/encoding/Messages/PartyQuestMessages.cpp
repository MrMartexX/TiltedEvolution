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
} // namespace

void RequestPartyQuestTransaction::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, RequestId);
    PartyQuestWireCodec::SerializeTransaction(aWriter, Transaction);
}

void RequestPartyQuestTransaction::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    RequestId = Serialization::ReadVarInt(aReader);
    IsValid = IsValid && RequestId != 0 && PartyQuestWireCodec::DeserializeTransaction(aReader, Transaction);
}

bool RequestPartyQuestTransaction::operator==(const RequestPartyQuestTransaction& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId &&
           Transaction == acRhs.Transaction && IsValid == acRhs.IsValid;
}

void RequestPartyQuestReplicaReport::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WriteProtocolHeader(aWriter);
    Serialization::WriteVarInt(aWriter, ReportId);
    WriteBool(aWriter, IsReconnect);
    PartyQuestWireCodec::SerializeReplicaReport(aWriter, Report);
}

void RequestPartyQuestReplicaReport::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    ReportId = Serialization::ReadVarInt(aReader);
    bool reconnect{};
    IsValid = IsValid && ReportId != 0 && ReadBool(aReader, reconnect) &&
              PartyQuestWireCodec::DeserializeReplicaReport(aReader, Report);
    IsReconnect = reconnect;
}

bool RequestPartyQuestReplicaReport::operator==(const RequestPartyQuestReplicaReport& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && ReportId == acRhs.ReportId &&
           IsReconnect == acRhs.IsReconnect && Report == acRhs.Report && IsValid == acRhs.IsValid;
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
    if (status > static_cast<uint8_t>(PartyQuestReplicaApplyStatus::InvalidPlan))
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
    PartyQuestWireCodec::SerializeRepairPlan(aWriter, Plan);
}

void NotifyPartyQuestRepairPlan::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    IsValid = ReadProtocolHeader(aReader);
    ReportId = Serialization::ReadVarInt(aReader);
    PlanId = Serialization::ReadVarInt(aReader);
    IsValid = IsValid && ReportId != 0 && PlanId != 0 &&
              PartyQuestWireCodec::DeserializeRepairPlan(aReader, Plan);
}

bool NotifyPartyQuestRepairPlan::operator==(const NotifyPartyQuestRepairPlan& acRhs) const noexcept
{
    return GetOpcode() == acRhs.GetOpcode() && ReportId == acRhs.ReportId &&
           PlanId == acRhs.PlanId && Plan == acRhs.Plan && IsValid == acRhs.IsValid;
}
