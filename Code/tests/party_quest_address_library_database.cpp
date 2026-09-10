#include <Structs/Skyrim/SkyrimAddressLibraryDatabase.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
template <typename T>
void AppendUnsigned(std::vector<uint8_t>& aBytes, T aValue)
{
    static_assert(std::is_unsigned_v<T>);
    for (size_t index = 0; index < sizeof(T); ++index)
    {
        aBytes.push_back(static_cast<uint8_t>(aValue >> (index * 8u)));
    }
}

void AppendRuntime(
    std::vector<uint8_t>& aBytes,
    const SkyrimAddressLibraryRuntimeVersion& acRuntime)
{
    AppendUnsigned(aBytes, acRuntime.Major);
    AppendUnsigned(aBytes, acRuntime.Minor);
    AppendUnsigned(aBytes, acRuntime.Patch);
    AppendUnsigned(aBytes, acRuntime.Build);
}

std::vector<uint8_t> MakeDenseV5(
    const SkyrimAddressLibraryRuntimeVersion& acRuntime,
    std::span<const uint32_t> aOffsets)
{
    std::vector<uint8_t> bytes;
    AppendUnsigned(bytes, uint32_t{5u});
    AppendRuntime(bytes, acRuntime);

    std::array<uint8_t, 64> moduleName{};
    constexpr std::string_view kModule = "SkyrimSE.exe";
    std::copy(kModule.begin(), kModule.end(), moduleName.begin());
    bytes.insert(bytes.end(), moduleName.begin(), moduleName.end());
    AppendUnsigned(bytes, uint32_t{8u});
    AppendUnsigned(bytes, uint32_t{0u});
    AppendUnsigned(bytes, static_cast<uint32_t>(aOffsets.size()));
    for (const uint32_t offset : aOffsets)
        AppendUnsigned(bytes, offset);
    return bytes;
}

std::vector<uint8_t> MakeLegacy(
    uint32_t aFormat,
    const SkyrimAddressLibraryRuntimeVersion& acRuntime)
{
    std::vector<uint8_t> bytes;
    AppendUnsigned(bytes, aFormat);
    AppendRuntime(bytes, acRuntime);
    constexpr std::string_view kModule = "SkyrimSE.exe";
    AppendUnsigned(bytes, static_cast<uint32_t>(kModule.size()));
    bytes.insert(bytes.end(), kModule.begin(), kModule.end());
    AppendUnsigned(bytes, uint32_t{8u});
    AppendUnsigned(bytes, uint32_t{2u});

    // Absolute ID/offset followed by the previous values plus one.
    AppendUnsigned(bytes, uint8_t{0x00u});
    AppendUnsigned(bytes, uint64_t{100u});
    AppendUnsigned(bytes, uint64_t{0x1000u});
    AppendUnsigned(bytes, uint8_t{0x11u});
    return bytes;
}
} // namespace

TEST_CASE(
    "dense Address Library v5 parser binds exact Skyrim 1.7.104 runtime",
    "[quest.party-state.address-library][versions][v5]")
{
    const SkyrimAddressLibraryRuntimeVersion runtime{1u, 7u, 104u, 0u};
    const std::array<uint32_t, 5> offsets{
        0u,
        0x1000u,
        0u,
        0xABCDEFu,
        0xFFFFFFFFu};
    const auto bytes = MakeDenseV5(runtime, offsets);

    SkyrimAddressLibraryImage image{};
    REQUIRE(SkyrimAddressLibraryDatabaseParser::TryParse(
        bytes,
        runtime,
        "SkyrimSE.exe",
        image));
    REQUIRE(image.Format == 5u);
    REQUIRE(image.RuntimeVersion == runtime);
    REQUIRE(image.ModuleName == "SkyrimSE.exe");
    REQUIRE(image.PointerSize == 8u);
    REQUIRE(image.IdNamespace == SkyrimAddressLibraryIdNamespace::Ae);
    REQUIRE(image.Entries.size() == 3u);
    REQUIRE(image.Entries[0].Id == 1u);
    REQUIRE(image.Entries[0].Offset == 0x1000u);
    REQUIRE(image.Entries[1].Id == 3u);
    REQUIRE(image.Entries[1].Offset == 0xABCDEFu);
    REQUIRE(image.Entries[2].Id == 4u);
    REQUIRE(image.Entries[2].Offset == 0xFFFFFFFFu);
}

