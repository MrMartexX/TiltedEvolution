#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeLifecycleFence.h>

#include <cstdint>

class PartyQuestSkyrimNativeHookValidator;
class PartyQuestRuntimeLifecycleIntegrationTestAccess;

/**
 * Process-local evidence ledger for production lifecycle integration.
 *
 * PartyQuestRuntimeLifecycleFence defines what must happen before a transition;
 * this policy records which real process boundaries are actually wired early
 * enough to invoke that fence. It intentionally does not infer coverage from a
 * post-transition notification or from the existence of a lifecycle enum.
 *
 * Character/save lineage authority may be published only when Load Game, New
 * Game and return-to-Main-Menu all have verified pre-transition coverage. A
 * hook is recorded only after its exact Address Library entry resolved and the
 * detour was proven enabled in this process. Merely compiling or queueing a
 * hook cannot make bootstrap authority available on an unsupported runtime.
 */
struct PartyQuestRuntimeLifecycleIntegrationPolicy final
{
    [[nodiscard]] static bool HasVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept;

    [[nodiscard]] static bool HasCompleteCharacterIdentityCoverage() noexcept;

private:
    static void MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept;
    static void ResetForTests() noexcept;

    friend class PartyQuestSkyrimNativeHookValidator;
    friend class PartyQuestRuntimeLifecycleIntegrationTestAccess;
};