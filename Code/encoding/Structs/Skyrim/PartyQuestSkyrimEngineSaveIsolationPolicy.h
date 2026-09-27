#pragma once

/**
 * Production policy for redirecting Skyrim's process-global sLocalSavePath
 * around an engine-generated PreRepair checkpoint.
 *
 * BGSSaveLoadManager owns an asynchronous save/load queue and SKSE constructs
 * its co-save path from sLocalSavePath inside the deeper save pipeline. Until
 * we can prove a completion/path-capture boundary that keeps both .ess and
 * .skse writes confined to the protected co-op replica, changing the global
 * setting around SaveByName is not authorized.
 *
 * Keep this fail-closed. Enabling it requires version-reviewed engine evidence
 * plus live validation of the exact supported runtime; a successful request
 * admission or SaveGuard alone is not sufficient evidence.
 */
class PartyQuestSkyrimEngineSaveIsolationPolicy final
{
public:
    [[nodiscard]] static constexpr bool AllowsProductionCapture() noexcept
    {
        return false;
    }
};
