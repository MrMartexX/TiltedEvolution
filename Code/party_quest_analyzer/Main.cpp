#include <Structs/Skyrim/PartyQuestOfflineCompatibilityAnalyzer.h>

#include <charconv>
#include <iostream>
#include <string_view>

namespace
{
void Usage()
{
    std::cerr << "Usage: PartyQuestCompatibilityAnalyzer --data ABSOLUTE_DATA_DIR "
        "--runtime VERSION --runtime-sha256 HEX64 --analyzer-version N "
        "--plugin standard|lite:RELATIVE_PATH [--plugin ...] "
        "[--archive PRIORITY:RELATIVE_PATH ...] [--loose-pex RELATIVE_PATH ...] "
        "--quest SOURCE_PLUGIN:LOCAL_FORM_ID [--quest ...] "
        "--manifest OUTPUT.json --report OUTPUT.txt\n";
}

bool Split(std::string_view value, std::string_view& left, std::string_view& right)
{
    const auto separator = value.find(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == value.size())
        return false;
    left = value.substr(0, separator);
    right = value.substr(separator + 1);
    return true;
}

bool Number(std::string_view value, uint32_t& result, int base = 10)
{
    const char* first = value.data();
    if (base == 16 && value.starts_with("0x"))
        first += 2;
    const auto parsed = std::from_chars(first, value.data() + value.size(), result, base);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
}

int main(int argc, char** argv)
{
    PartyQuestOfflineAnalyzerInput input;
    std::filesystem::path manifest;
    std::filesystem::path report;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option = argv[i];
        if (i + 1 >= argc)
        {
            Usage();
            return 2;
        }
        const std::string_view value = argv[++i];
        if (option == "--data") input.Environment.DataDirectory = value;
        else if (option == "--runtime") input.RuntimeVersion = value;
        else if (option == "--runtime-sha256") input.RuntimeExecutableSha256 = value;
        else if (option == "--manifest") manifest = value;
        else if (option == "--report") report = value;
        else if (option == "--loose-pex") input.LoosePexFiles.emplace_back(value);
        else if (option == "--analyzer-version")
        {
            if (!Number(value, input.AnalyzerVersion)) { Usage(); return 2; }
        }
        else if (option == "--plugin")
        {
            std::string_view kind, path;
            if (!Split(value, kind, path) || (kind != "standard" && kind != "lite"))
            { Usage(); return 2; }
            input.Environment.OrderedPlugins.push_back(
                {std::filesystem::path(path), kind == "lite"});
        }
        else if (option == "--archive")
        {
            std::string_view priority, path;
            uint32_t parsed{};
            if (!Split(value, priority, path) || !Number(priority, parsed))
            { Usage(); return 2; }
            input.OrderedArchives.push_back({std::filesystem::path(path), parsed});
        }
        else if (option == "--quest")
        {
            std::string_view plugin, form;
            uint32_t parsed{};
            if (!Split(value, plugin, form) || !Number(form, parsed, 16))
            { Usage(); return 2; }
            input.Quests.push_back({std::string(plugin), parsed});
        }
        else { Usage(); return 2; }
    }
    if (manifest.empty() || report.empty()) { Usage(); return 2; }

    const auto result = PartyQuestOfflineCompatibilityAnalyzer::Analyze(input);
    if (!PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(result, manifest, report))
    {
        std::cerr << "Analysis did not produce a complete publishable artifact.\n";
        for (const auto& error : result.Errors) std::cerr << "- " << error << '\n';
        return result.Status == PartyQuestOfflineAnalysisStatus::Cancelled ? 3 : 1;
    }
    std::cout << "Review-only artifacts written. Runtime authorization remains disabled.\n";
    if (!result.IsComplete())
    {
        for (const auto& error : result.Errors) std::cerr << "- " << error << '\n';
        return 1;
    }
    return 0;
}
