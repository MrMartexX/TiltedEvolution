#include <Structs/Skyrim/PartyQuestNativeLoadIdentityCapture.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace
{
using Capture = PartyQuestNativeLoadIdentityCapture;
using Result = PartyQuestNativeLoadIdentityCaptureResult;
using Status = PartyQuestNativeLoadIdentityCaptureStatus;
using Identity = PartyQuestNativeLoadIdentity;

void RequireFailureZeroed(const Result& acResult)
{
    REQUIRE(acResult.Status != Status::Captured);
    REQUIRE(acResult.Reserved == 0u);
    REQUIRE(acResult.Identity.Length == 0u);
    for (const auto value : acResult.Identity.Bytes)
        REQUIRE(static_cast<unsigned char>(value) == 0u);
}

void RequireExact(
    const Result& acResult,
    const unsigned char* apExpected,
    uint16_t aLength)
{
    REQUIRE(acResult.Status == Status::Captured);
    REQUIRE(acResult.Reserved == 0u);
    REQUIRE(acResult.Identity.Length == aLength);
    REQUIRE(std::memcmp(
        acResult.Identity.Bytes,
        apExpected,
        aLength) == 0);
}

bool SameResult(const Result& acLeft, const Result& acRight) noexcept
{
    return std::memcmp(&acLeft, &acRight, sizeof(Result)) == 0;
}
}

TEST_CASE("Native load identity capture rejects null and zeroes output",
          "[quest.party-state][native-load-identity-capture]")
{
    const auto result = Capture::Capture(nullptr);
    REQUIRE(result.Status == Status::NullInput);
    RequireFailureZeroed(result);
}

TEST_CASE("Native load identity capture rejects empty string and zeroes output",
          "[quest.party-state][native-load-identity-capture]")
{
    const char input[] = {'\0'};
    const auto result = Capture::Capture(input);
    REQUIRE(result.Status == Status::Empty);
    RequireFailureZeroed(result);
}

TEST_CASE("Native load identity capture accepts one byte exactly",
          "[quest.party-state][native-load-identity-capture]")
{
    const char input[] = {'X', '\0'};
    const auto result = Capture::Capture(input);
    const unsigned char expected[] = {'X'};
    RequireExact(result, expected, 1u);

    for (size_t index = 1u; index < Identity::kCapacity; ++index)
        REQUIRE(static_cast<unsigned char>(result.Identity.Bytes[index]) == 0u);
}

TEST_CASE("Native load identity capture accepts length 259 exactly",
          "[quest.party-state][native-load-identity-capture][boundary]")
{
    std::array<char, 260> input{};
    std::fill_n(input.begin(), 259u, 'A');
    input[259] = '\0';

    const auto result = Capture::Capture(input.data());
    REQUIRE(result.Status == Status::Captured);
    REQUIRE(result.Identity.Length == 259u);
    REQUIRE(std::memcmp(result.Identity.Bytes, input.data(), 259u) == 0);
    REQUIRE(static_cast<unsigned char>(result.Identity.Bytes[259]) == 0u);
}

TEST_CASE("Native load identity capture accepts length 260 with final bounded probe",
          "[quest.party-state][native-load-identity-capture][boundary]")
{
    std::array<char, 261> input{};
    std::fill_n(input.begin(), 260u, 'B');
    input[260] = '\0';

    const auto result = Capture::Capture(input.data());
    REQUIRE(result.Status == Status::Captured);
    REQUIRE(result.Identity.Length == Identity::kCapacity);
    REQUIRE(std::memcmp(
        result.Identity.Bytes,
        input.data(),
        Identity::kCapacity) == 0);
}

TEST_CASE("Native load identity capture rejects NUL first appearing at index 261",
          "[quest.party-state][native-load-identity-capture][boundary]")
{
    std::array<char, 262> input{};
    std::fill_n(input.begin(), 261u, 'C');
    input[261] = '\0';

    const auto result = Capture::Capture(input.data());
    REQUIRE(result.Status == Status::TerminatorNotFoundWithinBound);
    RequireFailureZeroed(result);
}

TEST_CASE("Native load identity capture rejects unterminated 261-byte bounded window",
          "[quest.party-state][native-load-identity-capture][boundary]")
{
    std::array<char, Capture::kProbeBytes> input{};
    std::fill(input.begin(), input.end(), 'D');

    const auto result = Capture::Capture(input.data());
    REQUIRE(result.Status == Status::TerminatorNotFoundWithinBound);
    RequireFailureZeroed(result);
}

