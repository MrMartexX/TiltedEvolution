#include <Structs/Skyrim/PartyQuestNativeSaveRequest.h>

#include <cstdio>
#include <cstring>

namespace
{
[[nodiscard]] bool FormatId(
    std::array<char, 33>& aOutput,
    uint64_t aHigh,
    uint64_t aLow) noexcept
{
    const int written = std::snprintf(
        aOutput.data(),
        aOutput.size(),
        "%016llX%016llX",
        static_cast<unsigned long long>(aHigh),
        static_cast<unsigned long long>(aLow));
    return written == 32;
}
}

PartyQuestNativeSaveRequestEncodeResult
PartyQuestNativeSaveRequestEncoder::Encode(
    const PartyQuestAsyncSaveRequestIdentity& acIdentity) noexcept
{
    PartyQuestNativeSaveRequestEncodeResult result;
    if (!acIdentity.IsValid() || acIdentity.SaveName.size() >= 96u)
        return result;

    PartyQuestNativeIsolatedSaveRequest request{};
    request.AbiVersion = kAbiVersion;
    request.StructSize = sizeof(request);
    request.Identity.RuntimeGeneration = acIdentity.RuntimeGeneration;
    request.Identity.TransactionId = acIdentity.TransactionId;
    request.Identity.TargetWorldRevision = acIdentity.TargetWorldRevision;
    request.Identity.CaptureEpochId = acIdentity.CaptureEpochId;
    request.Identity.AttemptNonce = acIdentity.AttemptNonce;
    if (!FormatId(
            request.Identity.CampaignId,
            acIdentity.CampaignId.High,
            acIdentity.CampaignId.Low) ||
        !FormatId(
            request.Identity.PlayerProfileId,
            acIdentity.PlayerProfileId.High,
            acIdentity.PlayerProfileId.Low))
    {
        result.Status = PartyQuestNativeSaveRequestEncodeStatus::EncodingFailed;
        return result;
    }

    std::memcpy(
        request.Identity.SaveName.data(),
        acIdentity.SaveName.c_str(),
        acIdentity.SaveName.size() + 1u);
    const int pathLength = std::snprintf(
        request.RelativeSavePath.data(),
        request.RelativeSavePath.size(),
        "CoopCampaigns\\Campaign_%s\\Player_%s\\saves\\",
        request.Identity.CampaignId.data(),
        request.Identity.PlayerProfileId.data());
    if (pathLength <= 0 ||
        static_cast<size_t>(pathLength) >= request.RelativeSavePath.size())
    {
        result.Status = PartyQuestNativeSaveRequestEncodeStatus::EncodingFailed;
        return result;
    }

    result.Status = PartyQuestNativeSaveRequestEncodeStatus::Encoded;
    result.Request = request;
    return result;
}