TEST_CASE(
    "legacy Address Library parser preserves SE and AE ID namespaces",
    "[quest.party-state.address-library][versions][legacy]")
{
    const SkyrimAddressLibraryRuntimeVersion runtime{1u, 5u, 97u, 0u};
    for (const auto [format, expectedNamespace] : {
             std::pair{1u, SkyrimAddressLibraryIdNamespace::Se},
             std::pair{2u, SkyrimAddressLibraryIdNamespace::Ae}})
    {
        const auto bytes = MakeLegacy(format, runtime);
        SkyrimAddressLibraryImage image{};
        REQUIRE(SkyrimAddressLibraryDatabaseParser::TryParse(
            bytes,
            runtime,
            "SkyrimSE.exe",
            image));
        REQUIRE(image.Format == format);
        REQUIRE(image.IdNamespace == expectedNamespace);
        REQUIRE(image.Entries.size() == 2u);
        REQUIRE(image.Entries[0].Id == 100u);
        REQUIRE(image.Entries[0].Offset == 0x1000u);
        REQUIRE(image.Entries[1].Id == 101u);
        REQUIRE(image.Entries[1].Offset == 0x1001u);
    }
}

TEST_CASE(
    "Address Library parser rejects unbound or malformed input without partial output",
    "[quest.party-state.address-library][versions][fail-closed]")
{
    const SkyrimAddressLibraryRuntimeVersion runtime{1u, 7u, 104u, 0u};
    const std::array<uint32_t, 2> offsets{0u, 0x1234u};
    const auto valid = MakeDenseV5(runtime, offsets);

    const auto rejects = [&](
        std::vector<uint8_t> bytes,
        SkyrimAddressLibraryRuntimeVersion expected) {
        SkyrimAddressLibraryImage image{};
        image.Format = 99u;
        REQUIRE_FALSE(SkyrimAddressLibraryDatabaseParser::TryParse(
            bytes,
            expected,
            "SkyrimSE.exe",
            image));
        REQUIRE(image.Format == 99u);
        REQUIRE(image.Entries.empty());
    };

    SECTION("unknown format")
    {
        auto bytes = valid;
        bytes[0] = 3u;
        rejects(std::move(bytes), runtime);
    }
    SECTION("runtime mismatch")
    {
        rejects(valid, {1u, 7u, 99u, 0u});
    }
    SECTION("module mismatch")
    {
        auto bytes = valid;
        bytes[20] = 'X';
        rejects(std::move(bytes), runtime);
    }
    SECTION("unknown dense data format")
    {
        auto bytes = valid;
        bytes[88] = 1u;
        rejects(std::move(bytes), runtime);
    }
    SECTION("truncated dense array")
    {
        auto bytes = valid;
        bytes.pop_back();
        rejects(std::move(bytes), runtime);
    }
    SECTION("oversized dense count")
    {
        auto bytes = valid;
        bytes[92] = 0xFFu;
        bytes[93] = 0xFFu;
        bytes[94] = 0xFFu;
        bytes[95] = 0x7Fu;
        rejects(std::move(bytes), runtime);
    }
    SECTION("invalid legacy encoding")
    {
        auto bytes = MakeLegacy(2u, runtime);
        constexpr size_t kFirstLegacyEntryOffset =
            4u + 16u + 4u + 12u + 4u + 4u;
        bytes[kFirstLegacyEntryOffset] = 0x08u;
        rejects(std::move(bytes), runtime);
    }
}
