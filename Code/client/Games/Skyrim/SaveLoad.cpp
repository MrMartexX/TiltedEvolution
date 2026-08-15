#include <TiltedOnlinePCH.h>

#include <Events/EventDispatcher.h>
#include <PartyQuestP0LiveDiagnostics.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>
#include <Structs/Skyrim/PartyQuestRuntimeSessionOwner.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

#include <mutex>
#include <optional>

TP_THIS_FUNCTION(
    TBGSSaveLoadManager_SaveImpl,
    bool,
    BGSSaveLoadManager,
    int32_t,
    uint32_t,
    const char*);

TP_THIS_FUNCTION(
    TBGSSaveLoadManager_LoadImpl,
    bool,
    BGSSaveLoadManager,
    const char*,
    int32_t,
    uint32_t,
    bool);

static TBGSSaveLoadManager_SaveImpl* RealBGSSaveLoadManager_SaveImpl = nullptr;
static TBGSSaveLoadManager_LoadImpl* RealBGSSaveLoadManager_LoadImpl = nullptr;

namespace
{
struct PartyQuestPendingLoadTransition
{
    PartyQuestEngineLoadTicket SaveGuardTicket;
    PartyQuestRuntimeGenerationFence::LifecycleTransitionTicket GenerationTicket;
};

std::mutex s_partyQuestPendingLoadMutex;
std::optional<PartyQuestPendingLoadTransition> s_partyQuestPendingLoad;
std::mutex s_partyQuestLoadSinkMutex;
bool s_partyQuestLoadSinkInstalled = false;

bool CompletePendingPartyQuestLoad(const char* acReason) noexcept
{
    std::optional<PartyQuestPendingLoadTransition> pending;
    {
        std::lock_guard lock(s_partyQuestPendingLoadMutex);
        if (!s_partyQuestPendingLoad)
            return false;

        pending = *s_partyQuestPendingLoad;
        s_partyQuestPendingLoad.reset();
    }

    auto& guard = PartyQuestSaveGuard::GetProcessGuard();
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();

    // Clear the SaveGuard admission ticket first. Dispatch is still blocked by
    // the generation lifecycle ticket until the second exact completion below.
    // If either exact ticket fails, the surviving process-local barrier remains
    // fail-closed rather than being reconstructed from guessed state.
    const bool guardCompleted =
        guard.CompleteEngineLoad(pending->SaveGuardTicket);
    if (!guardCompleted)
    {
        spdlog::error(
            "PartyQuest LoadGame lifecycle completion failed SaveGuard ticket validation: reason={} ticket={}",
            acReason ? acReason : "<unknown>",
            pending->SaveGuardTicket.Value);
        return false;
    }

    const uint64_t before = generationFence.GetGeneration();
    const bool generationCompleted =
        generationFence.CompleteLifecycleTransition(pending->GenerationTicket);
    const uint64_t after = generationFence.GetGeneration();
    PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
        "load-game",
        acReason ? acReason : "completion",
        before,
        after);

    if (!generationCompleted)
    {
        spdlog::error(
            "PartyQuest LoadGame lifecycle completion failed generation ticket validation: reason={} ticket={} admittedGeneration={} currentGeneration={}",
            acReason ? acReason : "<unknown>",
            pending->GenerationTicket.Ticket,
            pending->GenerationTicket.Generation,
            after);
        return false;
    }

    spdlog::info(
        "PartyQuest LoadGame lifecycle transition completed: reason={} loadTicket={} generationTicket={} generation={}",
        acReason ? acReason : "<unknown>",
        pending->SaveGuardTicket.Value,
        pending->GenerationTicket.Ticket,
        after);
    return true;
}

class PartyQuestLoadGameEventSink final : public BSTEventSink<TESLoadGameEvent>
{
public:
    BSTEventResult OnEvent(
        const TESLoadGameEvent*,
        const EventDispatcher<TESLoadGameEvent>*) override
    {
        if (!CompletePendingPartyQuestLoad("tes-load-game-event"))
        {
            // An unpaired load-complete event still invalidates all previously
            // observed runtime evidence. It cannot establish the missing
            // pre-load barrier, so diagnostics remain explicit and production
            // mutation stays fail-closed elsewhere.
            auto& fence = PartyQuestRuntimeGenerationFence::GetProcessFence();
            const uint64_t before = fence.GetGeneration();
            const uint64_t after = fence.Invalidate();
            PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
                "load-game",
                "unpaired-load-event",
                before,
                after);
            spdlog::warn(
                "PartyQuest observed TESLoadGameEvent without a matching admitted Load_Impl request; generation invalidated {} -> {}",
                before,
                after);
        }

        return BSTEventResult::kOk;
    }
};

PartyQuestLoadGameEventSink s_partyQuestLoadGameEventSink;
} // namespace

