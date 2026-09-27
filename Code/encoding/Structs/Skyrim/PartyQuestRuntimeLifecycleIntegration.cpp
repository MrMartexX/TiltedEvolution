#include <Structs/Skyrim/PartyQuestRuntimeLifecycleIntegration.h>

#include <atomic>

namespace
{
constexpr uint8_t kLoadGame = 1u << 0u;
constexpr uint8_t kNewGame = 1u << 1u;
constexpr uint8_t kMainMenu = 1u << 2u;
constexpr uint8_t kCompleteCharacterIdentityCoverage =
    kLoadGame | kNewGame | kMainMenu;

// Installer callbacks record which exact lifecycle targets resolved and were
// queued for the shared delayed MinHook commit. This is deliberately not yet
// production-visible coverage: the shared hook manager can fail to create or
// enable a delayed hook without surfacing that failure.
std::atomic<uint8_t> s_queuedHooks{};
std::atomic_bool s_nativeHookCommitValidated{false};

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
    if (!s_nativeHookCommitValidated.load(std::memory_order_acquire))
        return false;

    const uint8_t mask = ToMask(aEvent);
    return mask != 0u &&
        (s_queuedHooks.load(std::memory_order_acquire) & mask) == mask;
}

bool PartyQuestRuntimeLifecycleIntegrationPolicy::
    HasCompleteCharacterIdentityCoverage() noexcept
{
    return s_nativeHookCommitValidated.load(std::memory_order_acquire) &&
        (s_queuedHooks.load(std::memory_order_acquire) &
             kCompleteCharacterIdentityCoverage) ==
        kCompleteCharacterIdentityCoverage;
}

void PartyQuestRuntimeLifecycleIntegrationPolicy::
    MarkVerifiedPreTransitionHook(
        PartyQuestRuntimeLifecycleEvent aEvent) noexcept
{
    const uint8_t mask = ToMask(aEvent);
    if (mask != 0u)
        s_queuedHooks.fetch_or(mask, std::memory_order_release);
}

void PartyQuestRuntimeLifecycleIntegrationPolicy::
    ConfirmNativeHookCommitValidated() noexcept
{
    if ((s_queuedHooks.load(std::memory_order_acquire) &
            kCompleteCharacterIdentityCoverage) !=
        kCompleteCharacterIdentityCoverage)
    {
        return;
    }

    s_nativeHookCommitValidated.store(true, std::memory_order_release);
}

void PartyQuestRuntimeLifecycleIntegrationPolicy::ResetForTests() noexcept
{
    s_queuedHooks.store(0u, std::memory_order_release);

    // The existing unit-test seam injects already-verified lifecycle hooks; it
    // does not instantiate MinHook or a mapped Skyrim image. Keep that synthetic
    // contract explicit here while production process state still starts false
    // and can become true only through ConfirmNativeHookCommitValidated().
    s_nativeHookCommitValidated.store(true, std::memory_order_release);
}
