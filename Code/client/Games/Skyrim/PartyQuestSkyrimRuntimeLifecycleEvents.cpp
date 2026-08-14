#include <TiltedOnlinePCH.h>

#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <PartyQuestP0LiveDiagnostics.h>
#include <Structs/Skyrim/PartyQuestRuntimeGenerationFence.h>

namespace
{
void InvalidateRuntimeGeneration(const char* acReason) noexcept
{
    auto& generationFence = PartyQuestRuntimeGenerationFence::GetProcessFence();
    const uint64_t generationBefore = generationFence.GetGeneration();

    // Keep the exclusive lease alive until the engine event has been published
    // into the P0 generation. Any dispatch already holding a shared execution
    // lease drains before this point; any later dispatch must observe the new
    // generation and rebuild its runtime compatibility witness.
    auto invalidation = generationFence.BeginInvalidation();
    const uint64_t generationAfter = invalidation.GetGeneration();

    PartyQuestP0LiveDiagnostics::RecordGenerationTransition(
        acReason,
        "engine-lifecycle-event",
        generationBefore,
        generationAfter);
}

class PartyQuestSkyrimRuntimeLifecycleEventSink final
    : public BSTEventSink<TESLoadGameEvent>
    , public BSTEventSink<TESResetEvent>
{
public:
    BSTEventResult OnEvent(
        const TESLoadGameEvent*,
        const EventDispatcher<TESLoadGameEvent>*) override
    {
        // TESLoadGameEvent is an engine load-game notification, not a pre-load
        // barrier. It still invalidates every witness from the previous loaded
        // world before the next P0 dispatch is allowed to reuse that generation.
        InvalidateRuntimeGeneration("engine-load-game");
        return BSTEventResult::kOk;
    }

    BSTEventResult OnEvent(
        const TESResetEvent*,
        const EventDispatcher<TESResetEvent>*) override
    {
        // Reset is broader than a quest-specific signal and therefore suitable
        // only as an invalidation source. It never grants runtime authority.
        InvalidateRuntimeGeneration("engine-reset");
        return BSTEventResult::kOk;
    }
};

PartyQuestSkyrimRuntimeLifecycleEventSink s_lifecycleEventSink;

TiltedPhoques::Initializer s_partyQuestRuntimeLifecycleEventRegistration(
    []()
    {
        auto* pEvents = EventDispatcherManager::Get();
        if (!pEvents)
        {
            spdlog::error(
                "PartyQuest P0 could not register Skyrim lifecycle generation invalidation: event dispatcher unavailable");
            return;
        }

        pEvents->loadGameEvent.RegisterSink(&s_lifecycleEventSink);
        pEvents->resetEvent.RegisterSink(&s_lifecycleEventSink);

        spdlog::info(
            "PartyQuest P0 registered Skyrim load-game/reset generation invalidation sinks");
    });
} // namespace
