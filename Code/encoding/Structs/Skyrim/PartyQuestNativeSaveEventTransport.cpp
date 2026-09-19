#include <Structs/Skyrim/PartyQuestNativeSaveEventTransport.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

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
    const auto reject = [&](PartyQuestNativeSaveEventTransportStatus aStatus) {
        m_poisoned = true;
        result.Status = aStatus;
        return std::move(result);
    };
    if (m_poisoned)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::Poisoned;
        return result;
    }
    if (!apEnvelope)
        return reject(PartyQuestNativeSaveEventTransportStatus::NullEnvelope);
    if (aEnvelopeSize != sizeof(PartyQuestNativeSaveEventEnvelope))
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::InvalidEnvelopeSize;
        return reject(result.Status);
    }

    PartyQuestNativeSaveEventEnvelope envelope{};
    std::memcpy(&envelope, apEnvelope, sizeof(envelope));
    if (envelope.AbiVersion != kTransportAbiVersion)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::UnsupportedAbi;
        return reject(result.Status);
    }
    if (envelope.StructSize != sizeof(envelope))
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::InvalidStructSize;
        return reject(result.Status);
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
        return reject(result.Status);
    }
    if (envelope.PayloadSize != expectedPayloadSize)
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::InvalidPayloadSize;
        return reject(result.Status);
    }
    if (!TailIsZero(envelope))
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::NonZeroPayloadTail;
        return reject(result.Status);
    }
    if (m_nextSequence == 0u && envelope.Sequence != 1u)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::InvalidSequence;
        return reject(result.Status);
    }
    if (m_exhausted)
    {
        result.Status =
            PartyQuestNativeSaveEventTransportStatus::SequenceExhausted;
        return reject(result.Status);
    }
    if (m_nextSequence != 0u && envelope.Sequence < m_nextSequence)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::Replay;
        return reject(result.Status);
    }
    if (m_nextSequence != 0u && envelope.Sequence > m_nextSequence)
    {
        result.Status = PartyQuestNativeSaveEventTransportStatus::SequenceGap;
        return reject(result.Status);
    }

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
    if (result.Status != PartyQuestNativeSaveEventTransportStatus::Applied)
        m_poisoned = true;
    return result;
}
