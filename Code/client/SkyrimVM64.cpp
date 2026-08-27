#include <TiltedOnlinePCH.h>
#include "TiltedOnlineApp.h"

extern std::unique_ptr<TiltedOnlineApp> g_appInstance;

#include <GameVM.h>
#include <Games/Skyrim/SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>

struct Main
{
    void* Vtables[2];
    bool quitGame;
    bool resetGame;
    bool fullReset;
    bool gameActive;
    bool onIdle;
    bool reloadContent;
    bool freezeTime;
    bool freezeNextFrame;
};
static_assert(offsetof(Main, resetGame) == 0x11);
struct VMContext
{
    char pad[0x680];
    uint8_t inactive; // 0x680
};

TP_THIS_FUNCTION(TVMUpdate, int, VMContext, float);
TP_THIS_FUNCTION(TMainLoop, short, Main);
TP_THIS_FUNCTION(TVMDestructor, uintptr_t, void);

static TVMUpdate* VMUpdate = nullptr;
static TMainLoop* MainLoop = nullptr;
static TVMDestructor* VMDestructor = nullptr;

class PartyQuestSkyrimMainLoopLifecycleHookInstaller final
{
public:
    static void Mark() noexcept
    {
        PartyQuestRuntimeLifecycleIntegrationPolicy::
            MarkVerifiedPreTransitionHook(
                PartyQuestRuntimeLifecycleEvent::MainMenu);
    }
};

int TP_MAKE_THISCALL(HookVMUpdate, VMContext, float a2)
{
    if (apThis->inactive == 0)
        g_appInstance->Update();

    return TiltedPhoques::ThisCall(VMUpdate, apThis, a2);
}

short TP_MAKE_THISCALL(HookMainLoop, Main)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    PartyQuestEngineIdentityTransition transition;
    if (apThis && (apThis->resetGame || apThis->fullReset))
    {
        // Main::Update consumes these flags to perform the recurring reset back
        // to Main Menu. Enter the generation/owner fence before that engine
        // boundary, not when the menu becomes visible afterward.
        transition = BeginPartyQuestEngineIdentityTransition(
            PartyQuestRuntimeLifecycleEvent::MainMenu,
            "main-menu-reset");
        if (!transition.CanProceed())
            return 0;
    }

    const short result = TiltedPhoques::ThisCall(MainLoop, apThis);
    CompletePartyQuestEngineIdentityTransition(
        transition,
        "main-menu-reset");
    return result;
}

uintptr_t TP_MAKE_THISCALL(HookVMDestructor, void)
{
    TP_EMPTY_HOOK_PLACEHOLDER

    return TiltedPhoques::ThisCall(VMDestructor, apThis);
}

static TiltedPhoques::Initializer s_mainHooks(
    []()
    {
        POINTER_SKYRIMSE(TMainLoop, cMainLoop, 36564);
        POINTER_SKYRIMSE(TVMUpdate, cVMUpdate, 53926);
        POINTER_SKYRIMSE(TVMDestructor, cVMDestructor, 40412);

        VMUpdate = cVMUpdate.Get();
        MainLoop = cMainLoop.Get();
        VMDestructor = cVMDestructor.Get();

        TP_HOOK(&VMUpdate, HookVMUpdate);
        TP_HOOK(&MainLoop, HookMainLoop);
        if (MainLoop)
            PartyQuestSkyrimMainLoopLifecycleHookInstaller::Mark();
        TP_HOOK(&VMDestructor, HookVMDestructor);
    });

