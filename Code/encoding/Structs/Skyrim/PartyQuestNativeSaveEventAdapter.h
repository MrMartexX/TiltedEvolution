#pragma once

#include <Structs/Skyrim/PartyQuestAsyncSaveContract.h>

#include <cstddef>
#include <cstdint>
#include <optional>

enum class PartyQuestNativeSaveEventPhase : uint8_t
{
    Closed,
    Published,
    Retired
};

enum class PartyQuestNativeSaveEventOutcome : uint8_t
{
    Succeeded,
    Failed
};

enum class PartyQuestNativeSaveEventDecodeStatus : uint8_t
{
    Decoded,
    NullBuffer,
    Truncated,
    TrailingBytes,
    UnsupportedVersion,
    InvalidStructSize,
    InvalidArtifact,
    InvalidPhase,
    InvalidOutcome,
    NonZeroReserved,
    UnterminatedCampaignId,
    InvalidCampaignId,
    UnterminatedPlayerProfileId,
    InvalidPlayerProfileId,
    UnterminatedSaveName,
    InvalidIdentity,
    InconsistentNativeError,
    TranslationFailure
};

struct PartyQuestNativeSaveEvent
{
    PartyQuestAsyncSaveRequestIdentity Identity;
    PartyQuestAsyncSaveArtifact Artifact{PartyQuestAsyncSaveArtifact::SkyrimEss};
    PartyQuestNativeSaveEventPhase Phase{PartyQuestNativeSaveEventPhase::Closed};
    PartyQuestNativeSaveEventOutcome Outcome{PartyQuestNativeSaveEventOutcome::Failed};
    uint32_t NativeError{};
};

struct PartyQuestNativeSaveEventDecodeResult
{
    PartyQuestNativeSaveEventDecodeStatus Status{PartyQuestNativeSaveEventDecodeStatus::NullBuffer};
    std::optional<PartyQuestNativeSaveEvent> Event;
};

struct PartyQuestNativeSaveRequestRetiredEvent
{
    PartyQuestAsyncSaveRequestIdentity Identity;
    PartyQuestNativeSaveEventOutcome Outcome{PartyQuestNativeSaveEventOutcome::Failed};
    uint32_t NativeError{};
};

struct PartyQuestNativeSaveRequestRetiredDecodeResult
{
    PartyQuestNativeSaveEventDecodeStatus Status{PartyQuestNativeSaveEventDecodeStatus::NullBuffer};
    std::optional<PartyQuestNativeSaveRequestRetiredEvent> Event;
};

enum class PartyQuestNativeSaveEventAdapterStatus : uint8_t
{
    Malformed,
    InvalidExpectedIdentity,
    IdentityMismatch,
    Pending,
    Complete,
    Duplicate,
    Stale,
    Failed,
    TimedOut,
    Cancelled,
    InvalidClock,
    RetirementUnproven,
    RetirementBeforeTerminal,
    Retired,
    ContractRejected
};

struct PartyQuestNativeSaveEventAdapterResult
{
    PartyQuestNativeSaveEventAdapterStatus Status{PartyQuestNativeSaveEventAdapterStatus::Malformed};
    PartyQuestNativeSaveEventDecodeStatus DecodeStatus{PartyQuestNativeSaveEventDecodeStatus::NullBuffer};
    std::optional<PartyQuestAsyncSaveContractResult> ContractResult;
};

/**
 * Portable decoder and correlation adapter for the Windows x64 little-endian
 * native save-event ABI v2. The decoder accepts an untrusted bounded byte
 * range and never retains a pointer into the callback payload. ABI v2 is
 * intentionally exact-size: truncated and trailing payloads are rejected.
 *
 * ApplyTrustedProviderEvent has a trusted-caller precondition. Its caller must
 * authenticate the registered provider and hold the serialized request's
 * generation/lifecycle lease across this call. This helper does not establish
 * source authenticity, callback lifetime, or physical queue/writer drain.
 * ArtifactEvent::Retired therefore remains unproven. A valid PQS4
 * RequestRetiredEvent is request-wide drain proof regardless of whether its
 * Outcome reports save success or failure, and is translated to
 * PartyQuestAsyncSaveContract::Retire.
 */
class PartyQuestNativeSaveEventAdapter final
{
public:
    static constexpr uint32_t kArtifactEventMessageId = 0x50515303;
    static constexpr uint32_t kRequestRetiredMessageId = 0x50515304;
    static constexpr uint32_t kAbiVersion = 2;
    static constexpr size_t kArtifactEventStructSize = 232;
    static constexpr size_t kRequestRetiredStructSize = 224;
    // Compatibility name for the original PQS3 decoder API.
    static constexpr size_t kStructSize = kArtifactEventStructSize;

    [[nodiscard]] static PartyQuestNativeSaveEventDecodeResult Decode(
        const void* apPayload,
        size_t aPayloadSize) noexcept;

    [[nodiscard]] static PartyQuestNativeSaveRequestRetiredDecodeResult DecodeRequestRetired(
        const void* apPayload,
        size_t aPayloadSize) noexcept;

    [[nodiscard]] static PartyQuestNativeSaveEventAdapterResult ApplyTrustedProviderEvent(
        const void* apPayload,
        size_t aPayloadSize,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract,
        uint64_t aNowMs) noexcept;

    [[nodiscard]] static PartyQuestNativeSaveEventAdapterResult ApplyTrustedProviderRetirement(
        const void* apPayload,
        size_t aPayloadSize,
        const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
        PartyQuestAsyncSaveContract& aContract) noexcept;
};
