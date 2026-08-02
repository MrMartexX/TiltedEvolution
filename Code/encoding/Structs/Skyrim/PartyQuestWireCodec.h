#pragma once

#include <Structs/Skyrim/PartyQuestRepair.h>

#include <TiltedCore/Buffer.hpp>

namespace PartyQuestWireCodec
{
constexpr uint16_t ProtocolVersion = 3;

void SerializeQuestSnapshot(TiltedPhoques::Buffer::Writer& aWriter, const QuestSnapshot& acSnapshot) noexcept;
[[nodiscard]] bool DeserializeQuestSnapshot(TiltedPhoques::Buffer::Reader& aReader, QuestSnapshot& aSnapshot) noexcept;

void SerializeTransaction(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestTransaction& acTransaction) noexcept;
[[nodiscard]] bool DeserializeTransaction(TiltedPhoques::Buffer::Reader& aReader, PartyQuestTransaction& aTransaction) noexcept;

void SerializeApplyResult(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestApplyResult& acResult) noexcept;
[[nodiscard]] bool DeserializeApplyResult(TiltedPhoques::Buffer::Reader& aReader, PartyQuestApplyResult& aResult) noexcept;

void SerializeReplicaReport(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestReplicaReport& acReport) noexcept;
[[nodiscard]] bool DeserializeReplicaReport(TiltedPhoques::Buffer::Reader& aReader, PartyQuestReplicaReport& aReport) noexcept;

void SerializeRepairPlan(TiltedPhoques::Buffer::Writer& aWriter, const PartyQuestRepairPlan& acPlan) noexcept;
[[nodiscard]] bool DeserializeRepairPlan(TiltedPhoques::Buffer::Reader& aReader, PartyQuestRepairPlan& aPlan) noexcept;
} // namespace PartyQuestWireCodec
