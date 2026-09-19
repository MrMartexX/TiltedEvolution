#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveFinalizationGate.h>
#include <Structs/Skyrim/PartyQuestNativeSaveEventAdapter.h>

#include <cstddef>
#include <cstdint>

enum class PartyQuestAsyncSaveEventRouteStatus : uint8_t
{
    Applied,
    Malformed,
    InvalidExpectedIdentity,
    IdentityMismatch,
    AdapterRejected
};

struct PartyQuestAsyncSaveEventRouteResult
{
    PartyQuestAsyncSaveEventRouteStatus Status{
        PartyQuestAsyncSaveEventRouteStatus::Malformed};
    PartyQuestNativeSaveEventDecodeStatus DecodeStatus{
        PartyQuestNativeSaveEventDecodeStatus::NullBuffer};
    PartyQuestAsyncSaveFinalizationResult Finalization;
};

/**
 * Pure serialized routing boundary for authenticated native save callbacks.
 * The caller must authenticate the provider and hold the matching lifecycle /
 * generation lease across each call. This class does not register callbacks,
 * own threads, establish durability, or authorize Skyrim mutation.
 */
class PartyQuestAsyncSaveEventRouter final
{
public:
    [[nodiscard]] static PartyQuestAsyncSaveEventRouteResult ApplyArtifact(
        const void* apPayload,
        size_t aPayloadSize,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] static PartyQuestAsyncSaveEventRouteResult ApplyRetirement(
        const void* apPayload,
        size_t aPayloadSize,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        PartyQuestAsyncSaveFinalizationGate& aGate) noexcept;
};
