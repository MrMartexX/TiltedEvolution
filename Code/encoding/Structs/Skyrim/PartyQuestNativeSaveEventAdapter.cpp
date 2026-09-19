#include <Structs/Skyrim/PartyQuestNativeSaveEventAdapter.h>

#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr size_t kVersionOffset = 0;
constexpr size_t kSizeOffset = 4;
constexpr size_t kRuntimeGenerationOffset = 8;
constexpr size_t kTransactionIdOffset = 16;
constexpr size_t kTargetWorldRevisionOffset = 24;
constexpr size_t kCaptureEpochIdOffset = 32;
constexpr size_t kAttemptNonceOffset = 40;
constexpr size_t kCampaignIdOffset = 48;
constexpr size_t kCampaignIdSize = 33;
constexpr size_t kPlayerProfileIdOffset = 81;
constexpr size_t kPlayerProfileIdSize = 33;
constexpr size_t kSaveNameOffset = 114;
constexpr size_t kSaveNameSize = 96;
constexpr size_t kIdentityReservedOffset = 210;
constexpr size_t kIdentityReservedSize = 6;
constexpr size_t kArtifactOffset = 216;
constexpr size_t kPhaseOffset = 217;
constexpr size_t kOutcomeOffset = 218;
constexpr size_t kEventReservedOffset = 219;
constexpr size_t kEventReservedSize = 5;
constexpr size_t kNativeErrorOffset = 224;
constexpr size_t kReservedTailOffset = 228;
constexpr size_t kReservedTailSize = 4;
constexpr size_t kRetiredOutcomeOffset = 216;
constexpr size_t kRetiredReservedOffset = 217;
constexpr size_t kRetiredReservedSize = 3;
constexpr size_t kRetiredNativeErrorOffset = 220;

uint32_t ReadU32(const uint8_t* apBytes, size_t aOffset) noexcept
{
    uint32_t value{};
    for (size_t index = 0; index < sizeof(value); ++index)
        value |= static_cast<uint32_t>(apBytes[aOffset + index]) << (index * 8);
    return value;
}

uint64_t ReadU64(const uint8_t* apBytes, size_t aOffset) noexcept
{
    uint64_t value{};
    for (size_t index = 0; index < sizeof(value); ++index)
        value |= static_cast<uint64_t>(apBytes[aOffset + index]) << (index * 8);
    return value;
}

bool IsZeroRange(const uint8_t* apBytes, size_t aOffset, size_t aSize) noexcept
{
    for (size_t index = 0; index < aSize; ++index)
    {
        if (apBytes[aOffset + index] != 0)
            return false;
    }
    return true;
}

std::optional<std::string_view> BoundedString(
    const uint8_t* apBytes,
    size_t aOffset,
    size_t aSize) noexcept
{
    for (size_t index = 0; index < aSize; ++index)
    {
        if (apBytes[aOffset + index] == 0)
        {
            return std::string_view(
                reinterpret_cast<const char*>(apBytes + aOffset), index);
        }
    }
    return std::nullopt;
}

bool ParseCanonicalId(
    std::string_view aText,
    uint64_t& aHigh,
    uint64_t& aLow) noexcept
{
    if (aText.size() != 32)
        return false;

    uint64_t words[2]{};
    for (size_t index = 0; index < aText.size(); ++index)
    {
        const char character = aText[index];
        uint8_t nibble{};
        if (character >= '0' && character <= '9')
            nibble = static_cast<uint8_t>(character - '0');
        else if (character >= 'A' && character <= 'F')
            nibble = static_cast<uint8_t>(character - 'A' + 10);
        else
            return false;

        uint64_t& word = words[index / 16];
        word = (word << 4) | nibble;
    }

    aHigh = words[0];
    aLow = words[1];
    return aHigh != 0 || aLow != 0;
}