TEST_CASE("Native load identity capture stops at first embedded NUL",
          "[quest.party-state][native-load-identity-capture]")
{
    const char input[] = {'a', 'b', '\0', 'c', 'd', '\0'};
    const auto result = Capture::Capture(input);
    const unsigned char expected[] = {'a', 'b'};
    RequireExact(result, expected, 2u);

    REQUIRE(static_cast<unsigned char>(result.Identity.Bytes[2]) == 0u);
    REQUIRE(static_cast<unsigned char>(result.Identity.Bytes[3]) == 0u);
}

TEST_CASE("Native load identity capture preserves high bytes independently of signed char",
          "[quest.party-state][native-load-identity-capture][bytes]")
{
    const char input[] = {
        static_cast<char>(0x01u),
        static_cast<char>(0x7Fu),
        static_cast<char>(0x80u),
        static_cast<char>(0xFEu),
        static_cast<char>(0xFFu),
        '\0'};
    const unsigned char expected[] = {
        0x01u, 0x7Fu, 0x80u, 0xFEu, 0xFFu};

    const auto result = Capture::Capture(input);
    RequireExact(result, expected, 5u);

    const auto* actual = reinterpret_cast<const unsigned char*>(
        result.Identity.Bytes);
    for (size_t index = 0u; index < 5u; ++index)
        REQUIRE(actual[index] == expected[index]);
}

TEST_CASE("Native load identity capture preserves extension case exactly",
          "[quest.party-state][native-load-identity-capture][no-transform]")
{
    const char lower[] = "SaveName.ess";
    const char upper[] = "SaveName.ESS";

    const auto capturedLower = Capture::Capture(lower);
    const auto capturedUpper = Capture::Capture(upper);

    REQUIRE(capturedLower.Status == Status::Captured);
    REQUIRE(capturedUpper.Status == Status::Captured);
    REQUIRE(capturedLower.Identity.Length == 12u);
    REQUIRE(capturedUpper.Identity.Length == 12u);
    REQUIRE(std::memcmp(
        capturedLower.Identity.Bytes,
        lower,
        capturedLower.Identity.Length) == 0);
    REQUIRE(std::memcmp(
        capturedUpper.Identity.Bytes,
        upper,
        capturedUpper.Identity.Length) == 0);
    REQUIRE(std::memcmp(
        capturedLower.Identity.Bytes,
        capturedUpper.Identity.Bytes,
        capturedLower.Identity.Length) != 0);
}

TEST_CASE("Native load identity capture preserves slash and backslash exactly",
          "[quest.party-state][native-load-identity-capture][no-transform]")
{
    const char slash[] = "dir/sub/save.ess";
    const char backslash[] = "dir\\sub\\save.ess";

    const auto capturedSlash = Capture::Capture(slash);
    const auto capturedBackslash = Capture::Capture(backslash);

    REQUIRE(capturedSlash.Identity.Length == sizeof(slash) - 1u);
    REQUIRE(capturedBackslash.Identity.Length == sizeof(backslash) - 1u);
    REQUIRE(std::memcmp(
        capturedSlash.Identity.Bytes,
        slash,
        sizeof(slash) - 1u) == 0);
    REQUIRE(std::memcmp(
        capturedBackslash.Identity.Bytes,
        backslash,
        sizeof(backslash) - 1u) == 0);
}

TEST_CASE("Native load identity capture preserves path dots and spaces exactly",
          "[quest.party-state][native-load-identity-capture][no-transform]")
{
    const char input[] = "../Saves/My Slot.v1.ess";
    const auto result = Capture::Capture(input);

    REQUIRE(result.Status == Status::Captured);
    REQUIRE(result.Identity.Length == sizeof(input) - 1u);
    REQUIRE(std::memcmp(
        result.Identity.Bytes,
        input,
        sizeof(input) - 1u) == 0);
}

TEST_CASE("Native load identity capture preserves UTF-8 bytes without interpretation",
          "[quest.party-state][native-load-identity-capture][no-transform]")
{
    const char input[] = {
        'S', 'a', 'v', 'e', '-',
        static_cast<char>(0xC4u), static_cast<char>(0x80u),
        '-', static_cast<char>(0xE2u), static_cast<char>(0x82u),
        static_cast<char>(0xACu),
        '.', 'e', 's', 's', '\0'};
    const unsigned char expected[] = {
        'S', 'a', 'v', 'e', '-',
        0xC4u, 0x80u, '-', 0xE2u, 0x82u, 0xACu,
        '.', 'e', 's', 's'};

    const auto result = Capture::Capture(input);
    RequireExact(
        result,
        expected,
        static_cast<uint16_t>(sizeof(expected)));
}

