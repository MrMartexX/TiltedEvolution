#pragma once

#include <Structs/Skyrim/PartyQuestRuntimeLifecycleFence.h>

/**
 * Compile-time evidence ledger for production lifecycle integration.
 *
 * PartyQuestRuntimeLifecycleFence defines what must happen before a transition;
 * this policy records which real process boundaries are actually wired early
 * enough to invoke that fence. It intentionally does not infer coverage from a
 * post-transition notification or from the existence of a lifecycle enum.
 *
 * Character/save lineage authority may be published only when Load Game, New
 * Game and return-to-Main-Menu all have verified pre-transition coverage. Load
 * Game is currently wired in SaveLoad.cpp. New Game and Main Menu remain false
 * until equally strong pre-transition hooks are implemented and reviewed.
 */
struct PartyQuestRuntimeLifecycleIntegrationPolicy final
{
    [[nodiscard]] static constexpr bool HasVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept
    {
        switch (aEvent)
        {
        case PartyQuestRuntimeLifecycleEvent::LoadGame:
            return true;
        case PartyQuestRuntimeLifecycleEvent::NewGame:
        case PartyQuestRuntimeLifecycleEvent::MainMenu:
            return false;
        default:
            // Other lifecycle domains have separate network/process integration
            // and are not substitutes for character/save identity transitions.
            return false;
        }
    }

    [[nodiscard]] static constexpr bool HasCompleteCharacterIdentityCoverage() noexcept
    {
        return HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::LoadGame) &&
            HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::NewGame) &&
            HasVerifiedPreTransitionHook(PartyQuestRuntimeLifecycleEvent::MainMenu);
    }
};
