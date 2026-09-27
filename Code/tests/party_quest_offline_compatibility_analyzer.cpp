#include <Structs/Skyrim/PartyQuestOfflineCompatibilityAnalyzer.h>

#include <catch2/catch.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <thread>

namespace
{
uint32_t Tag(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
        (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

void U16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void U32(std::vector<uint8_t>& out, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

void U64(std::vector<uint8_t>& out, uint64_t value)
{
    U32(out, static_cast<uint32_t>(value));
    U32(out, static_cast<uint32_t>(value >> 32));
}

void Chunk(std::vector<uint8_t>& out, uint32_t tag, std::span<const uint8_t> data)
{
    U32(out, tag);
    U16(out, static_cast<uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<uint8_t> Record(
    uint32_t tag, uint32_t formId, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    U32(out, tag);
    U32(out, static_cast<uint32_t>(data.size()));
    U32(out, 0);
    U32(out, formId);
    U32(out, 0);
    U16(out, 44);
    U16(out, 0);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<uint8_t> Plugin(
    std::optional<std::string> master,
    uint32_t questFormId,
    std::string editorId,
    bool sideEffects = true)
{
    std::vector<uint8_t> tes4Data;
    if (master)
    {
        std::vector<uint8_t> value(master->begin(), master->end());
        value.push_back(0);
        Chunk(tes4Data, Tag('M', 'A', 'S', 'T'), value);
        std::array<uint8_t, 8> data{};
        Chunk(tes4Data, Tag('D', 'A', 'T', 'A'), data);
    }
    auto result = Record(Tag('T', 'E', 'S', '4'), 0, tes4Data);
    std::vector<uint8_t> quest;
    std::vector<uint8_t> editor(editorId.begin(), editorId.end());
    editor.push_back(0);
    Chunk(quest, Tag('E', 'D', 'I', 'D'), editor);
    for (const uint16_t stage : {10u, 20u, 30u})
    {
        std::array<uint8_t, 2> value{
            static_cast<uint8_t>(stage), static_cast<uint8_t>(stage >> 8)};
        Chunk(quest, Tag('I', 'N', 'D', 'X'), value);
    }
    std::array<uint8_t, 2> objective{20, 0};
    Chunk(quest, Tag('Q', 'O', 'B', 'J'), objective);
    if (sideEffects)
    {
        const std::array<uint8_t, 12> vmad{
            5, 0, 2, 0, 1, 0, 'F', 'o', 'o', 0, 0, 0};
        Chunk(quest, Tag('V', 'M', 'A', 'D'), vmad);
        std::array<uint8_t, 4> alias{1, 0, 0, 0};
        Chunk(quest, Tag('A', 'L', 'S', 'T'), alias);
    }
    const auto questRecord = Record(Tag('Q', 'U', 'S', 'T'), questFormId, quest);
    result.insert(result.end(), questRecord.begin(), questRecord.end());
    return result;
}

std::vector<uint8_t> BsaWithScript(std::string name)
{
    std::vector<uint8_t> out;
    out.insert(out.end(), {'B', 'S', 'A', 0});
    U32(out, 104);
    U32(out, 36);
    U32(out, 0);
    U32(out, 1);
    U32(out, 1);
    U32(out, 8); // "scripts" plus nul
    U32(out, static_cast<uint32_t>(name.size() + 1));
    U32(out, 0);
    U64(out, 0);
    U32(out, 1);
    U32(out, 0);
    out.push_back(8);
    out.insert(out.end(), {'s','c','r','i','p','t','s',0});
    U64(out, 0);
    U32(out, 0);
    U32(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);
    return out;
}

void Write(const std::filesystem::path& path, std::span<const uint8_t> bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.good());
    file.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
}

class Fixture final
{
public:
    Fixture()
    {
        static std::atomic<uint64_t> counter{0};
        Root = std::filesystem::temp_directory_path() /
            ("tp-offline-analyzer-" + std::to_string(++counter));
        std::filesystem::create_directories(Root / "Scripts");
        Write(Root / "Base.esm", Plugin(std::nullopt, 0x123, "BaseQuest"));
        Write(Root / "Override.esp", Plugin("Base.esm", 0x123, "WinningQuest"));
        Write(Root / "Scripts" / "Foo.pex", std::array<uint8_t, 4>{1,2,3,4});
        Write(Root / "Scripts.bsa", BsaWithScript("Foo.pex"));
    }

    ~Fixture() { std::filesystem::remove_all(Root); }

    PartyQuestOfflineAnalyzerInput Input(bool withOverride = true) const
    {
        PartyQuestOfflineAnalyzerInput input;
        input.AnalyzerVersion = 7;
        input.RuntimeVersion = "1.6.1170.0";
        input.RuntimeExecutableSha256 = std::string(64, 'a');
        input.Environment.DataDirectory = Root;
        input.Environment.OrderedPlugins.push_back({"Base.esm", false});
        if (withOverride)
            input.Environment.OrderedPlugins.push_back({"Override.esp", false});
        input.OrderedArchives.push_back({"Scripts.bsa", 1});
        input.LoosePexFiles.push_back("Scripts/Foo.pex");
        input.Quests.push_back({"Base.esm", 0x123});
        return input;
    }

    std::filesystem::path Root;
};
}

TEST_CASE("Offline analyzer output is byte stable and never authorizes runtime", "[quest.party-state][offline-analyzer]")
{
    Fixture fixture;
    const auto first = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    const auto second = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    REQUIRE(first.IsComplete());
    REQUIRE(second.IsComplete());
    // Cross-platform golden values: CI must produce the same bytes and fixed-width
    // hash domains on MSVC and GCC/Clang.
    REQUIRE(first.EnvironmentFingerprints.PluginEnvironment == 0x7acc3ca7855c8d11ull);
    REQUIRE(first.EnvironmentFingerprints.ScriptEnvironment == 0xaa5709f94cc00612ull);
    REQUIRE(first.InputProvenanceFingerprint == 0xb7f5fc12fbee2ef6ull);
    REQUIRE(first.MachineReadableManifest == second.MachineReadableManifest);
    REQUIRE(first.HumanReviewReport == second.HumanReviewReport);
    REQUIRE(first.MachineReadableManifest.find("\"authorizes_runtime\": false") != std::string::npos);
    REQUIRE(first.MachineReadableManifest.find("\"authorizes_mutation\": false") != std::string::npos);
    REQUIRE(first.Quests.front().WinningPlugin == "Override.esp");
    REQUIRE(first.Quests.front().HasPotentialSideEffects);
}

TEST_CASE("Offline analyzer shares exact runtime environment fingerprints", "[quest.party-state][offline-analyzer][fingerprint]")
{
    Fixture fixture;
    const auto input = fixture.Input();
    const auto shared = ComputePartyQuestCompatibilityEnvironmentFingerprints(input.Environment);
    const auto analyzed = PartyQuestOfflineCompatibilityAnalyzer::Analyze(input);
    REQUIRE(shared);
    REQUIRE(analyzed.IsComplete());
    REQUIRE(analyzed.EnvironmentFingerprints.PluginEnvironment == shared->PluginEnvironment);
    REQUIRE(analyzed.EnvironmentFingerprints.ScriptEnvironment == shared->ScriptEnvironment);
}

TEST_CASE("Winning override and load order changes alter review identity", "[quest.party-state][offline-analyzer][override]")
{
    Fixture fixture;
    const auto base = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input(false));
    const auto overridden = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input(true));
    REQUIRE(base.IsComplete());
    REQUIRE(overridden.IsComplete());
    REQUIRE(base.Quests.front().WinningPlugin == "Base.esm");
    REQUIRE(base.Quests.front().WinningOverrideFingerprint !=
        overridden.Quests.front().WinningOverrideFingerprint);
    REQUIRE(base.EnvironmentFingerprints.PluginEnvironment !=
        overridden.EnvironmentFingerprints.PluginEnvironment);
}

TEST_CASE("Standard and ESL local FormIDs retain distinct canonical identity", "[quest.party-state][offline-analyzer][esl]")
{
    Fixture fixture;
    Write(fixture.Root / "Lite.esl", Plugin(std::nullopt, 0xABC, "LiteQuest", false));
    auto input = fixture.Input(false);
    input.Environment.OrderedPlugins.push_back({"Lite.esl", true});
    input.Quests.push_back({"Lite.esl", 0xABC});
    const auto analyzed = PartyQuestOfflineCompatibilityAnalyzer::Analyze(input);
    REQUIRE(analyzed.IsComplete());
    REQUIRE(analyzed.Quests.size() == 2);
    REQUIRE_FALSE(analyzed.Quests[0].SourceIsLite);
    REQUIRE(analyzed.Quests[0].LocalFormId == 0x123);
    REQUIRE(analyzed.Quests[1].SourceIsLite);
    REQUIRE(analyzed.Quests[1].LocalFormId == 0xABC);
    REQUIRE(analyzed.Quests[0].ResolvedTopologyFingerprint !=
        analyzed.Quests[1].ResolvedTopologyFingerprint);
}

TEST_CASE("Loose Papyrus source wins over matching BSA entry", "[quest.party-state][offline-analyzer][bsa]")
{
    Fixture fixture;
    const auto analyzed = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    REQUIRE(analyzed.IsComplete());
    REQUIRE(analyzed.Quests.front().ScriptDependencies ==
        std::vector<std::string>{"scripts/foo.pex@loose:scripts/foo.pex"});
}

TEST_CASE("Missing or corrupt archive produces explicit incomplete artifact", "[quest.party-state][offline-analyzer][fail-closed]")
{
    Fixture fixture;
    auto missing = fixture.Input();
    missing.OrderedArchives.front().RelativePath = "Missing.bsa";
    const auto missingResult = PartyQuestOfflineCompatibilityAnalyzer::Analyze(missing);
    REQUIRE_FALSE(missingResult.IsComplete());
    REQUIRE(missingResult.MachineReadableManifest.find("Incomplete") != std::string::npos);
    REQUIRE_FALSE(missingResult.Errors.empty());
    REQUIRE(PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(missingResult,
        fixture.Root / "incomplete.json", fixture.Root / "incomplete.txt"));

    Write(fixture.Root / "Corrupt.bsa", std::array<uint8_t, 4>{'B','A','D','!'});
    auto corrupt = fixture.Input();
    corrupt.OrderedArchives.front().RelativePath = "Corrupt.bsa";
    REQUIRE_FALSE(PartyQuestOfflineCompatibilityAnalyzer::Analyze(corrupt).IsComplete());
}

TEST_CASE("Same filenames with changed bytes alter plugin and script fingerprints", "[quest.party-state][offline-analyzer][identity]")
{
    Fixture fixture;
    const auto before = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    Write(fixture.Root / "Scripts" / "Foo.pex", std::array<uint8_t, 4>{4,3,2,1});
    const auto scriptChanged = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    REQUIRE(before.EnvironmentFingerprints.ScriptEnvironment !=
        scriptChanged.EnvironmentFingerprints.ScriptEnvironment);
    Write(fixture.Root / "Base.esm", Plugin(std::nullopt, 0x123, "ChangedQuest"));
    const auto pluginChanged = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input(false));
    REQUIRE(before.EnvironmentFingerprints.PluginEnvironment !=
        pluginChanged.EnvironmentFingerprints.PluginEnvironment);
}

TEST_CASE("Normalized path case is identity stable", "[quest.party-state][offline-analyzer][path]")
{
    Fixture fixture;
    auto firstInput = fixture.Input(false);
    auto secondInput = fixture.Input(false);
    secondInput.Environment.OrderedPlugins.front().RelativePath = "BASE.ESM";
#ifdef _WIN32
    const auto first = PartyQuestOfflineCompatibilityAnalyzer::Analyze(firstInput);
    const auto second = PartyQuestOfflineCompatibilityAnalyzer::Analyze(secondInput);
    REQUIRE(first.IsComplete());
    REQUIRE(second.IsComplete());
    REQUIRE(first.EnvironmentFingerprints.PluginEnvironment ==
        second.EnvironmentFingerprints.PluginEnvironment);
#else
    // Case-sensitive snapshots must contain the named byte source; omission is
    // explicit rather than silently normalized to a different host file.
    REQUIRE_FALSE(PartyQuestOfflineCompatibilityAnalyzer::Analyze(secondInput).IsComplete());
#endif
}

TEST_CASE("Analyzer version changes provenance and manifest", "[quest.party-state][offline-analyzer][version]")
{
    Fixture fixture;
    auto firstInput = fixture.Input();
    auto secondInput = fixture.Input();
    ++secondInput.AnalyzerVersion;
    const auto first = PartyQuestOfflineCompatibilityAnalyzer::Analyze(firstInput);
    const auto second = PartyQuestOfflineCompatibilityAnalyzer::Analyze(secondInput);
    REQUIRE(first.InputProvenanceFingerprint != second.InputProvenanceFingerprint);
    REQUIRE(first.MachineReadableManifest != second.MachineReadableManifest);
}

TEST_CASE("Cancellation and publication failures leave no partial artifacts", "[quest.party-state][offline-analyzer][cancellation]")
{
    Fixture fixture;
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = PartyQuestOfflineCompatibilityAnalyzer::Analyze(
        fixture.Input(), stop.get_token());
    REQUIRE(cancelled.Status == PartyQuestOfflineAnalysisStatus::Cancelled);
    const auto manifest = fixture.Root / "cancelled.json";
    const auto report = fixture.Root / "cancelled.txt";
    REQUIRE_FALSE(PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(
        cancelled, manifest, report));
    REQUIRE_FALSE(std::filesystem::exists(manifest));
    REQUIRE_FALSE(std::filesystem::exists(report));

    const auto complete = PartyQuestOfflineCompatibilityAnalyzer::Analyze(fixture.Input());
    REQUIRE(PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(
        complete, manifest, report));
    REQUIRE(std::filesystem::exists(manifest));
    REQUIRE(std::filesystem::exists(report));
    REQUIRE_FALSE(PartyQuestOfflineCompatibilityAnalyzer::WriteArtifacts(
        complete, manifest, report));
}

TEST_CASE("Quest update hot path consumes immutable fingerprints without scanning", "[quest.party-state][offline-analyzer][performance]")
{
    Fixture fixture;
    PartyQuestCompatibilityEnvironmentCache cache;
    REQUIRE(cache.Start(fixture.Input().Environment));
    for (size_t i = 0; i < 5000 &&
         cache.GetStatus() == PartyQuestCompatibilityEnvironmentCacheStatus::Computing; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(cache.GetStatus() == PartyQuestCompatibilityEnvironmentCacheStatus::Ready);
    const uint64_t computations = cache.GetComputationCount();
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < 10000; ++i)
        REQUIRE(cache.GetReady()->IsValid());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(cache.GetComputationCount() == computations);
    REQUIRE(elapsed < std::chrono::seconds(2));
}
