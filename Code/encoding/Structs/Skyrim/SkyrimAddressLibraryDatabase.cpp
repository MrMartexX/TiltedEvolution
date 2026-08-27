#include <Structs/Skyrim/SkyrimAddressLibraryDatabase.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
constexpr uint32_t kFormatSeV1 = 1u;
constexpr uint32_t kFormatAeV2 = 2u;
constexpr uint32_t kFormatDenseV5 = 5u;
constexpr uint32_t kExpectedPointerSize = 8u;
constexpr uint32_t kMaximumLegacyModuleNameLength = 0xFFFFu;
constexpr uint32_t kMaximumEntryCount = 8u * 1024u * 1024u;
constexpr size_t kDenseV5HeaderSize = 96u;

class Reader final
{
public:
    explicit Reader(std::span<const uint8_t> aBytes) noexcept
        : m_bytes(aBytes)
    {
    }

    template <typename T>
    [[nodiscard]] bool ReadUnsigned(T& aOut) noexcept
    {
        static_assert(std::is_unsigned_v<T>);
        if (Remaining() < sizeof(T))
            return false;

        T value = 0;
        for (size_t index = 0; index < sizeof(T); ++index)
        {
            value |= static_cast<T>(m_bytes[m_offset + index]) <<
                static_cast<unsigned>(index * 8u);
        }

        m_offset += sizeof(T);
        aOut = value;
        return true;
    }

    [[nodiscard]] bool ReadString(
        size_t aLength,
        std::string& aOut) noexcept
    {
        if (Remaining() < aLength)
            return false;

        const auto first = m_bytes.begin() + static_cast<ptrdiff_t>(m_offset);
        const auto last = first + static_cast<ptrdiff_t>(aLength);
        if (std::find(first, last, uint8_t{0}) != last)
            return false;

        try
        {
            aOut.assign(
                reinterpret_cast<const char*>(m_bytes.data() + m_offset),
                aLength);
        }
        catch (...)
        {
            return false;
        }

        m_offset += aLength;
        return true;
    }

    [[nodiscard]] bool ReadFixedModuleName(
        size_t aLength,
        std::string& aOut) noexcept
    {
        if (Remaining() < aLength)
            return false;

        const auto* first = m_bytes.data() + m_offset;
        const auto* last = first + aLength;
        const auto* terminator = std::find(first, last, uint8_t{0});
        if (terminator == last ||
            std::any_of(terminator + 1, last, [](uint8_t aByte) {
                return aByte != 0u;
            }))
        {
            return false;
        }

        const size_t stringLength = static_cast<size_t>(terminator - first);
        try
        {
            aOut.assign(reinterpret_cast<const char*>(first), stringLength);
        }
        catch (...)
        {
            return false;
        }

        m_offset += aLength;
        return true;
    }

    [[nodiscard]] size_t Position() const noexcept { return m_offset; }
    [[nodiscard]] size_t Remaining() const noexcept
    {
        return m_bytes.size() - m_offset;
    }

private:
    std::span<const uint8_t> m_bytes;
    size_t m_offset{};
};

[[nodiscard]] bool ReadRuntimeVersion(
    Reader& aReader,
    SkyrimAddressLibraryRuntimeVersion& aVersion) noexcept
{
    return aReader.ReadUnsigned(aVersion.Major) &&
        aReader.ReadUnsigned(aVersion.Minor) &&
        aReader.ReadUnsigned(aVersion.Patch) &&
        aReader.ReadUnsigned(aVersion.Build);
}

[[nodiscard]] bool TryAdd(uint64_t aBase, uint64_t aDelta, uint64_t& aOut) noexcept
{
    if (aDelta > std::numeric_limits<uint64_t>::max() - aBase)
        return false;
    aOut = aBase + aDelta;
    return true;
}

[[nodiscard]] bool TrySubtract(
    uint64_t aBase,
    uint64_t aDelta,
    uint64_t& aOut) noexcept
{
    if (aDelta > aBase)
        return false;
    aOut = aBase - aDelta;
    return true;
}

