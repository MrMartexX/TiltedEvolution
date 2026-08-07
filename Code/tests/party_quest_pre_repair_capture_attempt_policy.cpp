#include <Structs/Skyrim/PartyQuestPreRepairCaptureAttemptPolicy.h>

#include <catch2/catch.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace
{
struct AttemptPolicySandbox
{
    std::filesystem::path Root;
    PartyQuestCoopSavePaths Paths;

    AttemptPolicySandbox()
    {
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        Root = std::filesystem::temp_directory_path() /
            ("tp_pre_repair_attempt_policy_" + std::to_string(nonce));
        Paths.PlayerDirectory = Root / "player";
        Paths.SavesDirectory = Paths.PlayerDirectory / "saves";
        std::error_code ec;
        std::filesystem::create_directories(Paths.SavesDirectory, ec);
        REQUIRE_FALSE(ec);
    }

    ~AttemptPolicySandbox()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    void Touch(const std::string& acName)
    {
        std::ofstream stream(Paths.SavesDirectory / acName, std::ios::binary);
        REQUIRE(stream.good());
        stream << "evidence";
        stream.close();
        REQUIRE(stream.good());
    }
};

constexpr uint64_t kTransaction = 0x1111222233334444ull;
constexpr uint64_t kRevision = 0x5555666677778888ull;

std::string AttemptName(uint64_t aNonce, const char* acExtension)
{
    std::array<char, 128> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX%s",
        static_cast<unsigned long long>(kTransaction),
        static_cast<unsigned long long>(kRevision),
        static_cast<unsigned long long>(aNonce),
        acExtension);
    return buffer.data();
}
} // namespace

TEST_CASE("PreRepair capture retries have an immutable per-transaction bound", "[quest.party-state.pre-repair-attempts]")
{
    AttemptPolicySandbox sandbox;

    for (size_t i = 0;
         i < PartyQuestPreRepairCaptureAttemptPolicy::MaxAttemptsPerTransactionRevision - 1;
         ++i)
    {
        sandbox.Touch(AttemptName(i + 1, ".ess"));
        sandbox.Touch(AttemptName(i + 1, ".skse"));
    }

    auto decision = PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
        sandbox.Paths,
        kTransaction,
        kRevision);
    REQUIRE(decision.Status == PartyQuestPreRepairCaptureAttemptStatus::Ready);
    REQUIRE(decision.ExistingAttemptCount ==
        PartyQuestPreRepairCaptureAttemptPolicy::MaxAttemptsPerTransactionRevision - 1);

    sandbox.Touch(AttemptName(
        PartyQuestPreRepairCaptureAttemptPolicy::MaxAttemptsPerTransactionRevision,
        ".ess"));
    decision = PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
        sandbox.Paths,
        kTransaction,
        kRevision);
    REQUIRE(decision.Status ==
        PartyQuestPreRepairCaptureAttemptStatus::AttemptLimitExceeded);
    REQUIRE(decision.ExistingAttemptCount ==
        PartyQuestPreRepairCaptureAttemptPolicy::MaxAttemptsPerTransactionRevision);
}

TEST_CASE("PreRepair attempt admission ignores unrelated and malformed names", "[quest.party-state.pre-repair-attempts]")
{
    AttemptPolicySandbox sandbox;
    sandbox.Touch("manual-save.ess");
    sandbox.Touch(AttemptName(1, ".tmp"));
    sandbox.Touch("STR_PreRepair_T1111222233334444_R5555666677778888_AINVALID.ess");

    const auto decision = PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
        sandbox.Paths,
        kTransaction,
        kRevision);
    REQUIRE(decision.Status == PartyQuestPreRepairCaptureAttemptStatus::Ready);
    REQUIRE(decision.ExistingAttemptCount == 0);
}

TEST_CASE("PreRepair attempt directory scan is locally bounded", "[quest.party-state.pre-repair-attempts]")
{
    AttemptPolicySandbox sandbox;
    for (size_t i = 0;
         i <= PartyQuestPreRepairCaptureAttemptPolicy::MaxInspectedDirectoryEntries;
         ++i)
    {
        sandbox.Touch("unrelated-" + std::to_string(i) + ".tmp");
    }

    const auto decision = PartyQuestPreRepairCaptureAttemptPolicy::Evaluate(
        sandbox.Paths,
        kTransaction,
        kRevision);
    REQUIRE(decision.Status ==
        PartyQuestPreRepairCaptureAttemptStatus::DirectoryEntryLimitExceeded);
}

TEST_CASE("PreRepair retention removes only exact historical capture sources", "[quest.party-state.pre-repair-attempts]")
{
    AttemptPolicySandbox sandbox;
    const auto currentEss = AttemptName(1, ".ess");
    const auto currentSkse = AttemptName(1, ".skse");
    sandbox.Touch(currentEss);
    sandbox.Touch(currentSkse);

    constexpr uint64_t historicalTransaction = kTransaction + 1;
    std::array<char, 128> historical{};
    std::snprintf(
        historical.data(),
        historical.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX.ess",
        static_cast<unsigned long long>(historicalTransaction),
        static_cast<unsigned long long>(kRevision),
        2ull);
    sandbox.Touch(historical.data());
    sandbox.Touch("manual-save.ess");

    const auto decision =
        PartyQuestPreRepairCaptureAttemptPolicy::ReclaimHistoricalAttempts(
            sandbox.Paths,
            kTransaction,
            kRevision);
    REQUIRE(decision.Status == PartyQuestPreRepairCaptureAttemptStatus::Ready);
    REQUIRE(decision.ReclaimedFileCount == 1);
    REQUIRE(std::filesystem::exists(sandbox.Paths.SavesDirectory / currentEss));
    REQUIRE(std::filesystem::exists(sandbox.Paths.SavesDirectory / currentSkse));
    REQUIRE(std::filesystem::exists(sandbox.Paths.SavesDirectory / "manual-save.ess"));
    REQUIRE_FALSE(std::filesystem::exists(
        sandbox.Paths.SavesDirectory / historical.data()));
}

TEST_CASE("PreRepair retention validates all historical candidates before deleting", "[quest.party-state.pre-repair-attempts]")
{
    AttemptPolicySandbox sandbox;
    constexpr uint64_t historicalTransaction = kTransaction + 1;
    std::array<char, 128> regular{};
    std::array<char, 128> conflict{};
    std::snprintf(
        regular.data(),
        regular.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX.ess",
        static_cast<unsigned long long>(historicalTransaction),
        static_cast<unsigned long long>(kRevision),
        3ull);
    std::snprintf(
        conflict.data(),
        conflict.size(),
        "STR_PreRepair_T%016llX_R%016llX_A%016llX.skse",
        static_cast<unsigned long long>(historicalTransaction),
        static_cast<unsigned long long>(kRevision),
        3ull);
    sandbox.Touch(regular.data());
    std::error_code ec;
    std::filesystem::create_directory(
        sandbox.Paths.SavesDirectory / conflict.data(),
        ec);
    REQUIRE_FALSE(ec);

    const auto decision =
        PartyQuestPreRepairCaptureAttemptPolicy::ReclaimHistoricalAttempts(
            sandbox.Paths,
            kTransaction,
            kRevision);
    REQUIRE(decision.Status ==
        PartyQuestPreRepairCaptureAttemptStatus::RetentionConflict);
    REQUIRE(decision.ReclaimedFileCount == 0);
    REQUIRE(std::filesystem::exists(sandbox.Paths.SavesDirectory / regular.data()));
    REQUIRE(std::filesystem::is_directory(
        sandbox.Paths.SavesDirectory / conflict.data()));
}
