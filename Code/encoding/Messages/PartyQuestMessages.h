#pragma once

#include <Messages/Message.h>
#include <Structs/Skyrim/PartyQuestCampaign.h>
#include <Structs/Skyrim/PartyQuestWireCodec.h>

#include <optional>

/** Client proposal for one canonical party quest transition. */
struct RequestPartyQuestTransaction final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPartyQuestTransaction;

    RequestPartyQuestTransaction()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPartyQuestTransaction& acRhs) const noexcept;

    uint64_t RequestId{};
    PartyQuestTransaction Transaction;
    bool IsValid{true};
};

/** Compact periodic or reconnect report of a client's local campaign replica. */
struct RequestPartyQuestReplicaReport final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPartyQuestReplicaReport;

    RequestPartyQuestReplicaReport()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPartyQuestReplicaReport& acRhs) const noexcept;

    uint64_t ReportId{};
    PartyQuestCampaignId CampaignId;
    bool IsReconnect{};
    PartyQuestReplicaReport Report;
    bool IsValid{true};
};

/** Client acknowledgement containing a compact post-repair verification report. */
struct RequestPartyQuestRepairAck final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPartyQuestRepairAck;

    RequestPartyQuestRepairAck()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPartyQuestRepairAck& acRhs) const noexcept;

    uint64_t PlanId{};
    PartyQuestReplicaApplyStatus ApplyStatus{PartyQuestReplicaApplyStatus::InvalidPlan};
    PartyQuestReplicaReport PostApplyReport;
    bool IsValid{true};
};

/** Result of a submitted quest transaction, correlated by RequestId. */
struct NotifyPartyQuestTransactionResult final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyQuestTransactionResult;

    NotifyPartyQuestTransactionResult()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyQuestTransactionResult& acRhs) const noexcept;

    uint64_t RequestId{};
    PartyQuestApplyResult Result;
    std::optional<QuestSnapshot> CanonicalSnapshot;
    bool IsValid{true};
};

/** Server repair response correlated to one replica report and one unique plan. */
struct NotifyPartyQuestRepairPlan final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyQuestRepairPlan;

    NotifyPartyQuestRepairPlan()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyQuestRepairPlan& acRhs) const noexcept;

    uint64_t ReportId{};
    uint64_t PlanId{};
    PartyQuestCampaignId CampaignId;
    PartyQuestRepairPlan Plan;
    bool IsValid{true};
};

/** Canonical accepted transition broadcast to every connected campaign client. */
struct NotifyPartyQuestCanonicalUpdate final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyQuestCanonicalUpdate;

    NotifyPartyQuestCanonicalUpdate()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyQuestCanonicalUpdate& acRhs) const noexcept;

    uint64_t TransactionId{};
    uint64_t WorldRevision{};
    uint32_t InitiatorPlayerId{};
    QuestSnapshot CanonicalSnapshot;
    bool IsValid{true};
};