void InstallPartyQuestLoadGameLifecycleFence() noexcept
{
    try
    {
        std::lock_guard lock(s_partyQuestLoadSinkMutex);
        if (s_partyQuestLoadSinkInstalled)
            return;

        auto* pEventList = EventDispatcherManager::Get();
        if (!pEventList)
        {
            spdlog::error(
                "PartyQuest could not register TESLoadGameEvent lifecycle sink: EventDispatcherManager unavailable");
            return;
        }

        pEventList->loadGameEvent.RegisterSink(&s_partyQuestLoadGameEventSink);
        s_partyQuestLoadSinkInstalled = true;
        spdlog::info("PartyQuest TESLoadGameEvent lifecycle sink installed");
    }
    catch (...)
    {
        spdlog::error(
            "PartyQuest failed to register TESLoadGameEvent lifecycle sink");
    }
}

BGSSaveLoadManager* BGSSaveLoadManager::Get() noexcept
{
    // CommonLibSSE-NG: BGSSaveLoadManager::Singleton is
    // RELOCATION_ID(516860, 403340). STR VersionDb uses the AE-side id.
    POINTER_SKYRIMSE(BGSSaveLoadManager*, s_singleton, 403340);
    auto** ppSingleton = s_singleton.Get();
    return ppSingleton ? *ppSingleton : nullptr;
}

void BGSSaveLoadManager::Save(SaveData* apData)
{
    apData->flags |= 4;

    const char* cSaveName = "";
    if (apData->saveName)
        cSaveName = apData->saveName;
}

bool BGSSaveLoadManager::SaveByName(const char* acFileName) noexcept
{
    if (!acFileName || !*acFileName)
        return false;

    try
    {
        // Important: call the live relocation entry, not
        // RealBGSSaveLoadManager_SaveImpl. TP_HOOK patches this entry, so the
        // PartyQuest save guard still authorizes/denies the request.
        POINTER_SKYRIMSE(TBGSSaveLoadManager_SaveImpl, s_saveImpl, 35727);
        auto* pSaveImpl = s_saveImpl.Get();
        if (!pSaveImpl)
            return false;

        // CommonLibSSE-NG BGSSaveLoadManager::Save(name) uses exactly
        // Save_Impl(2, 0, name).
        return TiltedPhoques::ThisCall(
            pSaveImpl,
            this,
            2,
            0u,
            acFileName);
    }
    catch (...)
    {
        return false;
    }
}

bool TP_MAKE_THISCALL(
    PartyQuest_BGSSaveLoadManager_SaveImpl,
    BGSSaveLoadManager,
    int32_t aDeviceId,
    uint32_t aOutputStats,
    const char* acFileName)
{
    auto& guard = PartyQuestSaveGuard::GetProcessGuard();
    auto permit = guard.TryEnterEngineSave();
    if (!permit.IsAllowed())
    {
        PartyQuestP0LiveDiagnostics::RecordEngineSave(
            "blocked-by-save-guard",
            acFileName,
            guard.GetTransactionId(),
            aDeviceId,
            aOutputStats,
            false,
            true,
            false);
        spdlog::warn(
            "PartyQuest runtime blocked Skyrim save during critical repair: transaction={} device={} outputStats={} save={}",
            guard.GetTransactionId(),
            aDeviceId,
            aOutputStats,
            acFileName ? acFileName : "<null>");
        return false;
    }

    if (!RealBGSSaveLoadManager_SaveImpl)
    {
        PartyQuestP0LiveDiagnostics::RecordEngineSave(
            "original-unavailable",
            acFileName,
            guard.GetTransactionId(),
            aDeviceId,
            aOutputStats,
            true,
            true,
            false);
        spdlog::error(
            "PartyQuest runtime cannot enter Skyrim save pipeline: original BGSSaveLoadManager::Save_Impl is null");
        return false;
    }

    PartyQuestP0LiveDiagnostics::RecordEngineSave(
        "enter-original",
        acFileName,
        guard.GetTransactionId(),
        aDeviceId,
        aOutputStats,
        true,
        false,
        false);

    const bool result = TiltedPhoques::ThisCall(
        RealBGSSaveLoadManager_SaveImpl,
        apThis,
        aDeviceId,
        aOutputStats,
        acFileName);

    PartyQuestP0LiveDiagnostics::RecordEngineSave(
        "return-original",
        acFileName,
        guard.GetTransactionId(),
        aDeviceId,
        aOutputStats,
        true,
        true,
        result);
    return result;
}

