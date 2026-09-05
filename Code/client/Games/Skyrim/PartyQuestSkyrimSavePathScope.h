#pragma once

#include <Structs/Skyrim/PartyQuestSkyrimSavePath.h>

#include <mutex>
#include <string>

/**
 * Scoped Skyrim runtime override for sLocalSavePath:General.
 *
 * This class does not trigger a save. It only provides the narrow path-routing
 * primitive required by a later controlled checkpoint caller. Construction
 * fails closed unless the requested campaign/player path is canonical and the
 * live INI setting can be resolved with the expected string-setting layout.
 *
 * Conflicting scopes are serialized process-wide. The override string storage
 * is owned by this object and remains stable for the complete scope. On
 * destruction the original pointer is restored only if the live setting still
 * points at this scope's storage; an unexpected external setting replacement is
 * never overwritten.
 */
class PartyQuestSkyrimSavePathScope final
{
public:
    PartyQuestSkyrimSavePathScope(
        const PartyQuestCampaignId& acCampaignId,
        const PartyQuestPlayerProfileId& acPlayerProfileId) noexcept;
    ~PartyQuestSkyrimSavePathScope();

    PartyQuestSkyrimSavePathScope(const PartyQuestSkyrimSavePathScope&) = delete;
    PartyQuestSkyrimSavePathScope& operator=(const PartyQuestSkyrimSavePathScope&) = delete;
    PartyQuestSkyrimSavePathScope(PartyQuestSkyrimSavePathScope&&) = delete;
    PartyQuestSkyrimSavePathScope& operator=(PartyQuestSkyrimSavePathScope&&) = delete;

    [[nodiscard]] bool IsArmed() const noexcept { return m_armed; }
    [[nodiscard]] const std::string& GetRelativePath() const noexcept { return m_relativePath; }

private:
    static void* FindLocalSavePathSetting() noexcept;
    static std::mutex& GetOverrideMutex() noexcept;

    std::unique_lock<std::mutex> m_lock;
    void* m_pSetting{};
    char* m_pOriginalValue{};
    std::string m_relativePath;
    bool m_armed{};
};