[[nodiscard]] bool ReadLegacyValue(
    Reader& aReader,
    uint8_t aEncoding,
    uint64_t aPrevious,
    uint64_t& aOut) noexcept
{
    uint8_t byte = 0;
    uint16_t word = 0;
    uint32_t dword = 0;
    switch (aEncoding)
    {
    case 0:
        return aReader.ReadUnsigned(aOut);
    case 1:
        return TryAdd(aPrevious, 1u, aOut);
    case 2:
        return aReader.ReadUnsigned(byte) && TryAdd(aPrevious, byte, aOut);
    case 3:
        return aReader.ReadUnsigned(byte) && TrySubtract(aPrevious, byte, aOut);
    case 4:
        return aReader.ReadUnsigned(word) && TryAdd(aPrevious, word, aOut);
    case 5:
        return aReader.ReadUnsigned(word) && TrySubtract(aPrevious, word, aOut);
    case 6:
        if (!aReader.ReadUnsigned(word))
            return false;
        aOut = word;
        return true;
    case 7:
        if (!aReader.ReadUnsigned(dword))
            return false;
        aOut = dword;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ParseLegacyEntries(
    Reader& aReader,
    uint32_t aPointerSize,
    uint32_t aEntryCount,
    std::vector<SkyrimAddressLibraryEntry>& aEntries) noexcept
{
    if (aEntryCount > kMaximumEntryCount || aEntryCount > aReader.Remaining())
        return false;

    try
    {
        aEntries.reserve(aEntryCount);
    }
    catch (...)
    {
        return false;
    }

    uint64_t previousId = 0;
    uint64_t previousOffset = 0;
    for (uint32_t index = 0; index < aEntryCount; ++index)
    {
        uint8_t type = 0;
        if (!aReader.ReadUnsigned(type))
            return false;

        uint64_t id = 0;
        if (!ReadLegacyValue(aReader, type & 0xFu, previousId, id))
            return false;

        const bool scaled = (type & 0x80u) != 0u;
        const uint64_t offsetBase = scaled
            ? previousOffset / aPointerSize
            : previousOffset;
        uint64_t offset = 0;
        if (!ReadLegacyValue(
                aReader,
                static_cast<uint8_t>((type >> 4u) & 0x7u),
                offsetBase,
                offset))
        {
            return false;
        }

        if (scaled)
        {
            if (offset > std::numeric_limits<uint64_t>::max() / aPointerSize)
                return false;
            offset *= aPointerSize;
        }

        try
        {
            aEntries.push_back({id, offset});
        }
        catch (...)
        {
            return false;
        }
        previousId = id;
        previousOffset = offset;
    }

    if (aReader.Remaining() != 0u)
        return false;

    std::sort(aEntries.begin(), aEntries.end(), [](const auto& acLeft, const auto& acRight) {
        return acLeft.Id < acRight.Id;
    });
    return std::adjacent_find(
               aEntries.begin(),
               aEntries.end(),
               [](const auto& acLeft, const auto& acRight) {
                   return acLeft.Id == acRight.Id;
               }) == aEntries.end();
}

[[nodiscard]] bool ParseDenseV5Entries(
    Reader& aReader,
    uint32_t aEntryCount,
    std::vector<SkyrimAddressLibraryEntry>& aEntries) noexcept
{
    if (aEntryCount > kMaximumEntryCount ||
        aReader.Position() != kDenseV5HeaderSize ||
        aReader.Remaining() != static_cast<size_t>(aEntryCount) * sizeof(uint32_t))
    {
        return false;
    }

    try
    {
        aEntries.reserve(aEntryCount);
    }
    catch (...)
    {
        return false;
    }

    for (uint32_t id = 0; id < aEntryCount; ++id)
    {
        uint32_t offset = 0;
        if (!aReader.ReadUnsigned(offset))
            return false;
        if (offset == 0u)
            continue;

        try
        {
            aEntries.push_back({id, offset});
        }
        catch (...)
        {
            return false;
        }
    }

    return aReader.Remaining() == 0u;
}
} // namespace

bool SkyrimAddressLibraryDatabaseParser::TryParse(
    std::span<const uint8_t> aBytes,
    const SkyrimAddressLibraryRuntimeVersion& acExpectedRuntime,
    std::string_view aExpectedModuleName,
    SkyrimAddressLibraryImage& aOut) noexcept
{
    if (aBytes.empty() || aExpectedModuleName.empty() ||
        acExpectedRuntime.Major == 0u)
    {
        return false;
    }

    Reader reader(aBytes);
    SkyrimAddressLibraryImage candidate{};
    if (!reader.ReadUnsigned(candidate.Format) ||
        (candidate.Format != kFormatSeV1 &&
            candidate.Format != kFormatAeV2 &&
            candidate.Format != kFormatDenseV5) ||
        !ReadRuntimeVersion(reader, candidate.RuntimeVersion) ||
        candidate.RuntimeVersion != acExpectedRuntime)
    {
        return false;
    }

    uint32_t entryCount = 0;
    if (candidate.Format == kFormatDenseV5)
    {
        if (!reader.ReadFixedModuleName(64u, candidate.ModuleName) ||
            !reader.ReadUnsigned(candidate.PointerSize))
        {
            return false;
        }

        uint32_t dataFormat = 0;
        if (!reader.ReadUnsigned(dataFormat) || dataFormat != 0u ||
            !reader.ReadUnsigned(entryCount))
        {
            return false;
        }
        candidate.IdNamespace = SkyrimAddressLibraryIdNamespace::Ae;
    }
    else
    {
        uint32_t moduleNameLength = 0;
        if (!reader.ReadUnsigned(moduleNameLength) ||
            moduleNameLength == 0u ||
            moduleNameLength > kMaximumLegacyModuleNameLength ||
            !reader.ReadString(moduleNameLength, candidate.ModuleName) ||
            !reader.ReadUnsigned(candidate.PointerSize) ||
            !reader.ReadUnsigned(entryCount))
        {
            return false;
        }
        candidate.IdNamespace = candidate.Format == kFormatSeV1
            ? SkyrimAddressLibraryIdNamespace::Se
            : SkyrimAddressLibraryIdNamespace::Ae;
    }

    if (candidate.ModuleName != aExpectedModuleName ||
        candidate.PointerSize != kExpectedPointerSize ||
        entryCount == 0u)
    {
        return false;
    }

    const bool parsed = candidate.Format == kFormatDenseV5
        ? ParseDenseV5Entries(reader, entryCount, candidate.Entries)
        : ParseLegacyEntries(
              reader,
              candidate.PointerSize,
              entryCount,
              candidate.Entries);
    if (!parsed)
        return false;

    aOut = std::move(candidate);
    return true;
}
