#include <Structs/Skyrim/PartyQuestCompatibilityEnvironmentCache.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string>
#include <type_traits>

namespace
{
class CompatibilityHash final
{
public:
    void Mix(const void* apData, size_t aSize) noexcept
    {
        const auto* pData = static_cast<const uint8_t*>(apData);
        for (size_t i = 0; i < aSize; ++i)
        {
            m_value ^= pData[i];
            m_value *= 1099511628211ull;
        }
    }

    template <class T>
    void MixPod(const T& acValue) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        Mix(&acValue, sizeof(acValue));
    }

    void MixString(std::string aValue) noexcept
    {
        std::transform(aValue.begin(), aValue.end(), aValue.begin(),
            [](unsigned char aCharacter)
            {
                return static_cast<char>(std::tolower(aCharacter));
            });
        const uint64_t size = static_cast<uint64_t>(aValue.size());
        MixPod(size);
        if (!aValue.empty())
            Mix(aValue.data(), aValue.size());
    }

    [[nodiscard]] uint64_t Value() const noexcept
    {
        return m_value == 0 ? 1 : m_value;
    }

private:
    uint64_t m_value{14695981039346656037ull};
};

bool IsContainedRelativePath(const std::filesystem::path& acPath) noexcept
{
    if (acPath.empty() || acPath.is_absolute())
        return false;
    for (const auto& component : acPath)
    {
        if (component == "..")
            return false;
    }
    return true;
}

bool MixFile(
    CompatibilityHash& aHash,
    const std::filesystem::path& acPath,
    std::stop_token aStopToken) noexcept
{
    try
    {
        std::ifstream file(acPath, std::ios::binary);
        if (!file)
            return false;

        std::array<char, 64 * 1024> buffer{};
        uint64_t total = 0;
        while (file)
        {
            if (aStopToken.stop_requested())
                return false;
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = file.gcount();
            if (count > 0)
            {
                aHash.Mix(buffer.data(), static_cast<size_t>(count));
                total += static_cast<uint64_t>(count);
            }
        }
        if (!file.eof() || aStopToken.stop_requested())
            return false;

        aHash.MixPod(total);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

uint64_t HashOrderedPlugins(
    const PartyQuestCompatibilityEnvironmentSnapshot& acSnapshot,
    std::stop_token aStopToken) noexcept
{
    try
    {
        CompatibilityHash hash;
        constexpr uint64_t domain = 0x504C55474F524452ull; // "PLUGORDR"
        hash.MixPod(domain);

        uint64_t count = 0;
        for (const auto& plugin : acSnapshot.OrderedPlugins)
        {
            if (aStopToken.stop_requested() ||
                !IsContainedRelativePath(plugin.RelativePath))
            {
                return 0;
            }

            ++count;
            hash.MixPod(count);
            hash.MixString(plugin.RelativePath.generic_string());
            hash.MixPod(plugin.IsLite);
            if (!MixFile(
                    hash,
                    acSnapshot.DataDirectory / plugin.RelativePath,
                    aStopToken))
            {
                return 0;
            }
        }

        if (count == 0 || aStopToken.stop_requested())
            return 0;
        hash.MixPod(count);
        return hash.Value();
    }
    catch (...)
    {
        return 0;
    }
}

uint64_t HashScripts(
    const std::filesystem::path& acDataDirectory,
    std::stop_token aStopToken) noexcept
{
    try
    {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(acDataDirectory, ec), end;
             !ec && it != end;
             it.increment(ec))
        {
            if (aStopToken.stop_requested())
                return 0;
            if (!it->is_regular_file(ec) || ec)
                continue;
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char aCharacter)
                {
                    return static_cast<char>(std::tolower(aCharacter));
                });
            if (extension == ".bsa")
                files.push_back(it->path());
        }
        if (ec)
            return 0;

        const auto scripts = acDataDirectory / "Scripts";
        ec.clear();
        if (std::filesystem::is_directory(scripts, ec) && !ec)
        {
            for (std::filesystem::recursive_directory_iterator it(
                     scripts,
                     std::filesystem::directory_options::skip_permission_denied,
                     ec),
                 end;
                 !ec && it != end;
                 it.increment(ec))
            {
                if (aStopToken.stop_requested())
                    return 0;
                if (!it->is_regular_file(ec) || ec)
                    continue;
                auto extension = it->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char aCharacter)
                    {
                        return static_cast<char>(std::tolower(aCharacter));
                    });
                if (extension == ".pex")
                    files.push_back(it->path());
            }
            if (ec)
                return 0;
        }
        else if (ec)
        {
            return 0;
        }

        std::sort(files.begin(), files.end(), [](const auto& acLeft, const auto& acRight)
        {
            auto left = acLeft.generic_string();
            auto right = acRight.generic_string();
            std::transform(left.begin(), left.end(), left.begin(),
                [](unsigned char aCharacter)
                {
                    return static_cast<char>(std::tolower(aCharacter));
                });
            std::transform(right.begin(), right.end(), right.begin(),
                [](unsigned char aCharacter)
                {
                    return static_cast<char>(std::tolower(aCharacter));
                });
            return left < right;
        });

        CompatibilityHash hash;
        constexpr uint64_t domain = 0x534352495054454Eull; // "SCRIPTEN"
        hash.MixPod(domain);
        const uint64_t count = static_cast<uint64_t>(files.size());
        hash.MixPod(count);
        for (const auto& file : files)
        {
            if (aStopToken.stop_requested())
                return 0;
            std::error_code relativeEc;
            const auto relative = std::filesystem::relative(
                file, acDataDirectory, relativeEc);
            if (relativeEc || !IsContainedRelativePath(relative))
                return 0;
            hash.MixString(relative.generic_string());
            if (!MixFile(hash, file, aStopToken))
                return 0;
        }
        return aStopToken.stop_requested() ? 0 : hash.Value();
    }
    catch (...)
    {
        return 0;
    }
}
}