TEST_CASE("Native load identity capture preserves first middle and last byte mismatches",
          "[quest.party-state][native-load-identity-capture][bytes]")
{
    const char exact[] = "abcdef";
    const char first[] = "xbcdef";
    const char middle[] = "abcxef";
    const char last[] = "abcdex";

    const auto base = Capture::Capture(exact);
    const auto changedFirst = Capture::Capture(first);
    const auto changedMiddle = Capture::Capture(middle);
    const auto changedLast = Capture::Capture(last);

    REQUIRE(base.Status == Status::Captured);
    REQUIRE(base.Identity.Length == 6u);

    for (const auto* changed : {
             &changedFirst,
             &changedMiddle,
             &changedLast})
    {
        REQUIRE(changed->Status == Status::Captured);
        REQUIRE(changed->Identity.Length == base.Identity.Length);
        REQUIRE(std::memcmp(
            changed->Identity.Bytes,
            base.Identity.Bytes,
            base.Identity.Length) != 0);
    }

    REQUIRE(static_cast<unsigned char>(changedFirst.Identity.Bytes[0]) ==
        static_cast<unsigned char>('x'));
    REQUIRE(static_cast<unsigned char>(changedMiddle.Identity.Bytes[3]) ==
        static_cast<unsigned char>('x'));
    REQUIRE(static_cast<unsigned char>(changedLast.Identity.Bytes[5]) ==
        static_cast<unsigned char>('x'));
}

TEST_CASE("Native load identity capture is byte deterministic across repetitions",
          "[quest.party-state][native-load-identity-capture][determinism]")
{
    const char input[] = "Folder\\Exact Name.ESS";

    const auto first = Capture::Capture(input);
    const auto second = Capture::Capture(input);
    const auto third = Capture::Capture(input);

    REQUIRE(SameResult(first, second));
    REQUIRE(SameResult(second, third));
}

TEST_CASE("Native load identity capture zeroes entire identity tail",
          "[quest.party-state][native-load-identity-capture]")
{
    const char input[] = "tail";
    const auto result = Capture::Capture(input);

    REQUIRE(result.Status == Status::Captured);
    REQUIRE(result.Identity.Length == 4u);
    for (size_t index = result.Identity.Length;
         index < Identity::kCapacity;
         ++index)
    {
        REQUIRE(static_cast<unsigned char>(
            result.Identity.Bytes[index]) == 0u);
    }
}

TEST_CASE("Native load identity capture performs no shared normalization transforms",
          "[quest.party-state][native-load-identity-capture][no-transform]")
{
    const char input[] = {
        '.', '.', '/', 'A', ' ', 'B', '\\',
        'M', 'i', 'X', 'e', 'D', '.', 'E', 'S', 'S', '\0'};

    const auto result = Capture::Capture(input);
    REQUIRE(result.Status == Status::Captured);
    REQUIRE(result.Identity.Length == sizeof(input) - 1u);
    REQUIRE(std::memcmp(
        result.Identity.Bytes,
        input,
        sizeof(input) - 1u) == 0);
}

TEST_CASE("Native load identity capture result layout and noexcept surface are fixed",
          "[quest.party-state][native-load-identity-capture][abi]")
{
    STATIC_REQUIRE(Capture::kProbeBytes == 261u);
    STATIC_REQUIRE(sizeof(Status) == 1u);
    STATIC_REQUIRE(sizeof(Result) == 264u);
    STATIC_REQUIRE(alignof(Result) == 2u);
    STATIC_REQUIRE(offsetof(Result, Status) == 0u);
    STATIC_REQUIRE(offsetof(Result, Reserved) == 1u);
    STATIC_REQUIRE(offsetof(Result, Identity) == 2u);
    STATIC_REQUIRE(std::is_standard_layout_v<Result>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Result>);
    STATIC_REQUIRE(noexcept(Capture::Capture(nullptr)));
}

TEST_CASE("Native load identity capture failure outputs stay byte-zero except status",
          "[quest.party-state][native-load-identity-capture][failure]")
{
    const char empty[] = {'\0'};
    std::array<char, Capture::kProbeBytes> unterminated{};
    std::fill(unterminated.begin(), unterminated.end(), 'Z');

    const auto nullResult = Capture::Capture(nullptr);
    const auto emptyResult = Capture::Capture(empty);
    const auto longResult = Capture::Capture(unterminated.data());

    RequireFailureZeroed(nullResult);
    RequireFailureZeroed(emptyResult);
    RequireFailureZeroed(longResult);
}
