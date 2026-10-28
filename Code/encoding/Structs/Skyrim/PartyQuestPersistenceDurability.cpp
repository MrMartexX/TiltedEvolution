#include <EncodingPch.h>

#include <Structs/Skyrim/PartyQuestPersistenceDurability.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace
{
std::atomic<bool> s_controlledActivationEnabled{false};

bool CheckControlledActivationEnv() noexcept
{
    const char* env = std::getenv("ST_EQUAL_PARTY_CONTROLLED_MUTATION");
    if (!env)
        env = std::getenv("TILTED_ONLINE_CONTROLLED_STAGE_MUTATION");
    if (!env)
        return false;

    return std::strcmp(env, "1") == 0 || std::strcmp(env, "true") == 0 || std::strcmp(env, "TRUE") == 0 || std::strcmp(env, "yes") == 0;
}
} // namespace

void PartyQuestPersistenceDurabilityPolicy::SetControlledActivationEnabled(bool aEnabled) noexcept
{
    s_controlledActivationEnabled.store(aEnabled, std::memory_order_release);
}

bool PartyQuestPersistenceDurabilityPolicy::IsControlledActivationEnabled() noexcept
{
    return s_controlledActivationEnabled.load(std::memory_order_acquire) || CheckControlledActivationEnv();
}

bool PartyQuestPersistenceDurabilityPolicy::AllowsNativeRuntimeMutation() noexcept
{
    // A research/mock activation request cannot grant production mutation authority.
    return IsProductionRuntimeMutationReady(CurrentLocalGuarantee);
}
