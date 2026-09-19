#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveEventRouter.h>

#include <array>
#include <cstddef>
#include <cstdint>

struct PartyQuestNativeSaveEventEnvelope final
{
    uint32_t AbiVersion{};
    uint32_t StructSize{};
    uint32_t MessageType{};
    uint32_t PayloadSize{};
    uint64_t Sequence{};
    std::array<uint8_t, 232> Payload{};
};

static_assert(sizeof(PartyQuestNativeSaveEventEnvelope) == 256u);
static_assert(offsetof(PartyQuestNativeSaveEventEnvelope, Sequence) == 16u);
static_assert(offsetof(PartyQuestNativeSaveEventEnvelope, Payload) == 24u);

enum class PartyQuestNativeSaveEventTransportStatus : uint8_t
{
    Applied,
    NullEnvelope,
    InvalidEnvelopeSize,
    UnsupportedAbi,
    InvalidStructSize,
    UnsupportedMessage,
    InvalidPayloadSize,
    NonZeroPayloadTail,
    InvalidSequence,
    Replay,
    SequenceGap,
    SequenceExhausted,
    ProviderRejected,
    RouteRejected
};

struct PartyQuestNativeSaveEventTransportResult final
{
    PartyQuestNativeSaveEventTransportStatus Status{
        PartyQuestNativeSaveEventTransportStatus::NullEnvelope};
    PartyQuestAsyncSaveEventRouteResult Route;
};

/**
 * Pure, single-consumer transport state for one authenticated provider
 * registration. The first sequence must be nonzero; every later accepted
 * envelope must be contiguous. Readiness and transport validity never grant mutation authority;
 * the router still revalidates provider generation and request identity.
 *
 * The caller owns serialization and must keep the provider token's generation
 * lease across dequeue and this call. A dequeued, structurally valid envelope
 * consumes its sequence even when payload routing rejects it: the native queue
 * has already retired that item, so retrying it would invent replay semantics.
 */
class PartyQuestNativeSaveEventTransport final
{
public:
    [[nodiscard]] PartyQuestNativeSaveEventTransportResult Consume(
        const void* apEnvelope,
        size_t aEnvelopeSize,
        const PartyQuestNativeSaveProviderRegistration& acProviderRegistration,
        const PartyQuestNativeSaveProviderToken& acProviderToken,
        uint64_t aRuntimeGeneration,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] uint64_t GetNextSequence() const noexcept
    {
        return m_nextSequence;
    }

private:
    uint64_t m_nextSequence{};
    bool m_haveSequence{};
    bool m_exhausted{};
};