bool TP_MAKE_THISCALL(
    PartyQuest_BGSSaveLoadManager_LoadImpl,
    BGSSaveLoadManager,
    const char* acFileName,
    int32_t aDeviceId,
    uint32_t aOutputStats,
    bool aCheckForMods)
{
    // The process runtime owner owns persisted repair disposition. Fence it
    // before the engine load admission so deferred work (which intentionally
    // holds no physical SaveGuard yet) cannot survive across Load Game.
    auto& runtimeOwner = PartyQuestRuntimeSessionOwner::GetProcessOwner();
    const auto lifecycle = runtimeOwner.PrepareAndRelease(
        PartyQuestRuntimeLifecycleEvent::LoadGame);
    if (!lifecycle.CanProceed())
    {
        spdlog::warn(
            "PartyQuest runtime blocked Skyrim LoadGame because durable lifecycle disposition is unresolved: status={} transaction={} guardHeld={} save={}",
            static_cast<uint32_t>(lifecycle.Status),
            lifecycle.TransactionId,
            lifecycle.GuardHeld,
            acFileName ? acFileName : "<null>");
        return false;
    }

    auto& guard = PartyQuestSaveGuard::GetProcessGuard();
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();

    const auto loadTicket = guard.BeginEngineLoad();
    if (!loadTicket.IsValid())
    {
        spdlog::warn(
            "PartyQuest runtime blocked Skyrim LoadGame during critical/pending lifecycle state: transaction={} loadPending={} save={}",
            guard.GetTransactionId(),
            guard.IsEngineLoadPending(),
            acFileName ? acFileName : "<null>");
        return false;
    }

    const uint64_t generationBefore = generationFence.GetGeneration();
    const auto generationTicket = generationFence.BeginLifecycleTransition();
    if (!generationTicket.IsValid())
    {
        const bool released = guard.CompleteEngineLoad(loadTicket);
        spdlog::error(
            "PartyQuest runtime blocked Skyrim LoadGame because generation lifecycle admission failed: loadTicket={} saveGuardReleased={} save={}",
            loadTicket.Value,
            released,
            acFileName ? acFileName : "<null>");
        return false;
    }

    {
        std::lock_guard lock(s_partyQuestPendingLoadMutex);
        if (s_partyQuestPendingLoad)
        {
            const bool generationReleased =
                generationFence.CompleteLifecycleTransition(generationTicket);
            const bool guardReleased = guard.CompleteEngineLoad(loadTicket);
            spdlog::error(
                "PartyQuest runtime detected duplicate local LoadGame lifecycle state: loadTicket={} generationTicket={} generationReleased={} saveGuardReleased={}",
                loadTicket.Value,
                generationTicket.Ticket,
                generationReleased,
                guardReleased);
            return false;
        }

        s_partyQuestPendingLoad = PartyQuestPendingLoadTransition{
            loadTicket,
            generationTicket};
    }

    PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
        "load-game",
        "request-admitted",
        generationBefore,
        generationTicket.Generation);

    if (!RealBGSSaveLoadManager_LoadImpl)
    {
        CompletePendingPartyQuestLoad("original-unavailable");
        spdlog::error(
            "PartyQuest runtime cannot enter Skyrim load pipeline: original BGSSaveLoadManager::Load_Impl is null");
        return false;
    }

    const bool result = TiltedPhoques::ThisCall(
        RealBGSSaveLoadManager_LoadImpl,
        apThis,
        acFileName,
        aDeviceId,
        aOutputStats,
        aCheckForMods);

    if (!result)
    {
        // The exact asynchronous failure/cancellation contract is not yet
        // proven. Do not infer that the engine transition is over merely from
        // this return value; retain both tickets until TESLoadGameEvent or
        // process restart rather than opening a mutation race.
        spdlog::warn(
            "PartyQuest BGSSaveLoadManager::Load_Impl returned false; retaining fail-closed lifecycle tickets until an authoritative completion event: loadTicket={} generationTicket={} save={}",
            loadTicket.Value,
            generationTicket.Ticket,
            acFileName ? acFileName : "<null>");
    }

    return result;
}

static TiltedPhoques::Initializer s_partyQuestSaveLoadGuardHook(
    []()
    {
        // CommonLibSSE-NG: BGSSaveLoadManager::Save_Impl is
        // RELOCATION_ID(34818, 35727), Load_Impl is RELOCATION_ID(34819, 35728).
        // STR VersionDb uses the AE-side Address Library ids.
        POINTER_SKYRIMSE(TBGSSaveLoadManager_SaveImpl, s_saveImpl, 35727);
        POINTER_SKYRIMSE(TBGSSaveLoadManager_LoadImpl, s_loadImpl, 35728);

        RealBGSSaveLoadManager_SaveImpl = s_saveImpl.Get();
        if (!RealBGSSaveLoadManager_SaveImpl)
        {
            spdlog::error(
                "PartyQuest runtime failed to resolve BGSSaveLoadManager::Save_Impl (Address Library id 35727); save interception not installed");
        }
        else
        {
            TP_HOOK(
                &RealBGSSaveLoadManager_SaveImpl,
                PartyQuest_BGSSaveLoadManager_SaveImpl);
        }

        RealBGSSaveLoadManager_LoadImpl = s_loadImpl.Get();
        if (!RealBGSSaveLoadManager_LoadImpl)
        {
            spdlog::error(
                "PartyQuest runtime failed to resolve BGSSaveLoadManager::Load_Impl (Address Library id 35728); LoadGame lifecycle interception not installed");
        }
        else
        {
            TP_HOOK(
                &RealBGSSaveLoadManager_LoadImpl,
                PartyQuest_BGSSaveLoadManager_LoadImpl);
        }
    });