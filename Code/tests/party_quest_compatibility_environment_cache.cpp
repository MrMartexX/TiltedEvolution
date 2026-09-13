#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>

#include <catch2/catch.hpp>

#include <chrono>
#include <fstream>
#include <thread>

namespace
{
struct EnvironmentSandbox
{
    std::filesystem::path Root;

    EnvironmentSandbox()
    {
        const auto nonce =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_party_quest_compatibility_cache_" + std::to_string(nonce));
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
        std::filesystem::create_directories(Root / "Scripts" / "Quest", ec);
        REQUIRE_FALSE(ec);
    }

    ~EnvironmentSandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    void Write(const std::filesystem::path& acRelativePath, const char* acBytes)
    {
        std::ofstream file(Root / acRelativePath, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file << acBytes;
        REQUIRE(file.good());
    }

    PartyQuestCompatibilityEnvironmentSnapshot Snapshot() const
    {
        PartyQuestCompatibilityEnvironmentSnapshot snapshot;
        snapshot.DataDirectory = Root;
        snapshot.OrderedPlugins = {
            {"Skyrim.esm", false},
            {"Update.esm", false},
            {"Light.esl", true}};
        return snapshot;
    }
};

PartyQuestCompatibilityEnvironmentCacheStatus WaitForTerminal(
    const PartyQuestCompatibilityEnvironmentCache& acCache)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto status = acCache.GetStatus();
        if (status != PartyQuestCompatibilityEnvironmentCacheStatus::Computing)
            return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return acCache.GetStatus();
}

void Populate(EnvironmentSandbox& aSandbox)
{
    aSandbox.Write("Skyrim.esm", "base-plugin-bytes");
    aSandbox.Write("Update.esm", "update-plugin-bytes");
    aSandbox.Write("Light.esl", "light-plugin-bytes");
    aSandbox.Write("Skyrim - Scripts.bsa", "archive-bytes");
    aSandbox.Write("Scripts/Quest/Example.pex", "compiled-script-bytes");
}
}

TEST_CASE("party quest compatibility environment is computed once and published atomically")
{
    EnvironmentSandbox sandbox;
    Populate(sandbox);

    PartyQuestCompatibilityEnvironmentCache cache;
    REQUIRE(cache.Start(sandbox.Snapshot()));
    REQUIRE(WaitForTerminal(cache) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Ready);

    const auto first = cache.GetReady();
    REQUIRE(first.has_value());
    REQUIRE(first->IsValid());
    REQUIRE(cache.GetComputationCount() == 1);

    sandbox.Write("Update.esm", "changed-after-publication");
    REQUIRE_FALSE(cache.Start(sandbox.Snapshot()));
    REQUIRE(cache.GetComputationCount() == 1);
    REQUIRE(cache.GetReady()->PluginEnvironment == first->PluginEnvironment);
    REQUIRE(cache.GetReady()->ScriptEnvironment == first->ScriptEnvironment);
}

TEST_CASE("party quest compatibility environment fails closed without complete plugin bytes")
{
    EnvironmentSandbox sandbox;
    Populate(sandbox);
    std::error_code ec;
    std::filesystem::remove(sandbox.Root / "Update.esm", ec);
    REQUIRE_FALSE(ec);

    PartyQuestCompatibilityEnvironmentCache cache;
    REQUIRE(cache.Start(sandbox.Snapshot()));
    REQUIRE(WaitForTerminal(cache) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Failed);
    REQUIRE_FALSE(cache.GetReady().has_value());
    REQUIRE(cache.GetComputationCount() == 1);
}

TEST_CASE("party quest compatibility environment rejects escaping plugin paths")
{
    EnvironmentSandbox sandbox;
    Populate(sandbox);
    auto snapshot = sandbox.Snapshot();
    snapshot.OrderedPlugins.push_back({"../outside.esm", false});

    PartyQuestCompatibilityEnvironmentCache cache;
    REQUIRE(cache.Start(std::move(snapshot)));
    REQUIRE(WaitForTerminal(cache) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Failed);
    REQUIRE_FALSE(cache.GetReady().has_value());
}

TEST_CASE("party quest compatibility environment identities change with environment bytes")
{
    EnvironmentSandbox firstSandbox;
    EnvironmentSandbox secondSandbox;
    Populate(firstSandbox);
    Populate(secondSandbox);
    secondSandbox.Write("Scripts/Quest/Example.pex", "different-script-bytes");

    PartyQuestCompatibilityEnvironmentCache first;
    PartyQuestCompatibilityEnvironmentCache second;
    REQUIRE(first.Start(firstSandbox.Snapshot()));
    REQUIRE(second.Start(secondSandbox.Snapshot()));
    REQUIRE(WaitForTerminal(first) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Ready);
    REQUIRE(WaitForTerminal(second) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Ready);
    REQUIRE(first.GetReady()->PluginEnvironment ==
        second.GetReady()->PluginEnvironment);
    REQUIRE(first.GetReady()->ScriptEnvironment !=
        second.GetReady()->ScriptEnvironment);
}

TEST_CASE("party quest compatibility environment stop never publishes partial state")
{
    EnvironmentSandbox sandbox;
    Populate(sandbox);

    PartyQuestCompatibilityEnvironmentCache cache;
    REQUIRE(cache.Start(sandbox.Snapshot()));
    cache.Stop();

    const auto status = cache.GetStatus();
    REQUIRE((status == PartyQuestCompatibilityEnvironmentCacheStatus::Cancelled ||
             status == PartyQuestCompatibilityEnvironmentCacheStatus::Ready));
    if (status == PartyQuestCompatibilityEnvironmentCacheStatus::Cancelled)
        REQUIRE_FALSE(cache.GetReady().has_value());
    else
        REQUIRE(cache.GetReady()->IsValid());
}
