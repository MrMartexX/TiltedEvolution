#include <TiltedOnlinePCH.h>

#include <PartyQuestP0LiveDiagnostics.h>
#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

TP_THIS_FUNCTION(
    TBGSSaveLoadManager_LoadImpl,
    bool,
    BGSSaveLoadManager,
    const char*,
    int32_t,
    uint32_t,
    bool);

static TBGSSaveLoadManager_LoadImpl* RealBGSSaveLoadManager_LoadImpl = nullptr;

bool TP_MAKE_THISCALL(
    PartyQuest_BGSSaveLoadManager_LoadImpl,
    BGSSaveLoadManager,
    const char* acFileName,
    int32_t aDeviceId,
    uint32_t aOutputStats,
    bool aCheckForMods)
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generationBefore = generationFence.GetGeneration();

    // Load_Impl is the earliest verified Skyrim load boundary currently exposed
    // by this client. Publish a process-local lifecycle ticket before entering
    // the original engine call. TryAcquire() remains fail-closed for the complete
    // load even though no mutex ownership is carried across the long-running
    // engine operation.
    const auto lifecycle = generationFence.BeginLifecycleTransition();
    if (lifecycle.IsValid())
    {
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "engine-load-begin",
            "lifecycle-ticket-published",
            generationBefore,
            lifecycle.Generation);
    }
    else
    {
        // An already-pending lifecycle transition still blocks TryAcquire(). A
        // resolver/locking failure is deliberately not converted into authority.
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "engine-load-begin",
            "lifecycle-ticket-unavailable",
            generationBefore,
            generationFence.GetGeneration());
    }

    if (!RealBGSSaveLoadManager_LoadImpl)
    {
        spdlog::error(
            "PartyQuest P0 cannot enter Skyrim load pipeline: original BGSSaveLoadManager::Load_Impl is null");
        if (lifecycle.IsValid())
            (void)generationFence.CompleteLifecycleTransition(lifecycle);
        return false;
    }

    const bool result = TiltedPhoques::ThisCall(
        RealBGSSaveLoadManager_LoadImpl,
        apThis,
        acFileName,
        aDeviceId,
        aOutputStats,
        aCheckForMods);

    if (lifecycle.IsValid())
    {
        const bool completed = generationFence.CompleteLifecycleTransition(lifecycle);
        const uint64_t generationAfter = generationFence.GetGeneration();
        PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
            "engine-load-complete",
            completed ? "lifecycle-ticket-completed" : "lifecycle-ticket-completion-failed",
            lifecycle.Generation,
            generationAfter);

        if (!completed)
        {
            // A failed exact-ticket completion leaves the fence fail-closed; do
            // not attempt to manufacture a replacement generation authority.
            spdlog::error(
                "PartyQuest P0 Skyrim load lifecycle ticket could not be completed; runtime mutation remains blocked");
        }
    }

    return result;
}

static TiltedPhoques::Initializer s_partyQuestRuntimeLoadHook(
    []()
    {
        // CommonLibSSE-NG: BGSSaveLoadManager::Load_Impl is
        // RELOCATION_ID(34819, 35728). STR VersionDb uses the AE-side id, the
        // same convention already used by Save_Impl=35727 in SaveLoad.cpp.
        POINTER_SKYRIMSE(TBGSSaveLoadManager_LoadImpl, s_loadImpl, 35728);

        RealBGSSaveLoadManager_LoadImpl = s_loadImpl.Get();
        if (!RealBGSSaveLoadManager_LoadImpl)
        {
            spdlog::error(
                "PartyQuest P0 failed to resolve BGSSaveLoadManager::Load_Impl (Address Library id 35728); load lifecycle fencing not installed");
            return;
        }

        TP_HOOK(
            &RealBGSSaveLoadManager_LoadImpl,
            PartyQuest_BGSSaveLoadManager_LoadImpl);
        spdlog::info(
            "PartyQuest P0 installed Skyrim Load_Impl generation lifecycle fence");
    });
