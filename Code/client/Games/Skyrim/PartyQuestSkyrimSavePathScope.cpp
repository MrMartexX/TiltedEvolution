#include <TiltedOnlinePCH.h>

#include <PartyQuestSkyrimSavePathScope.h>

#include <cstddef>
#include <cstring>

namespace
{
constexpr const char* kLocalSavePathSettingName = "sLocalSavePath:General";
constexpr size_t kSettingListOffset = 0x118;
constexpr size_t kMaxSettingNodes = 4096;

struct RuntimeSetting
{
    void* pVtable{};      // 00
    char* pStringValue{}; // 08 - Setting::Data::s
    char* pName{};        // 10
};
static_assert(sizeof(RuntimeSetting) == 0x18);

struct RuntimeSettingNode
{
    RuntimeSetting* pItem{};
    RuntimeSettingNode* pNext{};
};
static_assert(sizeof(RuntimeSettingNode) == 0x10);

struct RuntimeIniSettingCollection
{
    std::byte Padding[kSettingListOffset];
    RuntimeSettingNode Settings;
};
static_assert(offsetof(RuntimeIniSettingCollection, Settings) == kSettingListOffset);

RuntimeSetting* AsRuntimeSetting(void* apSetting) noexcept
{
    return static_cast<RuntimeSetting*>(apSetting);
}
} // namespace

std::mutex& PartyQuestSkyrimSavePathScope::GetOverrideMutex() noexcept
{
    static std::mutex mutex;
    return mutex;
}

void* PartyQuestSkyrimSavePathScope::FindLocalSavePathSetting() noexcept
{
    try
    {
        // CommonLibSSE-NG: INISettingCollection::Singleton is
        // RELOCATION_ID(524557, 411155). STR VersionDb uses the AE-side ids.
        // VersionDbPtr<T*> resolves to T**. Never dereference the relocation
        // result until the Address Library lookup itself has succeeded: a
        // missing/mismatched runtime id must fail closed rather than becoming a
        // native null dereference that C++ catch(...) cannot reliably contain.
        POINTER_SKYRIMSE(RuntimeIniSettingCollection*, s_iniSettings, 411155);
        RuntimeIniSettingCollection** ppCollection = s_iniSettings.Get();
        if (!ppCollection)
            return nullptr;

        RuntimeIniSettingCollection* pCollection = *ppCollection;
        if (!pCollection)
            return nullptr;

        RuntimeSettingNode* pNode = &pCollection->Settings;
        for (size_t i = 0; pNode && i < kMaxSettingNodes; ++i)
        {
            RuntimeSetting* pSetting = pNode->pItem;
            if (pSetting && pSetting->pName &&
                std::strcmp(pSetting->pName, kLocalSavePathSettingName) == 0)
            {
                return pSetting;
            }
            pNode = pNode->pNext;
        }
    }
    catch (...)
    {
    }

    return nullptr;
}

PartyQuestSkyrimSavePathScope::PartyQuestSkyrimSavePathScope(
    const PartyQuestCampaignId& acCampaignId,
    const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept
{
    try
    {
        m_relativePath = PartyQuestSkyrimSavePathPolicy::BuildRelativeSavePath(
            acCampaignId,
            acPlayerProfileId);
        if (!PartyQuestSkyrimSavePathPolicy::IsSafeRelativeSavePath(m_relativePath) ||
            !PartyQuestSkyrimSavePathPolicy::MatchesRelativeSavePath(
                m_relativePath,
                acCampaignId,
                acPlayerProfileId))
        {
            m_relativePath.clear();
            return;
        }

        // Serialize process-wide mutation of the shared INI Setting pointer.
        m_lock = std::unique_lock<std::mutex>(GetOverrideMutex());

        m_pSetting = FindLocalSavePathSetting();
        RuntimeSetting* pSetting = AsRuntimeSetting(m_pSetting);
        if (!pSetting || !pSetting->pStringValue)
            return;

        m_pOriginalValue = pSetting->pStringValue;
        pSetting->pStringValue = m_relativePath.data();
        m_armed = true;
    }
    catch (...)
    {
        m_armed = false;
        m_pSetting = nullptr;
        m_pOriginalValue = nullptr;
        m_relativePath.clear();
    }
}

PartyQuestSkyrimSavePathScope::~PartyQuestSkyrimSavePathScope()
{
    if (!m_armed || !m_pSetting)
        return;

    RuntimeSetting* pSetting = AsRuntimeSetting(m_pSetting);
    if (!pSetting)
        return;

    // Do not clobber an unexpected external replacement. Our storage is about
    // to be destroyed, so restoring is required only while the live setting
    // still points at this exact scope-owned buffer.
    if (pSetting->pStringValue == m_relativePath.data())
        pSetting->pStringValue = m_pOriginalValue;
}