bool DecodeIdentity(
    const uint8_t* apBytes,
    PartyQuestAsyncSaveRequestIdentity& aIdentity,
    PartyQuestNativeSaveEventDecodeStatus& aStatus) noexcept
{
    const auto campaignText = BoundedString(apBytes, kCampaignIdOffset, kCampaignIdSize);
    if (!campaignText)
    {
        aStatus = PartyQuestNativeSaveEventDecodeStatus::UnterminatedCampaignId;
        return false;
    }
    const auto profileText = BoundedString(apBytes, kPlayerProfileIdOffset, kPlayerProfileIdSize);
    if (!profileText)
    {
        aStatus = PartyQuestNativeSaveEventDecodeStatus::UnterminatedPlayerProfileId;
        return false;
    }
    const auto saveName = BoundedString(apBytes, kSaveNameOffset, kSaveNameSize);
    if (!saveName)
    {
        aStatus = PartyQuestNativeSaveEventDecodeStatus::UnterminatedSaveName;
        return false;
    }
    if (!ParseCanonicalId(
            *campaignText,
            aIdentity.CampaignId.High,
            aIdentity.CampaignId.Low))
    {
        aStatus = PartyQuestNativeSaveEventDecodeStatus::InvalidCampaignId;
        return false;
    }
    if (!ParseCanonicalId(
            *profileText,
            aIdentity.PlayerProfileId.High,
            aIdentity.PlayerProfileId.Low))
    {
        aStatus = PartyQuestNativeSaveEventDecodeStatus::InvalidPlayerProfileId;
        return false;
    }

    aIdentity.RuntimeGeneration = ReadU64(apBytes, kRuntimeGenerationOffset);
    aIdentity.TransactionId = ReadU64(apBytes, kTransactionIdOffset);
    aIdentity.TargetWorldRevision = ReadU64(apBytes, kTargetWorldRevisionOffset);
    aIdentity.CaptureEpochId = ReadU64(apBytes, kCaptureEpochIdOffset);
    aIdentity.AttemptNonce = ReadU64(apBytes, kAttemptNonceOffset);

    try
    {
        aIdentity.SaveName.assign(saveName->data(), saveName->size());
        if (!aIdentity.IsValid())
        {
            aStatus = PartyQuestNativeSaveEventDecodeStatus::InvalidIdentity;
            return false;
        }
        return true;
    }
    catch (...)
    {
        aIdentity = {};
        aStatus = PartyQuestNativeSaveEventDecodeStatus::TranslationFailure;
        return false;
    }
}

PartyQuestNativeSaveEventAdapterStatus MapContractStatus(
    PartyQuestAsyncSaveContractStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case PartyQuestAsyncSaveContractStatus::Pending:
        return PartyQuestNativeSaveEventAdapterStatus::Pending;
    case PartyQuestAsyncSaveContractStatus::Complete:
        return PartyQuestNativeSaveEventAdapterStatus::Complete;
    case PartyQuestAsyncSaveContractStatus::Duplicate:
        return PartyQuestNativeSaveEventAdapterStatus::Duplicate;
    case PartyQuestAsyncSaveContractStatus::Stale:
        return PartyQuestNativeSaveEventAdapterStatus::Stale;
    case PartyQuestAsyncSaveContractStatus::Failed:
        return PartyQuestNativeSaveEventAdapterStatus::Failed;
    case PartyQuestAsyncSaveContractStatus::TimedOut:
        return PartyQuestNativeSaveEventAdapterStatus::TimedOut;
    case PartyQuestAsyncSaveContractStatus::Cancelled:
        return PartyQuestNativeSaveEventAdapterStatus::Cancelled;
    case PartyQuestAsyncSaveContractStatus::InvalidClock:
        return PartyQuestNativeSaveEventAdapterStatus::InvalidClock;
    default:
        return PartyQuestNativeSaveEventAdapterStatus::ContractRejected;
    }
}
} // namespace

PartyQuestNativeSaveEventDecodeResult PartyQuestNativeSaveEventAdapter::Decode(
    const void* apPayload,
    size_t aPayloadSize) noexcept
{
    PartyQuestNativeSaveEventDecodeResult result;
    if (!apPayload)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::NullBuffer;
        return result;
    }
    if (aPayloadSize < kStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::Truncated;
        return result;
    }
    if (aPayloadSize > kStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::TrailingBytes;
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(apPayload);
    if (ReadU32(bytes, kVersionOffset) != kAbiVersion)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::UnsupportedVersion;
        return result;
    }
    if (ReadU32(bytes, kSizeOffset) != kStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidStructSize;
        return result;
    }

    const uint8_t artifact = bytes[kArtifactOffset];
    if (artifact != 1 && artifact != 2)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidArtifact;
        return result;
    }
    const uint8_t phase = bytes[kPhaseOffset];
    if (phase < 1 || phase > 3)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidPhase;
        return result;
    }
    const uint8_t outcome = bytes[kOutcomeOffset];
    if (outcome != 1 && outcome != 2)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidOutcome;
        return result;
    }
    if (!IsZeroRange(bytes, kIdentityReservedOffset, kIdentityReservedSize) ||
        !IsZeroRange(bytes, kEventReservedOffset, kEventReservedSize) ||
        !IsZeroRange(bytes, kReservedTailOffset, kReservedTailSize))
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved;
        return result;
    }

    PartyQuestNativeSaveEvent event;
    if (!DecodeIdentity(bytes, event.Identity, result.Status))
        return result;
    event.Artifact = artifact == 1
        ? PartyQuestAsyncSaveArtifact::SkyrimEss
        : PartyQuestAsyncSaveArtifact::SkseCosave;
    event.Phase = static_cast<PartyQuestNativeSaveEventPhase>(phase - 1);
    event.Outcome = static_cast<PartyQuestNativeSaveEventOutcome>(outcome - 1);
    event.NativeError = ReadU32(bytes, kNativeErrorOffset);

    if (event.Outcome == PartyQuestNativeSaveEventOutcome::Succeeded &&
        event.NativeError != 0)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InconsistentNativeError;
        return result;
    }

    result.Event.emplace(std::move(event));
    result.Status = PartyQuestNativeSaveEventDecodeStatus::Decoded;
    return result;
}

