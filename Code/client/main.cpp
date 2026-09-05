
#include <TiltedOnlineApp.h>
#include <TiltedOnlinePCH.h>
#include <ScriptExtender.h>
#include <PartyQuestSkyrimNativeHookValidation.h>

#include <Commctrl.h>
#include <Windows.h>

#include <base/dialogues/win/TaskDialog.h>

std::unique_ptr<TiltedOnlineApp> g_appInstance{nullptr};

extern HICON g_SharedWindowIcon;

static void ShowAddressLibraryError(const wchar_t* apGamePath)
{
    auto errorDetail = fmt::format(L"Looking for it here: {}\\Data\\SKSE\\Plugins", apGamePath);

    Base::TaskDialog dia(g_SharedWindowIcon, L"Error", L"Failed to load Skyrim Address Library", L"Make sure to use \"All in one (1.6.X)\"", errorDetail.c_str());

    dia.AppendButton(0xBEED, L"Visit troubleshooting page on wiki.tiltedphoques.com");
    dia.AppendButton(0xBEEF, L"Visit Address Library modpage on nexusmods.com");
    const int result = dia.Show();
    if (result == 0xBEEF)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else if (result == 0xBEED)
    {
        ShellExecuteW(nullptr, L"open", LR"(https://wiki.tiltedphoques.com/tilted-online/guides/troubleshooting/address-library-error)", nullptr, nullptr, SW_SHOWNORMAL);
    }

    exit(4);
}

static void ShowNativeHookValidationError()
{
    Base::TaskDialog dia(
        g_SharedWindowIcon,
        L"Error",
        L"Skyrim native hook validation failed",
        L"Skyrim Together stopped before entering the game because one or more required P0 hooks did not resolve to the expected executable target or detour.",
        L"This normally indicates an incompatible Skyrim executable / Address Library pair or a conflicting native hook. No game code was entered after the failed validation. Check the SkyrimTogether log for the exact Address Library id and MinHook target.");
    dia.Show();
    exit(5);
}

void RunTiltedInit(const std::filesystem::path& acGamePath, const String& aExeVersion)
{
    if (!VersionDb::Get().Load(acGamePath, aExeVersion))
    {
        ShowAddressLibraryError(acGamePath.c_str());
    }

    // VersionDb::Get().DumpToTextFile(R"(S:\Work\Tilted\fallout\_addresslib.txt)");

    g_appInstance = std::make_unique<TiltedOnlineApp>();

    TiltedOnlineApp::InstallHooks2();

    // Do not let MinHook touch the mapped Skyrim image until every P0-critical
    // relocation has first been proven to resolve inside executable main-module
    // memory. This catches an incompatible executable / Address Library pair
    // before any crash-sensitive native mutation is attempted.
    if (!PartyQuestSkyrimNativeHookValidator::ValidateTargetsBeforeCommit())
        ShowNativeHookValidationError();

    TP_HOOK_COMMIT;

    // The shared delayed hook manager historically discards MinHook creation /
    // enable errors. After commit, decode MinHook's relay and require every
    // crash-sensitive target to route to our exact expected detour before SKSE
    // or Skyrim is entered.
    if (!PartyQuestSkyrimNativeHookValidator::ValidateAndPublish())
        ShowNativeHookValidationError();

    // SKSE installs hooks for both the pre- and post-CRT initialization
    // boundaries. Install those hooks before control reaches Skyrim's entry
    // point: BeginMain runs from GetStartupInfoW, after the pre-CRT boundary,
    // which leaves SKSE's trampoline pools uninitialized.
    LoadScriptExender();
}

void RunTiltedApp()
{
    g_appInstance->BeginMain();
}