PartyQuestCompatibilityEnvironmentCache::~PartyQuestCompatibilityEnvironmentCache() noexcept
{
    Stop();
}

bool PartyQuestCompatibilityEnvironmentCache::Start(
    PartyQuestCompatibilityEnvironmentSnapshot aSnapshot) noexcept
{
    try
    {
        PartyQuestCompatibilityEnvironmentCacheStatus expected =
            PartyQuestCompatibilityEnvironmentCacheStatus::Empty;
        if (!m_status.compare_exchange_strong(
                expected,
                PartyQuestCompatibilityEnvironmentCacheStatus::Computing,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return false;
        }

        if (aSnapshot.DataDirectory.empty() ||
            !aSnapshot.DataDirectory.is_absolute() ||
            aSnapshot.OrderedPlugins.empty())
        {
            m_status.store(
                PartyQuestCompatibilityEnvironmentCacheStatus::Failed,
                std::memory_order_release);
            return false;
        }

        m_worker = std::jthread(
            [this, snapshot = std::move(aSnapshot)](std::stop_token aStopToken)
            {
                m_computationCount.fetch_add(1, std::memory_order_relaxed);
                PartyQuestCompatibilityEnvironmentFingerprints fingerprints;
                fingerprints.PluginEnvironment =
                    HashOrderedPlugins(snapshot, aStopToken);
                if (!aStopToken.stop_requested() &&
                    fingerprints.PluginEnvironment != 0)
                {
                    fingerprints.ScriptEnvironment =
                        HashScripts(snapshot.DataDirectory, aStopToken);
                }

                if (aStopToken.stop_requested())
                {
                    m_status.store(
                        PartyQuestCompatibilityEnvironmentCacheStatus::Cancelled,
                        std::memory_order_release);
                    return;
                }
                if (!fingerprints.IsValid())
                {
                    m_status.store(
                        PartyQuestCompatibilityEnvironmentCacheStatus::Failed,
                        std::memory_order_release);
                    return;
                }

                {
                    std::scoped_lock lock(m_mutex);
                    m_ready = fingerprints;
                }
                m_status.store(
                    PartyQuestCompatibilityEnvironmentCacheStatus::Ready,
                    std::memory_order_release);
            });
        return true;
    }
    catch (...)
    {
        m_status.store(
            PartyQuestCompatibilityEnvironmentCacheStatus::Failed,
            std::memory_order_release);
        return false;
    }
}

void PartyQuestCompatibilityEnvironmentCache::Stop() noexcept
{
    if (m_worker.joinable())
    {
        m_worker.request_stop();
        m_worker.join();
    }
    if (m_status.load(std::memory_order_acquire) ==
        PartyQuestCompatibilityEnvironmentCacheStatus::Computing)
    {
        m_status.store(
            PartyQuestCompatibilityEnvironmentCacheStatus::Cancelled,
            std::memory_order_release);
    }
}

PartyQuestCompatibilityEnvironmentCacheStatus
PartyQuestCompatibilityEnvironmentCache::GetStatus() const noexcept
{
    return m_status.load(std::memory_order_acquire);
}

std::optional<PartyQuestCompatibilityEnvironmentFingerprints>
PartyQuestCompatibilityEnvironmentCache::GetReady() const noexcept
{
    if (GetStatus() != PartyQuestCompatibilityEnvironmentCacheStatus::Ready)
        return std::nullopt;
    std::scoped_lock lock(m_mutex);
    return m_ready;
}

uint64_t PartyQuestCompatibilityEnvironmentCache::GetComputationCount() const noexcept
{
    return m_computationCount.load(std::memory_order_acquire);
}