PartyQuestNativeSaveRequestRetiredDecodeResult
PartyQuestNativeSaveEventAdapter::DecodeRequestRetired(
    const void* apPayload,
    size_t aPayloadSize) noexcept
{
    PartyQuestNativeSaveRequestRetiredDecodeResult result;
    if (!apPayload)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::NullBuffer;
        return result;
    }
    if (aPayloadSize < kRequestRetiredStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::Truncated;
        return result;
    }
    if (aPayloadSize > kRequestRetiredStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::TrailingBytes;
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(apPayload);
    if (ReadU32(bytes, kVersionOffset) != kAbiVersion)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::UnsupportedVersion;
        return result;
    }
    if (ReadU32(bytes, kSizeOffset) != kRequestRetiredStructSize)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidStructSize;
        return result;
    }

    const uint8_t outcome = bytes[kRetiredOutcomeOffset];
    if (outcome != 1 && outcome != 2)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InvalidOutcome;
        return result;
    }
    if (!IsZeroRange(bytes, kIdentityReservedOffset, kIdentityReservedSize) ||
        !IsZeroRange(bytes, kRetiredReservedOffset, kRetiredReservedSize))
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::NonZeroReserved;
        return result;
    }

    PartyQuestNativeSaveRequestRetiredEvent event;
    if (!DecodeIdentity(bytes, event.Identity, result.Status))
        return result;
    event.Outcome = static_cast<PartyQuestNativeSaveEventOutcome>(outcome - 1);
    event.NativeError = ReadU32(bytes, kRetiredNativeErrorOffset);
    if (event.Outcome == PartyQuestNativeSaveEventOutcome::Succeeded &&
        event.NativeError != 0)
    {
        result.Status = PartyQuestNativeSaveEventDecodeStatus::InconsistentNativeError;
        return result;
    }

    result.Event.emplace(std::move(event));
    result.Status = PartyQuestNativeSaveEventDecodeStatus::Decoded;
    return result;
}

PartyQuestNativeSaveEventAdapterResult
PartyQuestNativeSaveEventAdapter::ApplyTrustedProviderEvent(
    const void* apPayload,
    size_t aPayloadSize,
    const PartyQuestAsyncSaveRequestIdentity& acReservedIdentity,
    PartyQuestAsyncSaveContract& aContract,
    uint64_t aNowMs) noexcept
{
    PartyQuestNativeSaveEventAdapterResult result;
    const auto decoded = Decode(apPayload, aPayloadSize);
    result.DecodeStatus = decoded.Status;
    if (decoded.Status != PartyQuestNativeSaveEventDecodeStatus::Decoded ||
        !decoded.Event)
    {
        result.Status = PartyQuestNativeSaveEventAdapterStatus::Malformed;
        return result;
    }
    if (!acReservedIdentity.IsValid())
    {
        result.Status = PartyQuestNativeSaveEventAdapterStatus::InvalidExpectedIdentity;
        return result;
    }
    if (decoded.Event->Identity != acReservedIdentity)
    {
        result.Status = PartyQuestNativeSaveEventAdapterStatus::IdentityMismatch;
        return result;
    }
    if (decoded.Event->Phase == PartyQuestNativeSaveEventPhase::Retired)
    {
        // An artifact-scoped event cannot prove that the whole request has
        // left every queue, callback and writer. Only the separate request-wide
        // PQS4 drain event may call contract.Retire(identity).
        result.Status = PartyQuestNativeSaveEventAdapterStatus::RetirementUnproven;
        return result;
    }

    PartyQuestAsyncSaveContractResult contractResult;
    if (decoded.Event->Phase == PartyQuestNativeSaveEventPhase::Closed)
    {
        contractResult = aContract.Observe(
            decoded.Event->Identity,
            decoded.Event->Artifact,
            decoded.Event->Outcome == PartyQuestNativeSaveEventOutcome::Succeeded
                ? PartyQuestAsyncSaveArtifactOutcome::ClosedSuccess
                : PartyQuestAsyncSaveArtifactOutcome::Failed,
            aNowMs);
    }
    else
    {
        contractResult = aContract.ObservePublication(
            decoded.Event->Identity,
            decoded.Event->Artifact,
            decoded.Event->Outcome == PartyQuestNativeSaveEventOutcome::Succeeded
                ? PartyQuestAsyncSavePublicationOutcome::PublishedSuccess
                : PartyQuestAsyncSavePublicationOutcome::Failed,
            aNowMs);
    }

    result.Status = MapContractStatus(contractResult.Status);
    result.ContractResult.emplace(std::move(contractResult));
    return result;
}
