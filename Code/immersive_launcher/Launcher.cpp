
#include <TiltedReverse/Code/reverse/include/Debug.hpp>
#include "TargetConfig.h"
#include "launcher.h"

#include "loader/ExeLoader.h"
#include "loader/PathRerouting.h"

#include "Utils/Error.h"
#include "Utils/FileVersion.inl"

#include "oobe/PathSelection.h"
#include "oobe/PathArgument.h"
#include "oobe/SupportChecks.h"
#include "steam/SteamLoader.h"

#include "base/dialogues/win/TaskDialog.h"
#include "utils/Registry.h"

#include <BranchInfo.h>

#include <array>
#include <cstdint>
#include <cstring>

// These symbols are defined within the client code skyrimtogetherclient
extern void InstallStartHook();
extern void RunTiltedApp();
extern void RunTiltedInit(const std::filesystem::path& acGamePath, const TiltedPhoques::String& aExeVersion);

// Defined in EarlyLoad.dll
bool __declspec(dllimport) EarlyInstallSucceeded();

HICON g_SharedWindowIcon = nullptr;

namespace launcher
{
static LaunchContext* g_context = nullptr;

namespace
{
constexpr std::size_t kGameEntryIntegrityProbeSize = 16;
std::array<std::uint8_t, kGameEntryIntegrityProbeSize> g_gameEntrySnapshot{};
bool g_gameEntrySnapshotValid = false;

const std::uint8_t* GetGameEntryBytes(const LaunchContext& aContext)
{
    return reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uintptr_t>(aContext.gameMain));
}

bool IsExecutableMemoryProbe(const void* apAddress, std::size_t aSize)
{
    if (!apAddress || aSize == 0)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(apAddress, &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & PAGE_GUARD) != 0)
    {
        return false;
    }

    switch (memory.Protect & 0xFFu)
    {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        break;
    default:
        return false;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(apAddress);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionStart + memory.RegionSize;
    return start >= regionStart && start <= regionEnd &&
        aSize <= regionEnd - start;
}

bool IsMappedGameEntryIdenticalToSource(
    const std::uint8_t* apSourceImage,
    std::size_t aSourceSize,
    ExeLoader::TEntryPoint aMappedEntry)
{
    if (!apSourceImage ||
        aSourceSize < sizeof(IMAGE_DOS_HEADER) ||
        !aMappedEntry)
    {
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(apSourceImage);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
        return false;

    const std::size_t ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    if (ntOffset > aSourceSize ||
        sizeof(IMAGE_NT_HEADERS) > aSourceSize - ntOffset)
    {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        apSourceImage + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const std::size_t sectionTableOffset =
        ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
        nt->FileHeader.SizeOfOptionalHeader;
    const std::size_t sectionTableSize =
        static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
        sizeof(IMAGE_SECTION_HEADER);
    if (sectionTableOffset > aSourceSize ||
        sectionTableSize > aSourceSize - sectionTableOffset)
    {
        return false;
    }

    const std::uint32_t entryRva = nt->OptionalHeader.AddressOfEntryPoint;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        apSourceImage + sectionTableOffset);
    for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& section = sections[i];
        const std::uint64_t sectionStart = section.VirtualAddress;
        const std::uint64_t copiedSize =
            section.SizeOfRawData < section.Misc.VirtualSize
                ? section.SizeOfRawData
                : section.Misc.VirtualSize;
        const std::uint64_t sectionEnd = sectionStart + copiedSize;
        const std::uint64_t probeEnd =
            static_cast<std::uint64_t>(entryRva) +
            kGameEntryIntegrityProbeSize;

        if (entryRva < sectionStart || probeEnd > sectionEnd)
            continue;

        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            return false;

        const std::uint64_t delta = entryRva - sectionStart;
        const std::uint64_t sourceOffset =
            static_cast<std::uint64_t>(section.PointerToRawData) + delta;
        if (sourceOffset > aSourceSize ||
            kGameEntryIntegrityProbeSize > aSourceSize - sourceOffset)
        {
            return false;
        }

        const auto moduleBase = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        const auto mappedAddress = reinterpret_cast<std::uintptr_t>(
            aMappedEntry);
        if (!moduleBase || mappedAddress != moduleBase + entryRva ||
            !IsExecutableMemoryProbe(
                reinterpret_cast<const void*>(mappedAddress),
                kGameEntryIntegrityProbeSize))
        {
            return false;
        }

        return std::memcmp(
                   apSourceImage + sourceOffset,
                   reinterpret_cast<const void*>(mappedAddress),
                   kGameEntryIntegrityProbeSize) == 0;
    }

    return false;
}

bool CaptureGameEntrySnapshot(const LaunchContext& aContext)
{
    const auto* pEntry = GetGameEntryBytes(aContext);
    if (!pEntry)
        return false;

    std::memcpy(g_gameEntrySnapshot.data(), pEntry, g_gameEntrySnapshot.size());
    g_gameEntrySnapshotValid = true;
    return true;
}

bool IsGameEntrySnapshotIntact(const LaunchContext& aContext)
{
    if (!g_gameEntrySnapshotValid)
        return false;

    const auto* pEntry = GetGameEntryBytes(aContext);
    return pEntry && std::memcmp(g_gameEntrySnapshot.data(), pEntry, g_gameEntrySnapshot.size()) == 0;
}
} // namespace

