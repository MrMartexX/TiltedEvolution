#pragma once

#include <Structs/Skyrim/PartyQuestNativeLoadRequest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum class PartyQuestNativeLoadIdentityCaptureStatus : uint8_t
{
    Captured = 1u,
    NullInput = 2u,
    Empty = 3u,
    TerminatorNotFoundWithinBound = 4u
};

struct PartyQuestNativeLoadIdentityCaptureResult final
{
    PartyQuestNativeLoadIdentityCaptureStatus Status{
        PartyQuestNativeLoadIdentityCaptureStatus::NullInput};
    uint8_t Reserved{};
    PartyQuestNativeLoadIdentity Identity;
};

/**
 * Pure bounded capture of a caller-supplied C string into
 * PartyQuestNativeLoadIdentity.
 *
 * The helper performs no normalization. It preserves exactly the bytes before
 * the first NUL and does not inspect filesystem syntax, locale, case, slash
 * direction, extensions, basenames, dots, spaces or UTF-8 structure.
 *
 * Valid lengths are 1..PartyQuestNativeLoadIdentity::kCapacity inclusive.
 * The bounded probe reads at most kProbeBytes bytes: indices 0..260. A NUL at
 * index 260 is therefore a valid 260-byte identity. If no NUL is present in
 * that window, capture fails without truncation and without reading index 261.
 *
 * Precondition: apInput is either null or points to memory readable for every
 * byte the bounded probe may inspect (through the first NUL, or all
 * kProbeBytes bytes when no NUL is found). Foreign-pointer faults are outside
 * this helper and must be contained by the future caller's native/SEH boundary.
 *
 * Failure results keep Identity fully zeroed. Success results are also
 * zero-initialized before copying, so every byte after Identity.Length remains
 * zero. This helper allocates nothing, performs no I/O and throws nothing.
 */
class PartyQuestNativeLoadIdentityCapture final
{
public:
    static constexpr uint16_t kProbeBytes =
        PartyQuestNativeLoadIdentity::kCapacity + 1u;

    [[nodiscard]] static PartyQuestNativeLoadIdentityCaptureResult Capture(
        const char* apInput) noexcept;
};

static_assert(PartyQuestNativeLoadIdentityCapture::kProbeBytes == 261u);
static_assert(sizeof(PartyQuestNativeLoadIdentityCaptureStatus) == 1u);
static_assert(sizeof(PartyQuestNativeLoadIdentityCaptureResult) == 264u);
static_assert(alignof(PartyQuestNativeLoadIdentityCaptureResult) == 2u);
static_assert(offsetof(
    PartyQuestNativeLoadIdentityCaptureResult, Status) == 0u);
static_assert(offsetof(
    PartyQuestNativeLoadIdentityCaptureResult, Reserved) == 1u);
static_assert(offsetof(
    PartyQuestNativeLoadIdentityCaptureResult, Identity) == 2u);
static_assert(std::is_standard_layout_v<
    PartyQuestNativeLoadIdentityCaptureResult>);
static_assert(std::is_trivially_copyable_v<
    PartyQuestNativeLoadIdentityCaptureResult>);
