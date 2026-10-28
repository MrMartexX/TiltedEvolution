#include <Structs/Skyrim/PartyQuestOfflineCompatibilityAnalyzer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <span>
#include <system_error>

#include <zlib.h>

namespace
{
constexpr uint32_t MakeTag(char a, char b, char c, char d) noexcept
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
        (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kTes4 = MakeTag('T', 'E', 'S', '4');
constexpr uint32_t kQuest = MakeTag('Q', 'U', 'S', 'T');
constexpr uint32_t kScene = MakeTag('S', 'C', 'E', 'N');
constexpr uint32_t kGroup = MakeTag('G', 'R', 'U', 'P');
constexpr uint32_t kCompressed = 0x00040000u;

class StableHash final
{
public:
    void Bytes(std::span<const uint8_t> aData) noexcept
    {
        for (const uint8_t value : aData)
        {
            m_value ^= value;
            m_value *= 1099511628211ull;
        }
    }

    void U8(uint8_t aValue) noexcept { Bytes({&aValue, 1}); }
    void U16(uint16_t aValue) noexcept
    {
        const std::array<uint8_t, 2> bytes{
            static_cast<uint8_t>(aValue), static_cast<uint8_t>(aValue >> 8)};
        Bytes(bytes);
    }
    void U32(uint32_t aValue) noexcept
    {
        const std::array<uint8_t, 4> bytes{
            static_cast<uint8_t>(aValue), static_cast<uint8_t>(aValue >> 8),
            static_cast<uint8_t>(aValue >> 16), static_cast<uint8_t>(aValue >> 24)};
        Bytes(bytes);
    }
    void U64(uint64_t aValue) noexcept
    {
        U32(static_cast<uint32_t>(aValue));
        U32(static_cast<uint32_t>(aValue >> 32));
    }
    void String(std::string aValue) noexcept
    {
        std::replace(aValue.begin(), aValue.end(), '\\', '/');
        std::transform(aValue.begin(), aValue.end(), aValue.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        U64(static_cast<uint64_t>(aValue.size()));
        Bytes({reinterpret_cast<const uint8_t*>(aValue.data()), aValue.size()});
    }
    [[nodiscard]] uint64_t Value() const noexcept { return m_value ? m_value : 1; }

private:
    uint64_t m_value{14695981039346656037ull};
};

uint16_t Read16(std::span<const uint8_t> aData, size_t aOffset, bool& aOk) noexcept
{
    if (aOffset > aData.size() || aData.size() - aOffset < 2)
    {
        aOk = false;
        return 0;
    }
    return static_cast<uint16_t>(aData[aOffset]) |
        static_cast<uint16_t>(aData[aOffset + 1] << 8);
}

uint32_t Read32(std::span<const uint8_t> aData, size_t aOffset, bool& aOk) noexcept
{
    if (aOffset > aData.size() || aData.size() - aOffset < 4)
    {
        aOk = false;
        return 0;
    }
    return static_cast<uint32_t>(aData[aOffset]) |
        (static_cast<uint32_t>(aData[aOffset + 1]) << 8) |
        (static_cast<uint32_t>(aData[aOffset + 2]) << 16) |
        (static_cast<uint32_t>(aData[aOffset + 3]) << 24);
}

uint64_t Read64(std::span<const uint8_t> aData, size_t aOffset, bool& aOk) noexcept
{
    const uint32_t low = Read32(aData, aOffset, aOk);
    const uint32_t high = Read32(aData, aOffset + 4, aOk);
    return static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
}

std::string LowerPath(std::filesystem::path aPath)
{
    auto value = aPath.lexically_normal().generic_string();
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsContained(const std::filesystem::path& aPath) noexcept
{
    if (aPath.empty() || aPath.is_absolute())
        return false;
    for (const auto& component : aPath)
    {
        if (component == "..")
            return false;
    }
    return true;
}

std::optional<std::vector<uint8_t>> ReadFile(
    const std::filesystem::path& aPath,
    std::stop_token aStopToken)
{
    std::ifstream input(aPath, std::ios::binary | std::ios::ate);
    if (!input || aStopToken.stop_requested())
        return std::nullopt;
    const auto end = input.tellg();
    if (end < 0 || static_cast<uint64_t>(end) > (1ull << 32))
        return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0);
    constexpr size_t kChunk = 64 * 1024;
    for (size_t offset = 0; offset < bytes.size(); offset += kChunk)
    {
        if (aStopToken.stop_requested())
            return std::nullopt;
        const size_t count = std::min(kChunk, bytes.size() - offset);
        input.read(reinterpret_cast<char*>(bytes.data() + offset),
            static_cast<std::streamsize>(count));
        if (!input)
            return std::nullopt;
    }
    return bytes;
}

struct Subrecord final
{
    uint32_t Tag{};
    std::span<const uint8_t> Data;
};

std::optional<std::vector<Subrecord>> ParseSubrecords(
    std::span<const uint8_t> aData) noexcept
{
    std::vector<Subrecord> result;
    uint32_t extendedSize = 0;
    size_t offset = 0;
    while (offset < aData.size())
    {
        bool ok = true;
        if (aData.size() - offset < 6)
            return std::nullopt;
        const uint32_t tag = Read32(aData, offset, ok);
        uint32_t size = Read16(aData, offset + 4, ok);
        offset += 6;
        if (!ok)
            return std::nullopt;
        if (tag == MakeTag('X', 'X', 'X', 'X'))
        {
            if (size != 4 || aData.size() - offset < 4)
                return std::nullopt;
            extendedSize = Read32(aData, offset, ok);
            offset += 4;
            if (!ok)
                return std::nullopt;
            continue;
        }
        if (extendedSize)
        {
            size = extendedSize;
            extendedSize = 0;
        }
        if (size > aData.size() - offset)
            return std::nullopt;
        result.push_back({tag, aData.subspan(offset, size)});
        offset += size;
    }
    return result;
}

std::optional<std::vector<uint8_t>> InflateRecord(
    std::span<const uint8_t> aData) noexcept
{
    bool ok = true;
    const uint32_t expected = Read32(aData, 0, ok);
    if (!ok || expected == 0 || expected > (128u << 20))
        return std::nullopt;
    std::vector<uint8_t> output(expected);
    uLongf outputSize = expected;
    const int status = ::uncompress(
        output.data(), &outputSize, aData.data() + 4,
        static_cast<uLong>(aData.size() - 4));
    if (status != Z_OK || outputSize != expected)
        return std::nullopt;
    return output;
}

struct RawRecord final
{
    uint32_t Tag{};
    uint32_t Flags{};
    uint32_t FormId{};
    std::vector<uint8_t> Data;
};

bool WalkRecords(
    std::span<const uint8_t> aBytes,
    size_t aBegin,
    size_t aEnd,
    std::vector<RawRecord>& aRecords,
    std::stop_token aStopToken) noexcept
{
    size_t offset = aBegin;
    while (offset < aEnd)
    {
        if (aStopToken.stop_requested() || aEnd - offset < 24)
            return false;
        bool ok = true;
        const uint32_t tag = Read32(aBytes, offset, ok);
        const uint32_t size = Read32(aBytes, offset + 4, ok);
        if (!ok)
            return false;
        if (tag == kGroup)
        {
            if (size < 24 || size > aEnd - offset ||
                !WalkRecords(aBytes, offset + 24, offset + size,
                    aRecords, aStopToken))
            {
                return false;
            }
            offset += size;
            continue;
        }
        if (size > aEnd - offset - 24)
            return false;
        const uint32_t flags = Read32(aBytes, offset + 8, ok);
        const uint32_t formId = Read32(aBytes, offset + 12, ok);
        if (!ok)
            return false;
        std::span<const uint8_t> data = aBytes.subspan(offset + 24, size);
        std::vector<uint8_t> owned;
        if ((flags & kCompressed) != 0)
        {
            auto inflated = InflateRecord(data);
            if (!inflated)
                return false;
            owned = std::move(*inflated);
        }
        else
        {
            owned.assign(data.begin(), data.end());
        }
        aRecords.push_back({tag, flags, formId, std::move(owned)});
        offset += 24 + size;
    }
    return offset == aEnd;
}

struct PluginView final
{
    std::string Name;
    bool IsLite{};
    std::vector<std::string> Masters;
    std::vector<RawRecord> Records;
    uint64_t BytesFingerprint{};
};

std::optional<PluginView> ParsePlugin(
    const std::filesystem::path& aPath,
    std::string aName,
    bool aIsLite,
    std::stop_token aStopToken)
{
    auto bytes = ReadFile(aPath, aStopToken);
    if (!bytes || bytes->size() < 24)
        return std::nullopt;
    PluginView result;
    result.Name = std::move(aName);
    result.IsLite = aIsLite;
    StableHash byteHash;
    byteHash.String("party-quest-plugin-bytes-v1");
    byteHash.Bytes(*bytes);
    byteHash.U64(bytes->size());
    result.BytesFingerprint = byteHash.Value();
    if (!WalkRecords(*bytes, 0, bytes->size(), result.Records, aStopToken) ||
        result.Records.empty() || result.Records.front().Tag != kTes4)
    {
        return std::nullopt;
    }
    auto header = ParseSubrecords(result.Records.front().Data);
    if (!header)
        return std::nullopt;
    for (const auto& chunk : *header)
    {
        if (chunk.Tag != MakeTag('M', 'A', 'S', 'T'))
            continue;
        auto end = std::find(chunk.Data.begin(), chunk.Data.end(), 0);
        if (end == chunk.Data.end())
            return std::nullopt;
        result.Masters.emplace_back(chunk.Data.begin(), end);
    }
    return result;
}

struct CanonicalForm final
{
    std::string Plugin;
    uint32_t Base{};
    auto operator<=>(const CanonicalForm&) const = default;
};

CanonicalForm ResolveForm(const PluginView& aPlugin, uint32_t aFormId)
{
    const uint8_t index = static_cast<uint8_t>(aFormId >> 24);
    CanonicalForm result;
    result.Plugin = index < aPlugin.Masters.size()
        ? aPlugin.Masters[index]
        : aPlugin.Name;
    std::transform(result.Plugin.begin(), result.Plugin.end(), result.Plugin.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    result.Base = aFormId & (aPlugin.IsLite ? 0xFFFu : 0xFFFFFFu);
    return result;
}

std::string ReadZString(std::span<const uint8_t> aData)
{
    const auto end = std::find(aData.begin(), aData.end(), 0);
    return std::string(aData.begin(), end);
}

std::string Hex64(uint64_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string EscapeJson(std::string_view value)
{
    std::ostringstream out;
    for (const unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch);
            else
                out << ch;
        }
    }
    return out.str();
}

const char* DispositionName(PartyQuestOfflineTransitionDisposition value) noexcept
{
    switch (value)
    {
    case PartyQuestOfflineTransitionDisposition::SafeCandidate: return "SafeCandidate";
    case PartyQuestOfflineTransitionDisposition::NeedsReview: return "NeedsReview";
    case PartyQuestOfflineTransitionDisposition::Unsupported: return "Unsupported";
    }
    return "Unsupported";
}

// Reads the BSA directory only. The relevant archive is fingerprinted in full
// by the shared environment primitive; unreadable/ambiguous indexes reject the
// artifact instead of silently dropping scripts.
std::optional<std::vector<std::string>> ReadBsaNames(
    const std::filesystem::path& aPath,
    std::stop_token aStopToken)
{
    auto bytes = ReadFile(aPath, aStopToken);
    if (!bytes || bytes->size() < 36 || (*bytes)[0] != 'B' ||
        (*bytes)[1] != 'S' || (*bytes)[2] != 'A' || (*bytes)[3] != 0)
    {
        return std::nullopt;
    }
    bool ok = true;
    const uint32_t version = Read32(*bytes, 4, ok);
    const uint32_t folderOffset = Read32(*bytes, 8, ok);
    const uint32_t folderCount = Read32(*bytes, 16, ok);
    const uint32_t fileCount = Read32(*bytes, 20, ok);
    const uint32_t folderNameBytes = Read32(*bytes, 24, ok);
    const uint32_t fileNameBytes = Read32(*bytes, 28, ok);
    if (!ok || (version != 104 && version != 105) || folderCount > (1u << 20) ||
        fileCount > (1u << 24) || folderOffset < 36)
    {
        return std::nullopt;
    }
    const size_t folderRecordSize = version == 105 ? 24 : 16;
    const uint64_t recordsEnd = static_cast<uint64_t>(folderOffset) +
        static_cast<uint64_t>(folderCount) * folderRecordSize;
    if (recordsEnd > bytes->size())
        return std::nullopt;

    struct Folder { uint32_t Count; uint64_t Offset; };
    std::vector<Folder> folders;
    size_t cursor = folderOffset;
    for (uint32_t i = 0; i < folderCount; ++i)
    {
        const uint32_t count = Read32(*bytes, cursor + 8, ok);
        const uint64_t offset = version == 105
            ? Read64(*bytes, cursor + 16, ok)
            : Read32(*bytes, cursor + 12, ok);
        if (!ok || count > fileCount)
            return std::nullopt;
        folders.push_back({count, offset});
        cursor += folderRecordSize;
    }

    uint64_t directoryBytes = folderNameBytes +
        static_cast<uint64_t>(fileCount) * 16;
    if (recordsEnd + directoryBytes > bytes->size() ||
        recordsEnd + directoryBytes + fileNameBytes > bytes->size())
    {
        return std::nullopt;
    }
    cursor = static_cast<size_t>(recordsEnd);
    for (const auto& folder : folders)
    {
        (void)folder.Offset;
        if (cursor >= bytes->size())
            return std::nullopt;
        const uint8_t nameLength = (*bytes)[cursor++];
        if (nameLength == 0 || nameLength > bytes->size() - cursor)
            return std::nullopt;
        cursor += nameLength;
        const uint64_t recordBytes = static_cast<uint64_t>(folder.Count) * 16;
        if (recordBytes > bytes->size() - cursor)
            return std::nullopt;
        cursor += static_cast<size_t>(recordBytes);
    }
    const size_t namesStart = cursor;
    if (fileNameBytes > bytes->size() - namesStart)
        return std::nullopt;
    std::vector<std::string> names;
    cursor = namesStart;
    const size_t namesEnd = namesStart + fileNameBytes;
    while (cursor < namesEnd && names.size() < fileCount)
    {
        const size_t start = cursor;
        while (cursor < namesEnd && (*bytes)[cursor] != 0)
            ++cursor;
        if (cursor == namesEnd)
            return std::nullopt;
        names.emplace_back(reinterpret_cast<const char*>(bytes->data() + start),
            cursor - start);
        ++cursor;
    }
    if (names.size() != fileCount || cursor != namesEnd)
        return std::nullopt;
    return names;
}

std::set<std::string> ExtractPrintableNames(std::span<const uint8_t> aData)
{
    std::set<std::string> result;
    size_t start = 0;
    for (size_t i = 0; i <= aData.size(); ++i)
    {
        const bool printable = i < aData.size() &&
            (std::isalnum(aData[i]) || aData[i] == '_' || aData[i] == '-');
        if (printable)
            continue;
        if (i - start >= 3 && i - start <= 260)
        {
            std::string value(reinterpret_cast<const char*>(aData.data() + start), i - start);
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            result.insert(std::move(value));
        }
        start = i + 1;
    }
    return result;
}

void BuildOutputs(
    const PartyQuestOfflineAnalyzerInput& input,
    PartyQuestOfflineAnalyzerResult& result)
{
    std::ostringstream json;
    json << "{\n  \"schema_version\": " << result.SchemaVersion
         << ",\n  \"analyzer_version\": " << result.AnalyzerVersion
         << ",\n  \"status\": \""
         << (result.Status == PartyQuestOfflineAnalysisStatus::Complete ? "Complete" :
             result.Status == PartyQuestOfflineAnalysisStatus::Cancelled ? "Cancelled" : "Incomplete")
         << "\",\n  \"authorizes_runtime\": false,"
            "\n  \"authorizes_mutation\": false,"
            "\n  \"runtime\": {\"version\": \""
         << EscapeJson(input.RuntimeVersion) << "\", \"executable_sha256\": \""
         << EscapeJson(input.RuntimeExecutableSha256) << "\"},"
            "\n  \"environment\": {\"plugin_fingerprint\": \""
         << Hex64(result.EnvironmentFingerprints.PluginEnvironment)
         << "\", \"script_fingerprint\": \""
         << Hex64(result.EnvironmentFingerprints.ScriptEnvironment)
         << "\"},\n  \"input_provenance\": {\"plugins\": [";
    for (size_t i = 0; i < input.Environment.OrderedPlugins.size(); ++i)
    {
        if (i) json << ',';
        const auto& plugin = input.Environment.OrderedPlugins[i];
        json << "{\"path\":\"" << EscapeJson(LowerPath(plugin.RelativePath))
             << "\",\"is_lite\":" << (plugin.IsLite ? "true" : "false") << '}';
    }
    json << "],\"archives\":[";
    for (size_t i = 0; i < input.OrderedArchives.size(); ++i)
    {
        if (i) json << ',';
        json << "{\"path\":\"" << EscapeJson(LowerPath(input.OrderedArchives[i].RelativePath))
             << "\",\"priority\":" << input.OrderedArchives[i].Priority << '}';
    }
    json << "],\"loose_pex\":[";
    for (size_t i = 0; i < input.LoosePexFiles.size(); ++i)
    {
        if (i) json << ',';
        json << '"' << EscapeJson(LowerPath(input.LoosePexFiles[i])) << '"';
    }
    json << "]},\n  \"input_provenance_fingerprint\": \""
         << Hex64(result.InputProvenanceFingerprint) << "\",\n  \"quests\": [";
    for (size_t i = 0; i < result.Quests.size(); ++i)
    {
        const auto& quest = result.Quests[i];
        if (i) json << ',';
        json << "\n    {\"game_id\": {\"source_plugin\":\""
             << EscapeJson(quest.SourcePlugin) << "\",\"local_form_id\":"
             << quest.LocalFormId << "}, \"source_plugin\": \"" << EscapeJson(quest.SourcePlugin)
             << "\", \"winning_plugin\": \"" << EscapeJson(quest.WinningPlugin)
             << "\", \"local_form_id\": " << quest.LocalFormId
             << ", \"source_is_lite\": " << (quest.SourceIsLite ? "true" : "false")
             << ", \"editor_id\": \"" << EscapeJson(quest.EditorId)
             << "\", \"resolved_topology_fingerprint\": \""
             << Hex64(quest.ResolvedTopologyFingerprint)
             << "\", \"winning_override_fingerprint\": \""
             << Hex64(quest.WinningOverrideFingerprint)
             << "\", \"script_dependency_fingerprint\": \""
             << Hex64(quest.ScriptDependencyFingerprint) << "\", \"stages\": [";
        for (size_t j = 0; j < quest.Stages.size(); ++j)
        {
            if (j) json << ',';
            json << quest.Stages[j];
        }
        json << "], \"objectives\": [";
        for (size_t j = 0; j < quest.Objectives.size(); ++j)
        {
            if (j) json << ',';
            json << quest.Objectives[j];
        }
        json << "], \"aliases\": " << quest.AliasCount
             << ", \"scenes\": " << quest.SceneCount
             << ", \"has_fragments\": " << (quest.HasPapyrusFragments ? "true" : "false")
             << ", \"has_conditions\": " << (quest.HasConditions ? "true" : "false")
             << ", \"has_aliases\": " << (quest.HasAliases ? "true" : "false")
             << ", \"has_scenes\": " << (quest.HasScenes ? "true" : "false")
             << ", \"has_side_effect_indicators\": " << (quest.HasPotentialSideEffects ? "true" : "false")
             << ", \"script_dependencies\": [";
        for (size_t j = 0; j < quest.ScriptDependencies.size(); ++j)
        {
            if (j) json << ',';
            json << '"' << EscapeJson(quest.ScriptDependencies[j]) << '"';
        }
        json << "], \"transitions\": [";
        for (size_t j = 0; j < quest.Transitions.size(); ++j)
        {
            const auto& edge = quest.Transitions[j];
            if (j) json << ',';
            json << "{\"from\":" << edge.FromStage << ",\"to\":" << edge.ToStage
                 << ",\"disposition\":\"" << DispositionName(edge.Disposition)
                 << "\",\"reason\":\"" << EscapeJson(edge.Reason) << "\"}";
        }
        json << "]}";
    }
    json << "\n  ],\n  \"errors\": [";
    for (size_t i = 0; i < result.Errors.size(); ++i)
    {
        if (i) json << ',';
        json << '"' << EscapeJson(result.Errors[i]) << '"';
    }
    json << "]\n}\n";
    result.MachineReadableManifest = json.str();

    std::ostringstream report;
    report << "Party Quest offline compatibility review\n"
           << "Status: " << (result.IsComplete() ? "COMPLETE" : "INCOMPLETE") << "\n"
           << "Analyzer version: " << result.AnalyzerVersion << "\n"
           << "Runtime: " << input.RuntimeVersion << "\n"
           << "Plugin environment: " << Hex64(result.EnvironmentFingerprints.PluginEnvironment) << "\n"
           << "Script environment: " << Hex64(result.EnvironmentFingerprints.ScriptEnvironment) << "\n"
           << "Important: this report is review input only; it authorizes no quest or mutation.\n";
    for (const auto& quest : result.Quests)
    {
        report << "\nQuest " << quest.SourcePlugin << ':' << std::hex << quest.LocalFormId << std::dec
               << " (winner " << quest.WinningPlugin << ")\n"
               << "  stages=" << quest.Stages.size() << ", objectives=" << quest.Objectives.size()
               << ", aliases=" << quest.AliasCount << ", scenes=" << quest.SceneCount << "\n"
               << "  side effects=" << (quest.HasPotentialSideEffects ? "YES; manual review required" : "none detected") << "\n";
    }
    for (const auto& error : result.Errors)
        report << "ERROR: " << error << "\n";
    result.HumanReviewReport = report.str();
}
}

PartyQuestOfflineAnalyzerResult PartyQuestOfflineCompatibilityAnalyzer::Analyze(
    const PartyQuestOfflineAnalyzerInput& acInput,
    std::stop_token aStopToken) noexcept
{
    PartyQuestOfflineAnalyzerResult result;
    result.AnalyzerVersion = acInput.AnalyzerVersion;
    try
    {
        const auto fail = [&result](std::string message)
        {
            result.Errors.push_back(std::move(message));
            result.Status = PartyQuestOfflineAnalysisStatus::Incomplete;
        };
        if (aStopToken.stop_requested())
        {
            result.Status = PartyQuestOfflineAnalysisStatus::Cancelled;
            BuildOutputs(acInput, result);
            return result;
        }
        if (acInput.AnalyzerVersion == 0 || acInput.RuntimeVersion.empty() ||
            acInput.RuntimeExecutableSha256.size() != 64 ||
            acInput.Environment.DataDirectory.empty() ||
            !acInput.Environment.DataDirectory.is_absolute() ||
            acInput.Environment.OrderedPlugins.empty() || acInput.Quests.empty())
        {
            fail("required analyzer input is missing or invalid");
            BuildOutputs(acInput, result);
            return result;
        }
        if (!std::all_of(acInput.RuntimeExecutableSha256.begin(),
                acInput.RuntimeExecutableSha256.end(),
                [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
        {
            fail("runtime executable SHA-256 is not hexadecimal");
            BuildOutputs(acInput, result);
            return result;
        }

        const auto environment =
            ComputePartyQuestCompatibilityEnvironmentFingerprints(
                acInput.Environment, aStopToken);
        if (!environment)
        {
            result.Status = aStopToken.stop_requested()
                ? PartyQuestOfflineAnalysisStatus::Cancelled
                : PartyQuestOfflineAnalysisStatus::Incomplete;
            if (!aStopToken.stop_requested())
                result.Errors.emplace_back("environment snapshot is unreadable or incomplete");
            BuildOutputs(acInput, result);
            return result;
        }
        result.EnvironmentFingerprints = *environment;

        std::vector<PluginView> plugins;
        std::set<std::string> pluginNames;
        for (const auto& plugin : acInput.Environment.OrderedPlugins)
        {
            if (aStopToken.stop_requested())
            {
                result.Status = PartyQuestOfflineAnalysisStatus::Cancelled;
                BuildOutputs(acInput, result);
                return result;
            }
            if (!IsContained(plugin.RelativePath))
            {
                fail("plugin path escapes the stable snapshot");
                continue;
            }
            const auto normalized = LowerPath(plugin.RelativePath.filename());
            if (!pluginNames.insert(normalized).second)
            {
                fail("duplicate plugin identity after path/case normalization: " + normalized);
                continue;
            }
            auto parsed = ParsePlugin(
                acInput.Environment.DataDirectory / plugin.RelativePath,
                plugin.RelativePath.filename().string(), plugin.IsLite, aStopToken);
            if (!parsed)
            {
                fail("unreadable or malformed plugin: " + plugin.RelativePath.generic_string());
                continue;
            }
            plugins.push_back(std::move(*parsed));
        }

        std::map<std::string, std::pair<uint32_t, std::string>> scriptSources;
        for (const auto& archive : acInput.OrderedArchives)
        {
            if (!IsContained(archive.RelativePath))
            {
                fail("archive path escapes the stable snapshot");
                continue;
            }
            auto names = ReadBsaNames(
                acInput.Environment.DataDirectory / archive.RelativePath,
                aStopToken);
            if (!names)
            {
                fail("unreadable or malformed BSA: " + archive.RelativePath.generic_string());
                continue;
            }
            for (auto name : *names)
            {
                std::replace(name.begin(), name.end(), '\\', '/');
                std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name.starts_with("scripts/") && name.ends_with(".pex"))
                {
                    auto& selected = scriptSources[name];
                    if (selected.second.empty() || archive.Priority >= selected.first)
                        selected = {archive.Priority, "bsa:" + LowerPath(archive.RelativePath)};
                }
            }
        }
        std::set<std::string> explicitLoose;
        for (const auto& relative : acInput.LoosePexFiles)
        {
            const auto normalized = LowerPath(relative);
            if (!IsContained(relative) || !normalized.starts_with("scripts/") ||
                !normalized.ends_with(".pex") || !explicitLoose.insert(normalized).second)
            {
                fail("invalid or duplicate explicit loose script: " +
                    relative.generic_string());
                continue;
            }
            auto bytes = ReadFile(acInput.Environment.DataDirectory / relative, aStopToken);
            if (!bytes)
            {
                fail("unreadable explicit loose script: " + relative.generic_string());
                continue;
            }
            scriptSources[normalized] = {UINT32_MAX, "loose:" + normalized};
        }

        std::map<CanonicalForm, std::pair<const PluginView*, const RawRecord*>> winners;
        std::map<CanonicalForm, uint32_t> sceneCounts;
        for (const auto& plugin : plugins)
        {
            for (const auto& record : plugin.Records)
            {
                if (record.Tag == kQuest)
                    winners[ResolveForm(plugin, record.FormId)] = {&plugin, &record};
            }
        }
        for (const auto& plugin : plugins)
        {
            for (const auto& record : plugin.Records)
            {
                if (record.Tag != kScene)
                    continue;
                auto chunks = ParseSubrecords(record.Data);
                if (!chunks)
                {
                    fail("malformed SCEN record in " + plugin.Name);
                    continue;
                }
                for (const auto& chunk : *chunks)
                {
                    if ((chunk.Tag == MakeTag('P', 'N', 'A', 'M') ||
                         chunk.Tag == MakeTag('Q', 'N', 'A', 'M')) &&
                        chunk.Data.size() == 4)
                    {
                        bool ok = true;
                        const auto quest = ResolveForm(plugin, Read32(chunk.Data, 0, ok));
                        if (ok)
                            ++sceneCounts[quest];
                    }
                }
            }
        }

        for (const auto& request : acInput.Quests)
        {
            std::string source = request.SourcePlugin;
            std::transform(source.begin(), source.end(), source.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const auto sourceIt = std::find_if(plugins.begin(), plugins.end(),
                [&source](const PluginView& plugin)
                {
                    auto name = plugin.Name;
                    std::transform(name.begin(), name.end(), name.begin(),
                        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    return name == source;
                });
            if (sourceIt == plugins.end())
            {
                fail("quest source plugin is not in the ordered set: " + request.SourcePlugin);
                continue;
            }
            CanonicalForm key{source, request.LocalFormId &
                (sourceIt->IsLite ? 0xFFFu : 0xFFFFFFu)};
            const auto winner = winners.find(key);
            if (winner == winners.end())
            {
                fail("requested QUST record was not found: " + request.SourcePlugin);
                continue;
            }
            const auto& plugin = *winner->second.first;
            const auto& record = *winner->second.second;
            auto chunks = ParseSubrecords(record.Data);
            if (!chunks)
            {
                fail("winning QUST record has malformed subrecords: " + plugin.Name);
                continue;
            }

            PartyQuestOfflineQuestCandidate candidate;
            candidate.SourcePlugin = request.SourcePlugin;
            candidate.WinningPlugin = plugin.Name;
            candidate.LocalFormId = key.Base;
            candidate.SourceIsLite = sourceIt->IsLite;
            candidate.SceneCount = sceneCounts[key];
            candidate.HasScenes = candidate.SceneCount != 0;
            std::set<uint16_t> stages;
            std::set<uint16_t> objectives;
            std::set<std::string> printable;
            StableHash topology;
            topology.String("party-quest-resolved-topology-v1");
            topology.String(key.Plugin);
            topology.U32(key.Base);
            for (const auto& chunk : *chunks)
            {
                topology.U32(chunk.Tag);
                topology.U64(chunk.Data.size());
                if (chunk.Tag == MakeTag('E', 'D', 'I', 'D'))
                    candidate.EditorId = ReadZString(chunk.Data);
                else if (chunk.Tag == MakeTag('I', 'N', 'D', 'X') && chunk.Data.size() >= 2)
                {
                    bool ok = true;
                    stages.insert(Read16(chunk.Data, 0, ok));
                }
                else if (chunk.Tag == MakeTag('Q', 'O', 'B', 'J') && chunk.Data.size() >= 2)
                {
                    bool ok = true;
                    objectives.insert(Read16(chunk.Data, 0, ok));
                }
                else if (chunk.Tag == MakeTag('A', 'L', 'S', 'T') ||
                         chunk.Tag == MakeTag('A', 'L', 'L', 'S'))
                {
                    ++candidate.AliasCount;
                }
                else if (chunk.Tag == MakeTag('V', 'M', 'A', 'D'))
                {
                    candidate.HasPapyrusFragments = true;
                    const auto names = ExtractPrintableNames(chunk.Data);
                    printable.insert(names.begin(), names.end());
                }
                else if (chunk.Tag == MakeTag('C', 'T', 'D', 'A'))
                {
                    candidate.HasConditions = true;
                }
            }
            candidate.Stages.assign(stages.begin(), stages.end());
            candidate.Objectives.assign(objectives.begin(), objectives.end());
            candidate.HasAliases = candidate.AliasCount != 0;
            candidate.HasPotentialSideEffects = candidate.HasPapyrusFragments ||
                candidate.HasConditions || candidate.HasAliases || candidate.HasScenes;
            topology.U32(candidate.AliasCount);
            topology.U32(candidate.SceneCount);
            for (const auto stage : candidate.Stages) topology.U16(stage);
            for (const auto objective : candidate.Objectives) topology.U16(objective);
            topology.U8(candidate.HasPapyrusFragments);
            topology.U8(candidate.HasConditions);
            candidate.ResolvedTopologyFingerprint = topology.Value();

            StableHash overrideHash;
            overrideHash.String("WINOVRDE");
            overrideHash.U64(candidate.ResolvedTopologyFingerprint);
            overrideHash.U64(result.EnvironmentFingerprints.PluginEnvironment);
            overrideHash.U64(plugin.BytesFingerprint);
            candidate.WinningOverrideFingerprint = overrideHash.Value();

            StableHash scripts;
            scripts.String("party-quest-script-dependencies-v1");
            for (const auto& [path, selected] : scriptSources)
            {
                auto stem = std::filesystem::path(path).stem().string();
                if (!printable.contains(stem))
                    continue;
                candidate.ScriptDependencies.push_back(path + "@" + selected.second);
                scripts.String(candidate.ScriptDependencies.back());
            }
            scripts.U64(result.EnvironmentFingerprints.ScriptEnvironment);
            candidate.ScriptDependencyFingerprint = scripts.Value();

            for (size_t i = 1; i < candidate.Stages.size(); ++i)
            {
                PartyQuestOfflineTransitionEdge edge;
                edge.FromStage = candidate.Stages[i - 1];
                edge.ToStage = candidate.Stages[i];
                edge.Disposition = candidate.HasPotentialSideEffects
                    ? PartyQuestOfflineTransitionDisposition::NeedsReview
                    : PartyQuestOfflineTransitionDisposition::SafeCandidate;
                edge.Reason = candidate.HasPotentialSideEffects
                    ? "quest contains fragments, conditions, aliases or scenes"
                    : "adjacent declared stages with no detected side-effect indicator";
                candidate.Transitions.push_back(std::move(edge));
            }
            result.Quests.push_back(std::move(candidate));
        }

        StableHash provenance;
        provenance.String("party-quest-offline-analyzer-input-v1");
        provenance.U32(acInput.AnalyzerVersion);
        provenance.String(acInput.RuntimeVersion);
        provenance.String(acInput.RuntimeExecutableSha256);
        provenance.U64(result.EnvironmentFingerprints.PluginEnvironment);
        provenance.U64(result.EnvironmentFingerprints.ScriptEnvironment);
        for (const auto& plugin : acInput.Environment.OrderedPlugins)
        {
            provenance.String(plugin.RelativePath.generic_string());
            provenance.U8(plugin.IsLite);
        }
        for (const auto& archive : acInput.OrderedArchives)
        {
            provenance.String(archive.RelativePath.generic_string());
            provenance.U32(archive.Priority);
        }
        for (const auto& loose : acInput.LoosePexFiles)
            provenance.String(LowerPath(loose));
        for (const auto& quest : acInput.Quests)
        {
            provenance.String(quest.SourcePlugin);
            provenance.U32(quest.LocalFormId);
        }
        result.InputProvenanceFingerprint = provenance.Value();
        if (result.Errors.empty() && result.Quests.size() == acInput.Quests.size())
            result.Status = PartyQuestOfflineAnalysisStatus::Complete;
        BuildOutputs(acInput, result);
        return result;
    }
    catch (...)
    {
        result.Status = PartyQuestOfflineAnalysisStatus::Incomplete;
        result.Errors.emplace_back("unexpected analyzer failure");
        BuildOutputs(acInput, result);
        return result;
    }
}

bool PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(
    const PartyQuestOfflineAnalyzerResult& acResult,
    const std::filesystem::path& acManifestPath,
    const std::filesystem::path& acReportPath) noexcept
{
    try
    {
        // Incomplete analyses are intentionally publishable review artifacts:
        // they make missing/corrupt input visible and carry no authority.
        if (acResult.Status == PartyQuestOfflineAnalysisStatus::Cancelled ||
            acResult.MachineReadableManifest.empty() ||
            acResult.HumanReviewReport.empty() ||
            acManifestPath.empty() || acReportPath.empty() ||
            acManifestPath == acReportPath)
        {
            return false;
        }
        const auto nonce = std::to_string(static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto manifestTemp = acManifestPath.string() + ".tmp." + nonce;
        const auto reportTemp = acReportPath.string() + ".tmp." + nonce;
        const auto cleanup = [&]()
        {
            std::error_code ignored;
            std::filesystem::remove(manifestTemp, ignored);
            std::filesystem::remove(reportTemp, ignored);
        };
        {
            std::ofstream manifest(manifestTemp, std::ios::binary | std::ios::trunc);
            std::ofstream report(reportTemp, std::ios::binary | std::ios::trunc);
            if (!manifest || !report)
            {
                cleanup();
                return false;
            }
            manifest.write(acResult.MachineReadableManifest.data(),
                static_cast<std::streamsize>(acResult.MachineReadableManifest.size()));
            report.write(acResult.HumanReviewReport.data(),
                static_cast<std::streamsize>(acResult.HumanReviewReport.size()));
            manifest.flush();
            report.flush();
            if (!manifest || !report)
            {
                cleanup();
                return false;
            }
        }
        std::error_code ec;
        if (std::filesystem::exists(acManifestPath, ec) || ec ||
            std::filesystem::exists(acReportPath, ec) || ec)
        {
            cleanup();
            return false;
        }
        std::filesystem::rename(manifestTemp, acManifestPath, ec);
        if (ec)
        {
            cleanup();
            return false;
        }
        std::filesystem::rename(reportTemp, acReportPath, ec);
        if (ec)
        {
            std::error_code ignored;
            std::filesystem::remove(acManifestPath, ignored);
            cleanup();
            return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