LaunchContext* GetLaunchContext()
{
#if 0
    if (!g_context)
        __debugbreak();
#endif
    return g_context;
}

bool LaunchContext::GetLoaded()
{
    return isLoaded;
}

// Everything is nothing, life is worth living, just look to the stars
#define DIE_NOW(err)  \
    {                 \
        Die(err);     \
        return false; \
    }

void SetMaxstdio()
{
    const auto handle = GetModuleHandleW(L"API-MS-WIN-CRT-STDIO-L1-1-0.DLL");
    if (!handle)
        return;

    const auto setmaxstdioFunc = reinterpret_cast<decltype(&_setmaxstdio)>(GetProcAddress(handle, "_setmaxstdio"));

    if (!setmaxstdioFunc)
        return;

    setmaxstdioFunc(8192);
}

int StartUp(int argc, char** argv)
{
    bool askSelect = (GetAsyncKeyState(VK_SPACE) & 0x8000);
    if (!HandleArguments(argc, argv, askSelect))
        return -1;

    // TODO(Force): Make some InitSharedResources func.
    g_SharedWindowIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102));

#if (!IS_MASTER)
    TiltedPhoques::Debug::CreateConsole();
#endif

    SetMaxstdio();

    if (!EarlyInstallSucceeded())
        DIE_NOW(L"Early load install failed. Tell Force about this.");

    auto LC = std::make_unique<LaunchContext>();
    g_context = LC.get();

    {
        const wchar_t* ec = nullptr;
        const auto status = oobe::ReportModCompatabilityStatus();
        switch (status)
        {
        case oobe::CompatabilityStatus::kDX11Unsupported: ec = L"Device does not support DirectX 11"; break;
        case oobe::CompatabilityStatus::kOldOS: ec = L"Operating system unsupported. Please upgrade to Windows 8.1 or greater"; break;
        }

        if (ec)
            DIE_NOW(ec);
    }

    if (!oobe::SelectInstall(askSelect))
        DIE_NOW(L"Failed to select game install.");

    // Bind path environment.
    loader::InstallPathRouting(LC->gamePath);
    steam::Load(LC->gamePath);

    if (!LoadProgram(*LC))
        return 3;

    InstallStartHook();
    if (!IsGameEntrySnapshotIntact(*LC))
        DIE_NOW(L"Skyrim executable entry point was modified by launcher startup hooks. Startup aborted before executing corrupted game code.");

    // Initialize all hooks before calling game init
    // TiltedPhoques::Initializer::RunAll();
    RunTiltedInit(LC->gamePath, LC->Version);
    if (!IsGameEntrySnapshotIntact(*LC))
        DIE_NOW(L"Skyrim executable entry point was modified during client/SKSE pre-start initialization. This indicates an incompatible executable, address library, or invalid hook target. Startup aborted before executing corrupted game code.");

    // This shouldn't return until the game is killed
    LC->gameMain();
    return 0;
}

bool LoadProgram(LaunchContext& LC)
{
    auto content = TiltedPhoques::LoadFile(LC.exePath);
    if (content.empty())
        DIE_NOW(L"Failed to mount game executable");

    LC.Version = QueryFileVersion(LC.exePath.c_str());
    if (LC.Version.empty())
        DIE_NOW(L"Failed to query game version");
    LC.SetLoaded();

    ExeLoader loader(CurrentTarget.exeLoadSz);
    if (!loader.Load(reinterpret_cast<uint8_t*>(content.data())))
        DIE_NOW(L"Fatal error while mapping executable");

    const auto gameMain = loader.GetEntryPoint();
    if (!IsMappedGameEntryIdenticalToSource(
            reinterpret_cast<const std::uint8_t*>(content.data()),
            content.size(),
            gameMain))
    {
        DIE_NOW(L"Skyrim executable entry point does not match the post-decryption source image after mapping. Startup aborted before any STR/SKSE hooks execute.");
    }

    LC.gameMain = gameMain;
    if (!CaptureGameEntrySnapshot(LC))
        DIE_NOW(L"Failed to capture Skyrim executable entry point integrity snapshot");

    return true;
}

void InitClient()
{
    // Jump into client code.
    RunTiltedApp();
}

bool HandleArguments(int aArgc, char** aArgv, bool& aAskSelect)
{
    for (int i = 1; i < aArgc; i++)
    {
        if (std::strcmp(aArgv[i], "-r") == 0)
            aAskSelect = true;
        else if (std::strcmp(aArgv[i], "--exePath") == 0)
        {
            if (i + 1 >= aArgc)
            {
                SetLastError(ERROR_BAD_PATHNAME);
                Die(L"No exe path specified", true);
                return false;
            }

            if (!oobe::PathArgument(aArgv[i + 1]))
            {
                SetLastError(ERROR_BAD_ARGUMENTS);
                Die(L"Failed to parse path argument", true);
                return false;
            }
        }
    }

    return true;
}
} // namespace launcher

// CreateProcess in suspended mode.
// Inject usvfs_64.dll -> invoke InitHooks
// (https://github.com/ModOrganizer2/usvfs/blob/f8051c179dee114b7e06c5dab2482977c285d611/src/usvfs_dll/usvfs.cpp#L352)
// Resume proc

// InjectDLLRemoteThread ->SkipInit