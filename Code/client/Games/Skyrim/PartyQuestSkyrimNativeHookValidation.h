#pragma once

/**
 * Validate the crash-sensitive Skyrim hooks required by the P0 runtime after
 * the shared delayed MinHook commit has run. Success means every required
 * Address Library target is executable in the mapped main module and MinHook
 * reports the target enabled. Lifecycle/save installation evidence is published
 * only after all required targets pass.
 */
class PartyQuestSkyrimNativeHookValidator final
{
public:
    [[nodiscard]] static bool ValidateAndPublish() noexcept;
};
