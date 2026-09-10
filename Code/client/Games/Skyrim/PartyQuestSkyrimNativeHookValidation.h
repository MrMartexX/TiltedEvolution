#pragma once

/**
 * Two-phase validation for the crash-sensitive Skyrim hooks required by P0.
 *
 * ValidateTargetsBeforeCommit() proves that every required Address Library
 * target exists inside executable memory of the mapped Skyrim main module before
 * MinHook is allowed to mutate native code.
 *
 * ValidateAndPublish() runs after the shared delayed commit and proves that each
 * target is actually routed to our exact expected MinHook detour. Lifecycle/save
 * installation evidence is published only after the post-commit proof passes.
 */
class PartyQuestSkyrimNativeHookValidator final
{
public:
    [[nodiscard]] static bool ValidateTargetsBeforeCommit() noexcept;
    [[nodiscard]] static bool ValidateAndPublish() noexcept;
};
