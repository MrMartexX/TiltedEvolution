#include <TiltedOnlinePCH.h>

#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

void BGSSaveLoadManager::Save(SaveData* apData)
{
    apData->flags |= 4;

    const char* cSaveName = "";
    if (apData->saveName)
        cSaveName = apData->saveName;
}

TP_THIS_FUNCTION(
    TBGSSaveLoadManager_SaveImpl,
    bool,
    BGSSaveLoadManager,
    int32_t,
    uint32_t,
    const char*);

static TBGSSaveLoadManager_SaveImpl* RealBGSSaveLoadManager_SaveImpl = nullptr;

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
        spdlog::error(
            "PartyQuest runtime cannot enter Skyrim save pipeline: original BGSSaveLoadManager::Save_Impl is null");
        return false;
    }

    // Keep the engine-save permit alive until the original call returns. This
    // prevents a critical repair lease from being published halfway through an
    // already-running save and prevents Release from racing a controlled save.
    return TiltedPhoques::ThisCall(
        RealBGSSaveLoadManager_SaveImpl,
        apThis,
        aDeviceId,
        aOutputStats,
        acFileName);
}

static TiltedPhoques::Initializer s_partyQuestSaveGuardHook(
    []()
    {
        // CommonLibSSE-NG: BGSSaveLoadManager::Save_Impl is
        // RELOCATION_ID(34818, 35727). STR's VersionDb/POINTER_SKYRIMSE uses
        // the AE-side Address Library ids, matching existing ids such as
        // ActorEquipManager::EquipObject=38894.
        POINTER_SKYRIMSE(TBGSSaveLoadManager_SaveImpl, s_saveImpl, 35727);

        RealBGSSaveLoadManager_SaveImpl = s_saveImpl.Get();
        if (!RealBGSSaveLoadManager_SaveImpl)
        {
            spdlog::error(
                "PartyQuest runtime failed to resolve BGSSaveLoadManager::Save_Impl (Address Library id 35727); save interception not installed");
            return;
        }

        TP_HOOK(
            &RealBGSSaveLoadManager_SaveImpl,
            PartyQuest_BGSSaveLoadManager_SaveImpl);
    });
