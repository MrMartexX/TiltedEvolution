#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SkyrimAddressLibraryRuntimeVersion final
{
    uint32_t Major{};
    uint32_t Minor{};
    uint32_t Patch{};
    uint32_t Build{};

    [[nodiscard]] constexpr bool operator==(
        const SkyrimAddressLibraryRuntimeVersion&) const noexcept = default;
};

enum class SkyrimAddressLibraryIdNamespace : uint8_t
{
    Unknown = 0,
    Se = 1,
    Ae = 2
};

struct SkyrimAddressLibraryEntry final
{
    uint64_t Id{};
    uint64_t Offset{};
};

struct SkyrimAddressLibraryImage final
{
    uint32_t Format{};
    SkyrimAddressLibraryRuntimeVersion RuntimeVersion{};
    std::string ModuleName;
    uint32_t PointerSize{};
    SkyrimAddressLibraryIdNamespace IdNamespace{
        SkyrimAddressLibraryIdNamespace::Unknown};
    std::vector<SkyrimAddressLibraryEntry> Entries;
};

/**
 * Bounded parser for the official Address Library v1, v2 and dense v5 files.
 *
 * Parsing never grants cross-namespace relocation authority. In particular,
 * v1 contains SE-side IDs while TiltedEvolution currently consumes AE-side
 * IDs; callers must explicitly reject that mismatch until a reviewed ID
 * translation profile exists.
 */
class SkyrimAddressLibraryDatabaseParser final
{
public:
    [[nodiscard]] static bool TryParse(
        std::span<const uint8_t> aBytes,
        const SkyrimAddressLibraryRuntimeVersion& acExpectedRuntime,
        std::string_view aExpectedModuleName,
        SkyrimAddressLibraryImage& aOut) noexcept;
};
