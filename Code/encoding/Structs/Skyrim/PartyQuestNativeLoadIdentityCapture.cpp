#include <Structs/Skyrim/PartyQuestNativeLoadIdentityCapture.h>

#include <cstring>

PartyQuestNativeLoadIdentityCaptureResult
PartyQuestNativeLoadIdentityCapture::Capture(
    const char* apInput) noexcept
{
    PartyQuestNativeLoadIdentityCaptureResult result{};

    if (!apInput)
    {
        result.Status = PartyQuestNativeLoadIdentityCaptureStatus::NullInput;
        return result;
    }

    const auto* bytes =
        reinterpret_cast<const unsigned char*>(apInput);

    for (uint16_t index = 0u; index < kProbeBytes; ++index)
    {
        if (bytes[index] != 0u)
            continue;

        if (index == 0u)
        {
            result.Status = PartyQuestNativeLoadIdentityCaptureStatus::Empty;
            return result;
        }

        result.Status =
            PartyQuestNativeLoadIdentityCaptureStatus::Captured;
        result.Identity.Length = index;
        std::memcpy(
            result.Identity.Bytes,
            apInput,
            static_cast<size_t>(index));
        return result;
    }

    result.Status =
        PartyQuestNativeLoadIdentityCaptureStatus::
            TerminatorNotFoundWithinBound;
    return result;
}
