
#include <ScriptExtender.h>
#include <TiltedOnlinePCH.h>
#include <VersionDb.h>

namespace
{
constexpr wchar_t kScriptExtenderName[] = L"skse64";

constexpr char kScriptExtenderEntrypoint[] = "StartSKSE";

constexpr size_t kScriptExtenderNameLength = sizeof(kScriptExtenderName) / sizeof(wchar_t) - 1;

// AE+ only
// Use this to raise the SKSE baseline
constexpr int kSKSEMinBuild = 20100;

HMODULE g_SKSEModuleHandle{nullptr};

struct FileVersion
{
    static constexpr uint8_t scVersionSize = 4;
    DWORD versions[scVersionSize];
};

int GetFileVersion(const std::filesystem::path& acFilePath, FileVersion& aVersion)
{
    const auto filename = acFilePath.c_str();

    DWORD dwHandle = 0, sz = GetFileVersionInfoSizeW(filename, &dwHandle);
    if (0 == sz)
    {
        return 1;
    }
    std::string buf(sz, '\0');
    if (!GetFileVersionInfoW(filename, dwHandle, sz, &buf[0]))
    {
        return 2;
    }
    VS_FIXEDFILEINFO* pvi;
    sz = sizeof(VS_FIXEDFILEINFO);
    if (!VerQueryValueA(&buf[0], "\\", reinterpret_cast<LPVOID*>(&pvi), reinterpret_cast<unsigned int*>(&sz)))
    {
        return 3;
    }

    aVersion.versions[0] = pvi->dwProductVersionMS >> 16;
    aVersion.versions[1] = pvi->dwFileVersionMS & 0xFFFF;
    aVersion.versions[2] = pvi->dwFileVersionLS >> 16;
    aVersion.versions[3] = pvi->dwFileVersionLS & 0xFFFF;

    return 0;
}

std::string GetSKSEStyleExeVersion()
{
    // make sure newer than anniversary!
    auto exeBuild = VersionDb::Get().GetLoadedVersionString();
    std::replace(exeBuild.begin(), exeBuild.end(), '.', '_');

    // SKSE DLL names omit only an exact trailing zero build component, e.g.
    // 1.6.1170.0 -> 1_6_1170. Do not use find_last_of("_0"): that would also
    // truncate legitimate non-zero build values ending in zero.
    if (exeBuild.size() >= 2 &&
        exeBuild[exeBuild.size() - 2] == '_' &&
        exeBuild.back() == '0')
    {
        exeBuild.resize(exeBuild.size() - 2);
    }

    return exeBuild;
}
} // namespace

bool IsScriptExtenderLoaded()
{
    return g_SKSEModuleHandle != nullptr;
}

void LoadScriptExender()
{
    // RunTiltedInit is expected to call this once. Keep repeated calls
    // idempotent rather than taking an additional loader reference or invoking
    // StartSKSE twice.
    if (g_SKSEModuleHandle)
        return;

    try
    {
        const auto exeVersion{GetSKSEStyleExeVersion()};
        if (exeVersion.empty())
        {
            spdlog::error("Unable to derive Script Extender runtime filename");
            return;
        }

        const std::string expectedFileName =
            fmt::format("skse64_{}.dll", exeVersion);

        // Get the path of the game, where the Script Extender dll resides.
        std::error_code ec;
        const auto gameDir = std::filesystem::current_path(ec);
        if (ec || gameDir.empty())
        {
            spdlog::error(
                "Unable to inspect game directory for Script Extender: {}",
                ec.message());
            return;
        }

        std::filesystem::path needle;
        std::filesystem::directory_iterator it(gameDir, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec))
        {
            const auto& path = it->path();
            if (path.extension() != L".dll")
                continue;

            const auto fileName = path.filename().string();
            if (_stricmp(fileName.c_str(), expectedFileName.c_str()) == 0)
            {
                needle = path;
                break;
            }
        }

        if (ec)
        {
            spdlog::error(
                "Unable to enumerate game directory for Script Extender: {}",
                ec.message());
            return;
        }

        if (needle.empty())
            return;

        FileVersion fileVersion{};
        if (GetFileVersion(needle, fileVersion) != 0)
        {
            spdlog::error("Unable to verify Script Extender version");
            return;
        }

        auto skseVersion = fmt::format(
            "v{}.{}.{}.{}",
            fileVersion.versions[0],
            fileVersion.versions[1],
            fileVersion.versions[2],
            fileVersion.versions[3]);

        // nice try.
        const int skseVCum =
            fileVersion.versions[0] * 1000000 +
            fileVersion.versions[1] * 10000 +
            fileVersion.versions[2] * 100 +
            fileVersion.versions[3];
        if (skseVCum < kSKSEMinBuild)
        {
            spdlog::error("Pre anniversary Script Extender is unsupported");
            return;
        }

        HMODULE module = LoadLibraryW(needle.c_str());
        if (!module)
        {
            spdlog::error(
                "Failed to load {}! Check your privileges or re-download the Script Extender files.",
                needle.string());
            return;
        }

        auto* pStartSKSE = reinterpret_cast<void (*)()>(
            GetProcAddress(module, kScriptExtenderEntrypoint));
        if (!pStartSKSE)
        {
            spdlog::warn(
                "SKSE dll doesn't expose StartSKSE(), it may be outdated.");
            // A successfully loaded DLL without the required bootstrap export is
            // not an active Script Extender. Drop the loader reference and keep
            // IsScriptExtenderLoaded() false rather than publishing stale state.
            FreeLibrary(module);
            return;
        }

        // Publish the module only once the required bootstrap contract is
        // present. StartSKSE itself installs SKSE's pre/post CRT hooks.
        g_SKSEModuleHandle = module;
        spdlog::info(
            "Starting SKSE {}... be aware that messages that start without a colored [timestamp] prefix are "
            "logs from the Script Extender and its loaded mods.",
            skseVersion);
        pStartSKSE();
        spdlog::info("SKSE is active");
    }
    catch (const std::exception& acException)
    {
        spdlog::error(
            "Script Extender pre-entry discovery failed closed: {}",
            acException.what());
    }
    catch (...)
    {
        spdlog::error(
            "Script Extender pre-entry discovery failed closed with an unknown error");
    }
}
