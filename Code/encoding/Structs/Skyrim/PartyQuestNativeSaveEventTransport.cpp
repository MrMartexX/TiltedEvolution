#include <Structs/Skyrim/PartyQuestNativeSaveEventTransport.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
constexpr uint32_t kTransportAbiVersion = 1u;

[[nodiscard]] bool TailIsZero(
    const PartyQuestNativeSaveEventEnvelope& acEnvelope) noexcept
{
    return std::all_of(
        acEnvelope.Payload.begin() + acEnvelope.PayloadSize,
        acEnvelope.Payload.end(),
        [](uint8_t aValue) { return aValue == 0u; });
}
}

PartyQuestNativeSaveEventTransportResult
PartyQuestNativeSaveEventTransport::Consume(
    const void* apEnvelope,
    size_t aEnvelopeSize,
    const PartyQuestNativeSaveProviderRegistration& acProviderRegistration,
    const PartyQuestNativeSaveProviderToken& acProviderToken,
    uint64_t aRuntimeGeneration,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    PartyQuestAsyncSaveContract& aContract,
    PartyQuestAsyncSaveFinalizationGate& aGate,
    uint64_t aNowMs) noexcept
{
    PartyQuestNativeSaveEventTransportResult result;
    if (!apEnvelope)
        return result;
    if (aEnvelopeSize != sizeof(PartyQuestNativeSaveEventEnvelope))
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::InvalidEnvelopeSize;
        return result;
    }

    PartyQuestNativeSaveEventEnvelope envelope{};
    std::memcpy(&envelope, apEnvelope, sizeof(envelope));
    if (envelope.AbiVersion != kTransportAbiVersion)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::UnsupportedAbi;
        return result;
    }
    if (envelope.StructSize != sizeof(envelope))
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::InvalidStructSize;
        return result;
    }

    size_t expectedPayloadSize = 0u;
    if (envelope.MessageType ==
        PartyQuestNativeSaveEventAdapter::kArtifactEventMessageId)
    {
        expectedPayloadSize =
            PartyQuestNativeSaveEventAdapter::kArtifactEventStructSize;
    }
    else if (envelope.MessageType ==
        PartyQuestNativeSaveEventAdapter::kRequestRetiredMessageId)
    {
        expectedPayloadSize =
            PartyQuestNativeSaveEventAdapter::kRequestRetiredStructSize;
    }
    else
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::UnsupportedMessage;
        return result;
    }
    if (envelope.PayloadSize != expectedPayloadSize)
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::InvalidPayloadSize;
        return result;
    }
    if (!TailIsZero(envelope))
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::NonZeroPayloadTail;
        return result;
    }
    if (envelope.Sequence == 0u)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::InvalidSequence;
        return result;
    }
    if (m_exhausted)
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::SequenceExhausted;
        return result;
    }
    if (m_haveSequence && envelope.Sequence < m_nextSequence)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::Replay;
        return result;
    }
    if (m_haveSequence && envelope.Sequence > m_nextSequence)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::SequenceGap;
        return result;
    }

    m_haveSequence = true;
    if (envelope.Sequence == std::numeric_limits<uint64_t>::max())
        m_exhausted = true;
    else
        m_nextSequence = envelope.Sequence + 1u;

    if (envelope.MessageType ==
        PartyQuestNativeSaveEventAdapter::kArtifactEventMessageId)
    {
        result.Route = PartyQuestAsyncSaveEventRouter::ApplyArtifact(
            envelope.Payload.data(),
            envelope.PayloadSize,
            acProviderRegistration,
            acProviderToken,
            aRuntimeGeneration,
            acReservedIdentity,
            aContract,
            aGate,
            aNowMs);
    }
    else
    {
        result.Route = PartyQuestAsyncSaveEventRouter::ApplyRetirement(
            envelope.Payload.data(),
            envelope.PayloadSize,
            acProviderRegistration,
            acProviderToken,
            aRuntimeGeneration,
            acReservedIdentity,
            aContract,
            aGate);
    }

    if (result.Route.Status ==
        PartyQuestAsyncSaveEventRouteStatus::ProviderRejected)
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::ProviderRejected;
    }
    else if (result.Route.Status !=
        PartyQuestAsyncSaveEventRouteStatus::Applied)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::RouteRejected;
    }
    else
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::Applied;
    }
    return result;
}
