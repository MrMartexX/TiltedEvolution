#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

enum class PartyQuestCompatibilityEnvironmentCacheStatus : uint8_t
{
    Empty,
    Computing,
    Ready,
    Failed,
    Cancelled
};

struct PartyQuestCompatibilityPluginFile
{
    std::filesystem::path RelativePath;
    bool IsLite{};
};

struct PartyQuestCompatibilityEnvironmentSnapshot
{
    std::filesystem::path DataDirectory;
    std::vector<PartyQuestCompatibilityPluginFile> OrderedPlugins;
};

struct PartyQuestCompatibilityEnvironmentFingerprints
{
    uint64_t PluginEnvironment{};
    uint64_t ScriptEnvironment{};

    [[nodiscard]] bool IsValid() const noexcept
    {
        return PluginEnvironment != 0 && ScriptEnvironment != 0;
    }
};

/**
 * Owner-bound, one-shot cache for expensive file compatibility fingerprints.
 *
 * The snapshot contains paths and scalar identities only. The worker never
 * touches Skyrim objects. Destruction requests cooperative cancellation and
 * joins the worker before the owning module can unload.
 */
class PartyQuestCompatibilityEnvironmentCache final
{
public:
    PartyQuestCompatibilityEnvironmentCache() = default;
    ~PartyQuestCompatibilityEnvironmentCache() noexcept;

    PartyQuestCompatibilityEnvironmentCache(
        const PartyQuestCompatibilityEnvironmentCache&) = delete;
    PartyQuestCompatibilityEnvironmentCache& operator=(
        const PartyQuestCompatibilityEnvironmentCache&) = delete;

    [[nodiscard]] bool Start(
        PartyQuestCompatibilityEnvironmentSnapshot aSnapshot) noexcept;
    void Stop() noexcept;

    [[nodiscard]] PartyQuestCompatibilityEnvironmentCacheStatus
    GetStatus() const noexcept;
    [[nodiscard]] std::optional<PartyQuestCompatibilityEnvironmentFingerprints>
    GetReady() const noexcept;
    [[nodiscard]] uint64_t GetComputationCount() const noexcept;

private:
    mutable std::mutex m_mutex;
    std::optional<PartyQuestCompatibilityEnvironmentFingerprints> m_ready;
    std::jthread m_worker;
    std::atomic<PartyQuestCompatibilityEnvironmentCacheStatus> m_status{
        PartyQuestCompatibilityEnvironmentCacheStatus::Empty};
    std::atomic<uint64_t> m_computationCount{0};
};
