#include <TiltedOnlinePCH.h>

#include <TiltedOnlineApp.h>

#include <DInputHook.hpp>
#include <dinput.h>
#include <WindowsHook.hpp>

#include <World.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Systems/RenderSystemD3D11.h>

#include <Services/OverlayService.h>
#include <Services/ImguiService.h>
#include <Services/DiscordService.h>

#include <PartyQuestP0LiveDiagnostics.h>
#include <PartyQuestSkyrimRuntimeThread.h>
#include <NvidiaUtil.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>

using TiltedPhoques::Debug;

TiltedOnlineApp::TiltedOnlineApp()
{
    // Set console code page to UTF-8 so console known how to interpret string data
    SetConsoleOutputCP(CP_UTF8);

    auto logPath = TiltedPhoques::GetPath() / "logs";

    std::error_code ec;
    create_directory(logPath, ec);

    auto rotatingLogger = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath / "tp_client.log", 1048576 * 5, 3);
    // rotatingLogger->set_level(spdlog::level::debug);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{console, rotatingLogger});
    logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [tid %t] %$ %v");
    spdlog::flush_every(std::chrono::seconds(1));
    set_default_logger(logger);

    // Optional, read-only P0 evidence recorder. It is disabled by default and
    // never grants runtime mutation authority.
    PartyQuestP0LiveDiagnostics::Initialize();
}

TiltedOnlineApp::~TiltedOnlineApp() = default;

void* TiltedOnlineApp::GetMainAddress() const
{
    POINTER_SKYRIMSE(void, winMain, 36544);

    return winMain.GetPtr();
}

bool TiltedOnlineApp::BeginMain()
{
    World::Create();
    World::Get().ctx().at<DiscordService>().Init();
    World::Get().ctx().emplace<RenderSystemD3D11>(World::Get().ctx().at<OverlayService>(), World::Get().ctx().at<ImguiService>());

    // TODO: Figure out a way to un-blacklist NvCamera64.dll (see DllBlocklist.cpp). Then this hack can be removed
    if (IsNvidiaOverlayLoaded())
        ApplyNvidiaFix();

    return true;
}

bool TiltedOnlineApp::EndMain()
{
    // EndMain is the explicit orderly teardown boundary. Fence the persisted
    // runtime owner before hooks/resources disappear so pre-mutation work is
    // durably aborted and post-mutation/recovery-blocked evidence is retained
    // for exact restart recovery rather than being mistaken for a clean exit.
    auto& runtimeOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto lifecycle = runtimeOwner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::Shutdown);
    if (!lifecycle.CanProceed())
    {
        // Process shutdown itself is not reversible. Do not fabricate a local
        // restore here; leave the durable journal/workspace evidence intact so
        // the next process can reconcile through the normal recovery path.
        spdlog::error(
            "PartyQuest orderly shutdown retained runtime recovery state: status={} transaction={} guardHeld={}",
            static_cast<uint32_t>(lifecycle.Status),
            lifecycle.TransactionId,
            lifecycle.GuardHeld);
    }
    else if (lifecycle.Status ==
             PartyQuestRuntimeLifecycleFenceStatus::SafeAbortApplied)
    {
        spdlog::info(
            "PartyQuest orderly shutdown durably aborted pre-mutation runtime work: transaction={}",
            lifecycle.TransactionId);
    }

    // World owns services registered with Skyrim event dispatchers. Destroy it
    // while the engine and our code are still fully alive so those external
    // sinks are detached before hook/DLL teardown begins.
    World::Destroy();
    UninstallHooks();
    if (m_pDevice)
        m_pDevice->Release();

    return true;
}

void TiltedOnlineApp::Update()
{
    // Bind canonical Skyrim mutation to the same frame-update thread that drains
    // RunnerService and drives World::Update. A direct network-thread executor
    // call will therefore fail closed; queued work must perform the complete
    // guarded Dispatch after it reaches this thread rather than carrying a
    // prevalidated capability across the queue boundary.
    (void)PartyQuestSkyrimRuntimeThread::ObserveCurrentUpdateThread();
    PartyQuestP0LiveDiagnostics::RecordPapyrusRuntimeObservation();

    // Reverting a change that used to be here to disable bUseFaceGenPreprocessedHeads==true (which is 
    // the default) handling. Extensive testing over months by multiple parties showed that enabling 
    // the flag introduces no issues WITH PROPERLY GENERATED CHARACTERS (in-game character generation 
    // or showracemenu). The shortcut of  "coc riverwood" from the main menu skips proper character generation.
    // 
    // Plus, having it on  has some benefits like helping with neck seams. Comment to avoid revisiting.
    // 
    // There are still some issues to track down, like hair color and maybe face tint not syncing correctly,
    // but they are unrelated and unchanged by this flag.
    // 
 
    // Make sure the window stays active
    POINTER_SKYRIMSE(uint32_t, bAlwaysActive, 380768);

    *bAlwaysActive = 1;

    World::Get().Update();
}

bool TiltedOnlineApp::Attach()
{
    TiltedPhoques::Debug::OnAttach();

    // TiltedPhoques::Nop(0x1405D3FA1, 6);
    return true;
}

bool TiltedOnlineApp::Detach()
{
    TiltedPhoques::Debug::OnDetach();
    return true;
}

void TiltedOnlineApp::InstallHooks2()
{
    TiltedPhoques::Initializer::RunAll();

    TiltedPhoques::DInputHook::Install();
    TiltedPhoques::DInputHook::Get().SetToggleKeys({DIK_F2, DIK_RCONTROL});
}

void TiltedOnlineApp::UninstallHooks()
{
}

void TiltedOnlineApp::ApplyNvidiaFix() noexcept
{
    auto d3dFeatureLevelOut = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = CreateEarlyDxDevice(&m_pDevice, &d3dFeatureLevelOut);
    if (FAILED(hr))
        spdlog::error("D3D11CreateDevice failed. Detected an NVIDIA GPU, error code={0:x}", hr);

    if (d3dFeatureLevelOut < D3D_FEATURE_LEVEL_11_0)
        spdlog::warn("Unexpected D3D11 feature level detected (< 11.0), may cause issues");
}
