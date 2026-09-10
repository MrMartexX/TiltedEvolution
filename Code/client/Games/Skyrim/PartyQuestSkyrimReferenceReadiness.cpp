#include <TiltedOnlinePCH.h>

#include <Events/EventDispatcher.h>
#include <PartyQuestSkyrimReferenceReadiness.h>
#include <Structs/Skyrim/PartyQuestExternalSinkLifetime.h>
#include <Structs/Skyrim/PartyQuestRuntimeReferenceReadiness.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace
{
// CommonLibSSE-NG TESObjectLoadedEvent ABI: FormID at +0, loaded at +4,
// total size 8. Keep EventDispatcher.h's legacy opaque declaration untouched;
// only this P0 evidence sink depends on the verified payload layout.
struct PartyQuestTESObjectLoadedEventLayout
{
    uint32_t FormId{};
    bool Loaded{};
    uint8_t Padding[3]{};
};
static_assert(sizeof(PartyQuestTESObjectLoadedEventLayout) == 0x8);
static_assert(offsetof(PartyQuestTESObjectLoadedEventLayout, Loaded) == 0x4);

PartyQuestRuntimeReferenceReadiness s_partyQuestReferenceReadiness;
std::mutex s_partyQuestReferenceSinkMutex;
EventDispatcher<TESObjectLoadedEvent>* s_pPartyQuestReferenceDispatcher = nullptr;

class PartyQuestObjectLoadedEventSink final : public BSTEventSink<TESObjectLoadedEvent>
{
public:
    BSTEventResult OnEvent(
        const TESObjectLoadedEvent* apEvent,
        const EventDispatcher<TESObjectLoadedEvent>*) override
    {
        if (!apEvent)
            return BSTEventResult::kOk;

        const auto* pObserved =
            reinterpret_cast<const PartyQuestTESObjectLoadedEventLayout*>(apEvent);
        (void)s_partyQuestReferenceReadiness.Observe(
            pObserved->FormId,
            pObserved->Loaded);
        return BSTEventResult::kOk;
    }
};

PartyQuestObjectLoadedEventSink s_partyQuestObjectLoadedEventSink;
} // namespace

PartyQuestRuntimeReferenceReadiness& GetPartyQuestSkyrimReferenceReadiness() noexcept
{
    return s_partyQuestReferenceReadiness;
}

void InstallPartyQuestSkyrimReferenceReadiness() noexcept
{
    try
    {
        std::lock_guard lock(s_partyQuestReferenceSinkMutex);
        if (s_pPartyQuestReferenceDispatcher)
            return;

        auto* pEventList = EventDispatcherManager::Get();
        if (!pEventList)
        {
            spdlog::error(
                "PartyQuest could not register TESObjectLoadedEvent readiness sink: EventDispatcherManager unavailable");
            return;
        }

        auto* pDispatcher = &pEventList->objectLoadedEvent;
        pDispatcher->RegisterSink(&s_partyQuestObjectLoadedEventSink);
        s_pPartyQuestReferenceDispatcher = pDispatcher;
        spdlog::info(
            "PartyQuest TESObjectLoadedEvent reference readiness sink installed");
    }
    catch (...)
    {
        spdlog::error(
            "PartyQuest failed to register TESObjectLoadedEvent reference readiness sink");
    }
}

void UninstallPartyQuestSkyrimReferenceReadiness() noexcept
{
    std::lock_guard lock(s_partyQuestReferenceSinkMutex);
    PartyQuestReleaseExternalSink(
        s_pPartyQuestReferenceDispatcher,
        &s_partyQuestObjectLoadedEventSink);
}
