#include <TiltedOnlinePCH.h>

#include <SaveLoad.h>
#include <Structs/Skyrim/PartyQuestSaveGuard.h>

TP_THIS_FUNCTION(
    TBGSSaveLoadManager_SaveImpl,
    bool,
    BGSSaveLoadManager,
    int32_t,
    uint32_t,
    const char*);

static TBGSSaveLoadManager_SaveImpl* RealBGSSaveLoadManager_SaveImpl = nullptr;

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

    // Keep the engine-save permit alive until the original call returns. SKSE
    // hooks its synchronous SaveGame path inside this call: SetSaveName(), the
    // .ess serialization and Serialization::HandleSaveGlobalData()/.skse
    // callbacks all occur before the hooked SaveGame call unwinds. Holding the
    // permit over the complete original call therefore also drains a save before
    // a critical repair lease can be published.
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
