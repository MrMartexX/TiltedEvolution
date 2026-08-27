#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>

#include <atomic>

namespace
{
constexpr uint8_t kLoadGame = 1u << 0u;
constexpr uint8_t kNewGame = 1u << 1u;
constexpr uint8_t kMainMenu = 1u << 2u;
constexpr uint8_t kCompleteCharacterIdentityCoverage =
    kLoadGame | kNewGame | kMainMenu;

std::atomic<uint8_t> s_verifiedHooks{};

uint8_t ToMask(PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    switch (aEvent)
    {
    case PartyQuestRuntimeLifecycleEvent::LoadGame: return kLoadGame;
    case PartyQuestRuntimeLifecycleEvent::NewGame: return kNewGame;
    case PartyQuestRuntimeLifecycleEvent::MainMenu: return kMainMenu;
    default: return 0u;
    }
}
} // namespace

bool PartyQuestRuntimeLifecycleIntegrationPolicy::HasVerifiedPreTransitionHook(
    PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    const uint8_t mask = ToMask(aEvent);
    return mask != 0u &&
        (s_verifiedHooks.load(std::memory_order_acquire) & mask) == mask;
}

bool PartyQuestRuntimeLifecycleIntegrationPolicy::
    HasCompleteCharacterIdentityCoverage() noexcept
{
    return (s_verifiedHooks.load(std::memory_order_acquire) &
               kCompleteCharacterIdentityCoverage) ==
        kCompleteCharacterIdentityCoverage;
}

void PartyQuestRuntimeLifecycleIntegrationPolicy::
    MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    const uint8_t mask = ToMask(aEvent);
    if (mask != 0u)
        s_verifiedHooks.fetch_or(mask, std::memory_order_release);
}

void PartyQuestRuntimeLifecycleIntegrationPolicy::ResetForTests() noexcept
{
    s_verifiedHooks.store(0u, std::memory_order_release);
}
